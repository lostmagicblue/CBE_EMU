# 2026-06-26 Title Role Select Active Role

## Problem

Selecting a role from the title role list could still enter the map with role
information from a different local role.

## IDA Evidence

`mmTitleMstarWqvga.cbm:0x39FC` selects a non-create role row and copies that
row's actor ID into the login record workspace before sending:

```text
net_build_login_request(1, 6, 1)
```

`mmTitleMstarWqvga.cbm:0x1B9C` handles subtype `6` by serializing the selected
actor ID through the same actor-ID field writer used by role deletion.

`mmTitleMstarWqvga.cbm:0x53EC` handles subtype `6` as the selected-role
continuation. The mock response also includes the scene/login actorinfo object,
which the main CBE scene bootstrap consumes to seed the visible map role.

## Fix

The mock now parses `1/1/6` through a dedicated title role-select parser:

```text
WT header kind/subtype = 1/6
exactly one object
object = 1/1/6
actorID / actorid / actorId: u32
```

The parser uses the shared object field helpers, which now accept both typed
object values and the direct value shape seen in client-built packets. IDA
evidence from `net_build_login_request(1, 6, 1)` also shows the selected actor
ID is passed through a callback-style writer rather than the normal
`(fieldName, value)` writer used by `serverID`, so the parser also scans the
small `1/1/6` payload for a big-endian u32 that matches an existing local role
ID. This keeps the detector narrow to the title select object and known DB rows.
The same helper path also supports client string fields with an optional
leading zero before the length word, so newly created role names are persisted
into the role DB instead of falling back to the default local name.

After parsing, the mock switches `activeRoleId` before building the subtype-6
actorinfo response. The scene actorinfo string fields now default to the active
role name for:

```text
roleName
displayName
shortLabel
```

The actor motion-resource generator also uses the active role job/sex as its
default, so the map sprite resource follows the selected title role unless
explicit `CBE_ACTOR_*` overrides are set.

The title role-list compact `actorinfo` is intentionally not the same layout as
scene actorinfo. `mmTitle:0x3544` reads each row as:

```text
actorId, jobIndex(0..2), sex(1..2), name, level
```

and stores the bytes into the same row layout used by create-success
`mmTitle:0x5324`: row+325 receives `jobIndex`, row+324 receives `sex`. The mock
therefore emits `role->job - 1` for title rows, but keeps scene actorinfo as
`visualVariant=role->job-1` and `visualGroup=role->sex+1`.

Autotest log line:

```text
mock_title_role_select requested=... selected=... active=... name_len=... actorinfo_len=... response=1/1/6+1/1/15 evidence=mmTitle:0x39FC/0x1B9C/0x53EC
```

For the preceding create path, the related check is:

```text
mock_title_role_create result=... actorid=... decoded=1 raw_sex=... raw_job=... job_index=... roles=... name_len=...
```

`decoded=0` means the title client can still insert the new row locally, but the
server-side persisted role will fall back to default job/sex and a stable unique
name such as `Role10002`. Duplicate default-name rows left by older decoder
failures are repaired on role DB load, while newly created rows should keep the
decoded title input.
