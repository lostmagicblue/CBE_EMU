# 2026-07-07 Mock Service Account Isolation

## Problem

After the mock moved into TCP service mode, all remote clients still shared one
process-global Jianghu OL runtime:

- one role DB file: `nvram/jhol_mock_roles.bin`
- one active role pointer
- one battle/session/shop/title transient state set

That was acceptable for a single local loopback client, but it broke remote
multi-user deployment because different accounts could read and overwrite each
other's role rows, backpack, scene, and battle state.

## Current Contract

The mock service now carries an account identifier on every TCP request and uses
that account to select a dedicated runtime context.

Request frame:

```text
magic   "CBMS"
ver     1
flags   bit0 = ping
len     metadata_len + wt_request_len
aux     metadata_len
body    metadata bytes, then raw WT request bytes
```

Current metadata payload:

```text
u32 clientId
optional NUL-terminated explicit account ID
```

Response frame is unchanged:

```text
magic   "CBMR"
ver     1
flags   bit0 = closeAfterData
len     raw response length
aux     response event type
body    raw WT response bytes
```

## Account Selection

Client-side priority:

1. explicit `--mock-service-account=...` or `CBE_MOCK_SERVICE_ACCOUNT`
2. authenticated server-side session binding for that `clientId`
3. fallback `default`

This keeps no-account title flows working while still allowing explicit account
separation for games whose visible login path sends empty credentials.

For login packets that do carry non-empty `username` / `userName` plus
`password`, the service no longer auto-creates or auto-learns accounts. The
credentials must match a server-side account DB entry or the mock returns login
failure immediately.

Account DB path:

```text
nvram/mock_service_accounts.bin
```

Server console commands:

```text
help
account list
account create <username> <password>
account passwd <username> <newpassword>
```

## Implementation

`src/mock-server.c` now keeps a linked list of per-account service contexts.
Each context snapshots the mutable Jianghu OL server state that used to be
process-global, including:

- role DB load/valid bits and `vm_net_mock_role_db_file`
- title/server-select/shop transient flags
- battle serials, HP/MP, enemy-slot state, reward serials
- scene transfer / teleport / moveinfo / reload tracking
- pending scene-save bridge state used by scene enter follow-ups
- update chunk progress fields used by staged enter-game loading

The service request path:

1. reads `clientId` plus optional explicit account metadata from the TCP frame
2. if the WT request is a credential login, validates username/password against
   the account DB
3. on successful credential login, binds `clientId -> username`
4. resolves the current account from explicit account or bound session
5. finds or creates that account context
6. restores its saved runtime globals into the existing mock code
7. processes the WT request normally
8. captures the mutated globals back into that account context

This keeps the existing packet builders and parsers unchanged while isolating
state per account.

## Persistence Path

- account `default`: keep compatibility path `nvram/jhol_mock_roles.bin`
- named accounts: `nvram/accounts/<sanitized-account>/jhol_mock_roles.bin`

The path sanitizer keeps `[0-9A-Za-z_-]` and rewrites other bytes to `_`.

## Validation

- `make -j2`: passed
- service-mode smoke:
  - `--mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19193`
  - manual frame injection logged `account_init id=alice`, proving the account
    metadata/session binding path was parsed before WT dispatch

## Notes

- This pass isolates server-side state per account, not per simultaneous device
  session. Two live clients using the same account intentionally share one mock
  account state.
- No client/game packet contract was changed; only the outer mock-service TCP
  wrapper gained account metadata.
