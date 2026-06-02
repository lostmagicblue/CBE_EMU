# Protocol Notes

This file is the canonical packet/protocol summary for the project.

Status tags used in this file:

- `confirmed`: established by code inspection, xrefs, logs, memory observation, or repeatable runtime behavior
- `hypothesis`: plausible inference that still needs direct confirmation

When adding new findings here:

- separate transport framing from message semantics
- record request triggers and response side effects
- mark each important claim as `confirmed` or `hypothesis`
- include enough detail for future server implementation

## WT Packet Framing

Current mock/server-side packet construction uses the `WT` container.

Status: `confirmed`

Framing:

- `out[0..1] = "WT"`
- `out[2..3] = packet_len` in big-endian order
- `out[4] = object_count`
- each object header is 6 bytes:
  - `major`
  - `kind`
  - `subtype`
  - `0`
  - `object_len_be16`

Field encoding inside an object:

- field name is encoded as `name_len, name`
- field value is encoded as `value_len_be16, value`
- integer field values are internally stored as `0, byte_len, big_endian_value`
- string/blob field values are internally stored as `data_len_be16, data`

Implementation helpers already exist in `src/main.c` and should be reused instead of hand-building packet headers:

- `vm_net_mock_begin_wt_object`
- `vm_net_mock_finish_wt_object`
- `vm_net_mock_finish_wt_packet`
- `vm_net_mock_put_object_u8`
- `vm_net_mock_put_object_u32`
- `vm_net_mock_put_object_blob`
- `vm_net_mock_put_object_string`

## Startup Update Protocol

### Version Check Response

Trigger request contains `version`.

Status: `confirmed`

Correct response shape contains two objects:

- subtype `5`
  - field: `result`
- subtype `9`
  - fields: `type`, `id`, `code`

Why this is required:

- `handle_version_update_response` consumes subtype `5` field `result`
- `startup_update_net_callback` consumes subtype `9` and enters `startup_handle_update_metadata`
- subtype `5` alone can drive `update_state=2`, but the startup screen does not remove itself and the UI stalls
- `type=0` is the confirmed "no update / local data already usable" path and the CBE removes the startup screen on its own

When local `JHOnlineData/MMORPGTempcbm` and `JHOnlineData/mmorpg_updateversioncbm` already exist, the built-in version response currently returns:

- `result = 0`
- an additional close event `9` after the data event

Do not change that close event to `8`; the current client net wrapper does not route event `8` into the active object callback.

### Update Chunk Response

Trigger request contains `start` and `id`.

Status: `confirmed`

Response fields:

- `totalsize`
- `crc`
- `type`
- `name = MMORPGTempcbm`
- `data`

Rules:

- `start` must be echoed from the request field
- `crc` is the signed-byte running sum up to the end of the current chunk
- chunk size is currently limited to `0x1000`

Current payload source priority:

- `JHOnlineData/MMORPGTempcbm.mock`
- `JHOnlineData/mmBattleMstarWqvga.cbm`
- `JHOnlineData/mmGameMstarWqvga.cbm`
- `JHOnlineData/mmTitleMstarWqvga.cbm`

Do not reuse the already-installed `JHOnlineData/MMORPGTempcbm` as the download source, or the true install/update path will be masked.

### Verified Startup Chain

Status: `confirmed`

Startup/update path:

1. startup screen state advances to `7`
2. net open event `5` triggers `send_version_update_request`
3. mock returns subtype `5 + 9` version response
4. `handle_version_update_response` sets update flags and `update_state=2`
5. `startup_handle_update_metadata` parses `type/id/code`
6. the CBE removes the startup screen on its own
7. the emulator resumes the lower screen
8. the client loads `JHOnlineData/mmTitleMstarWqvga.cbm` and enters the dynamic CBM screen

## Login Protocol

### Overview

The runtime login UI lives in `mmTitleMstarWqvga.cbm`.

Status: `confirmed`

Login-form flow:

- render: `0x2C96 -> login_form_render`
- touch dispatch: `0x2BE0 -> login_form_handle_touch`
- button/selection action: `0x2B1E -> login_form_handle_action`
- submit: `0x1E40 -> login_form_submit`
- request builder: `0x1B9C -> net_build_login_request`
- primary response handler: `0x16DC -> net_handle_login_response`

### Login Request

Status: `confirmed`

Account/password submit path:

`login_form_submit -> net_build_login_request(1, 1, 6)`

Request fields emitted by `net_build_login_request`:

- `coreVer`
- `appVer`
- `userName`
- `password`
- `imsi`

Notes:

- `userName` is used on the direct account/password submit path.
- A nearby alternate path uses `username` instead of `userName`. Status: `confirmed` for the existence of both spellings in client code, `hypothesis` for requiring server compatibility with both until a real server trace or deeper handler analysis confirms it.

