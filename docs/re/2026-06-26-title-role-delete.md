# 2026-06-26 Title Role Delete

## Runtime Trigger

Deleting a role from the title role-management screen emitted an unhandled
request:

```text
unhandled wt=1/8 len=25 objects=1 first=1/1/8:16 last_source=- last_resp=0
```

This is a single title-side WT object `1/1/8`.

## IDA Evidence

`mmTitleMstarWqvga.cbm:0x1F90` builds the delete request:

```text
major = 1
kind = 1
subtype = 8
actorID = role_table[selected_index].actorId
```

The same `actorID` field helper is used by the title role-select request path.

`mmTitleMstarWqvga.cbm:0x53EC`
(`role_manage_screen_handle_network`) handles response subtype `8`:

```text
result = object["result"]
```

`result == 0` is the success path. On success, the client calls
`role_manage_screen_remove_role_slot()` using its current selected role slot and
updates the local title role list. Nonzero result shows a client-side failure
message and leaves the row in place.

## Mock Contract

The mock-server now handles this request with source:

```text
builtin-title-role-delete
```

Request detector:

```text
WT header kind/subtype = 1/8
exactly one object
object = 1/1/8
actorID or actorid: u32
```

Response:

```text
WT object 1/1/8
  result: u8
```

Successful deletion removes the matching role from `nvram/jhol_mock_roles.bin`,
shifts later rows down, and updates `activeRoleId` to the first remaining role.
If no roles remain, `activeRoleId` is set to `0` and the title role-list
actorinfo response emits count `0`; the title client then shows only the
create-role sentinel row.

If the requested actor ID is not present, the mock returns `result=1`, which is
parser-safe and leaves the client on the failure branch.

Autotest log line:

```text
mock_title_role_delete result=... actorid=... roles=... response=1/1/8 evidence=mmTitle:0x1F90/0x53EC runtime=wt1/8-len25
```
