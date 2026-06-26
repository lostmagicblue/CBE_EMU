# 2026-06-26 Title Role Create

## Runtime Trigger

Creating a role from the title role-management screen emitted an unhandled
request:

```text
[error][network] unhandled wt=1/7 len=34 objects=1 first=1/1/7:25 last_source=- last_resp=0
[error][network] unhandled wt=1/7 len=35 objects=1 first=1/1/7:26 last_source=- last_resp=0
```

This is a single title-side WT object `1/1/7`. The request payload is produced
from the currently selected sex/job and the name edit buffer.

## IDA Evidence

`mmTitleMstarWqvga.cbm:0x3E66` (`role_manage_submit_selected_role`) creates a
network object with:

```text
major = 1
kind = 1
subtype = 7
```

It serializes three values in order:

```text
selected sex
selected job
entered role name
```

The caller at `mmTitleMstarWqvga.cbm:0x4016` passes the title screen's current
selection and name buffer. The job argument is submitted as the selected index
plus one.

`mmTitleMstarWqvga.cbm:0x5324`
(`role_manage_screen_handle_create_role_result`) handles the response:

```text
actorid = object["actorid"]
result  = object["result"]
```

`result == 0` is the success path. On success, the client:

- increments the local title role-list count
- inserts a new role row before the create-role sentinel row
- writes `actorid` into the new row
- copies the current title UI sex/job/name values from local screen state
- writes level/status `1`
- returns to the role-list view

Nonzero result codes stay on the create screen and show client-side error
messages.

## Mock Contract

The mock-server now handles this request with source:

```text
builtin-title-role-create
```

Response:

```text
WT object 1/1/7
  actorid: u32
  result:  u8
```

Successful creation persists a new row in `nvram/jhol_mock_roles.bin`:

```text
roleId = next local role id
name   = submitted name bytes
job    = submitted title job, clamped to 1..3
sex    = submitted title sex converted from 1..2 to local 0..1
level  = 1
exp    = 0
hp/mp/money/backpack/scene/position = normal defaults
```

The new role becomes the active role so selecting/entering it after creation
uses the same role state used by login, scene actorinfo, position memory, money,
and battle settlement.

If the local role DB is full, the mock still returns a parser-safe `1/1/7`
response with `result=1` and `actorid=0`, avoiding an unhandled packet assert
while letting the title client remain on its failure branch.

## Implementation Notes

The detector is intentionally narrow:

- WT header kind/subtype must be `1/7`
- exactly one request object must parse
- object must be `1/1/7`
- payload length must stay in the small title-create range
- field decoding is best-effort. The client response parser only needs
  `actorid/result`; local DB creation uses decoded sex/job/name when available,
  otherwise falls back to default job/sex/name.

The request parser supports three observed or plausible serializer shapes:

- named object fields such as `sex/job/name`
- named positional fields with typed values
- the raw `v10, v10, v9` stream produced by `mmTitle:0x3E66`, i.e. two numeric
  values followed by a string blob

Autotest log line:

```text
mock_title_role_create result=... actorid=... raw_sex=... raw_job=... roles=... response=1/1/7 evidence=mmTitle:0x3E66/0x5324 runtime=wt1/7-len34
```