### Login Response Dispatch

Status: `confirmed`

Two wrappers feed login responses into `net_handle_login_response`:

- `0x2D80`
  - matches `packet[4] == 1 && packet[8] == 1`
  - calls `net_handle_login_response(packet, 1)`
  - this is the primary account/password login response path
- `0x2A50`
  - matches `packet[4] == 1 && packet[8] == 12`
  - calls `net_handle_login_response(packet, 0)`
  - this appears to be a related alternate login/enter flow

### Response Fields

Status: `confirmed`

Fields consumed by `net_handle_login_response`:

- `result`
- `serverinfo`
- `servernum`
- `newVer`
- `information`
- `username`
- `password`

Behavior:

- `result` is treated as an ASCII digit, not a numeric integer.
- `serverinfo/servernum/newVer` are parsed into the in-memory server list/state when present.
- for `result == '3'` or `result == '4'`, the handler also copies returned `information`, `username`, and `password` into login-related buffers.
- for `result == '2'`, the handler copies `information` into a message buffer and marks a local error state.

### Success Path

Status: `confirmed`

Primary success path after account/password submit:

1. `login_form_submit` copies current edit buffers into `mmorpg_LoginRecord`.
2. `net_build_login_request(1, 1, 6)` sends the login request.
3. response wrapper `0x2D80` validates `type=1, subcmd=1`.
4. `0x2D80` calls `net_handle_login_response(packet, 1)`.
5. if `result == '1'` and the "save default account" flag is set, `0x2D80` calls `0x1430` to persist the active login record.
6. `0x2D80` then calls `0x23C0(resultChar)`.
7. `0x23C0('1')` calls the generic next-step callback at `*(v1 + 10464)()`.

This callback is the current best high-confidence transition point for "login succeeded, continue into the next screen/state".

### Non-Success Result Routing

Status: `confirmed`

Post-handler routing from `0x23C0`:

- `result == '1'`
  - invoke generic success callback `*(v1 + 10464)()`
- `result == '2'`
  - show a client message via `sub_10C6(&unk_258C, 0)`
- `result == '5'`
  - show a client message via `sub_10C6(&unk_2580, 0)`
- `result == '6'`
  - show a client message via `sub_10C6(&unk_25A0, 0)`

### Alternate Success Path

Status: mixed

Confirmed alternate login-related response path:

1. wrapper `0x2A50` validates `type=1, subcmd=12`
2. it calls `net_handle_login_response(packet, 0)`
3. it then calls `0x19C2(resultChar)`
4. `0x19C2('1')` falls through to `0x1956('1')`
5. `0x1956('1')` dispatches to a stage-dependent callback:
   - if local stage flag `*(r9+8276)+357 == 1`, call callback stored at `*(v1 + 10800)`
   - if local stage flag `*(r9+8276)+357 == 4`, call callback stored at `*(v1 + 10804)`

Status notes:

- `confirmed`: wrapper checks, call chain, and callback dispatch exist as described
- `hypothesis`: the gameplay meaning of subcmd `12`, stage flag values, and the two callbacks still needs runtime confirmation

## Business Scene-Entry Notes

Business-network entry in the main CBE is `0x01012E4C`.

Status: mixed

Confirmed fields seen across mocked game responses:

- login/role:
  - `actorinfo`
  - `playerinfo`
- scene/map:
  - `scene`
  - `posinfo`
  - `npcnum`
  - `npcinfo`
- resource/update:
  - `version`
  - `start`
  - `totalsize`
  - `crc`
  - `data`
  - `result`
- other observed payloads:
  - `rolesinfo`
  - `roleinfo`
  - `iteminfo`
  - `giftinfo`
  - `battleinfo`

For current `type=2/3` game-entry responses, the mock needs:

- default response subtype `0x1A`
- `scene = "00蓬莱仙岛_01"` as GBK bytes
- `posinfo` carrying two `i16` coordinates, currently `120,120`

If actor resource offsets need deeper decoding later, prefer reverse-engineering the `actorinfo` layout from CBE `0x0100FA88` rather than forcing actor state by direct global writes.

Status notes:

- `confirmed`: the listed field names are observed in the client and current mock flow
- `confirmed`: current mock-driven entry requires subtype `0x1A`, GBK `scene`, and `posinfo = (120,120)` to progress
- `hypothesis`: exact semantics and full binary layout for `actorinfo`, `playerinfo`, `npcinfo`, `rolesinfo`, `roleinfo`, `iteminfo`, `giftinfo`, and `battleinfo` are not yet fully decoded

## Legacy Source

`docs/net_mock_protocol.md` is retained as a historical working note and source document. New durable protocol conclusions should be recorded here first, then the legacy note can be trimmed or cross-referenced as needed.
