# Nearby Players

## Problem

User-visible symptom:

- no other players were visible on the map;
- the social `周围玩家` list was empty.

The current mock explained that behavior:

- `1/2/10` actor-other requests were answered with `othernum=0`;
- `1/2/7` scene-control page requests were answered only with `2/7 { result, pgIdx }`,
  without the `othernum` / `otherinfo` fields consumed by the nearby-player page.

## IDA Evidence

### Scene-visible nearby actors

`JianghuOL.CBE:ParseSceneOtherNodeData(0x01012958)` handles top-level
`1/2/10` response objects.

It reads:

```text
othernum
otherinfo
```

Then for each `otherinfo` row it reads:

```text
u32 actorId
u8 visualVariant
u8 visualGroup
len16 labelText
u8 targetPosX
u8 targetPosY
len16 shortLabel
len16 longLabel
i16 gridPosX
i16 gridPosY
```

Each row is passed to `scene_node_find_or_create(...)`, so this is a normal
packet-driven path for spawning nearby scene actors.

`JianghuOL.CBE:net_handle_actor_move_info(0x01012ADC)` dispatches subtype `10`
to `ParseSceneOtherNodeData`.

### Nearby-player social list

`JianghuOL.CBE:InitSceneCtrlState(0x0103014C)` constructs a `1/2/7` request and
initializes field `pgIdx`.

`JianghuOL.CBE:HandleFactionOtherInfoResponse(0x01031162)` checks the returned
object for `kind=2, subtype=7`. It reads:

```text
allpgs
othernum
otherinfo
```

`allpgs` is read through the entry's `+0x4C` accessor
`LookupItemByteField(0x01034578)`, so its wire type is `u8`. A `u32` encoding
decodes as zero. On the page-next path `HandleFriendListInput(0x0103023C)`
passes this value to `BigIntDivide(0x0104D538)` as the divisor; zero raises the
client's divide-by-zero interrupt.

For every `otherinfo` row it consumes an actor ID, two `u8` values, a display
name, two more `u8` values, two strings, and two `i16` coordinates. It stores
the actor IDs, first two bytes and display names in the cache used by
`DrawFriendListUI(0x0103063C)`. `30/7 -> ParseRoleListData` is a separate
task-hall room table: it writes a different cache and cannot populate this
page.

## Response Contract

### Scene actor-other

The mock now answers `1/2/10 { Type=1 }` with nearby-role rows:

```text
WT object 1/2/10
  othernum = N
  otherinfo =
    repeat N:
      actorId
      visualVariant = job - 1
      visualGroup = sex + 1
      labelText = actor resource name
      targetPosX = x / 16
      targetPosY = y / 16
      shortLabel = role name
      longLabel = ""
      gridPosX = x
      gridPosY = y
```

This uses the same parser path that already creates scene nodes for other
actors.

### Nearby-player page

The mock now answers page-open `1/2/7` with:

```text
WT object 1/2/7
  result = 1
  pgIdx = request.pgIdx
  allpgs = u8(1)
  othernum = N
  otherinfo =
    repeat N:
      actorId
      job - 1
      sex + 1
      role name
      targetPosX = x / 16
      targetPosY = y / 16
      title text
      title badge
      x
      y
```

The row stream is the exact order consumed by
`HandleFactionOtherInfoResponse(0x01031162)`.

## Implementation

- Added a shared nearby-role seed model in `src/mock-server.c`.
- Seeds come from other online client sessions whose active role is already
  visible in the same scene. The current implementation does not invent
  fallback players when no live peer exists.
- `1/2/10` now serializes those seeds through the confirmed
  `ParseSceneOtherNodeData` row format.
- `1/2/7` now keeps the page ack and carries `allpgs`, `othernum`, and raw
  `otherinfo` in that same `2/7` object for page `0`.
- The old environment-gated NPC-only `2/10` path remains as a fallback when no
  nearby-role rows are emitted.

## Negative Runtime

The first nearby-player implementation used `vm_net_mock_put_object_blob()` for
nonempty `otherinfo`. That is wrong for this parser family.

`ParseSceneOtherNodeData(0x01012958)` fetches field `otherinfo` and passes the
returned pointer directly to `stream_reader_init_from_blob()`. A `blob` field
adds an extra `u16 dataLen` header before the actual row stream, so the reader
starts on the length prefix instead of the first actor id. The visible result
was:

- nearby actors still did not appear;
- later scene transitions could crash after the malformed actor-other parse had
  already poisoned nearby-node state.

The fix is to emit `otherinfo` as a raw/entry field with no extra inner length
wrapper.

## Negative Evidence

- Do not force nearby players by writing scene-node arrays or `Global_R9`
  actor-manager internals. Both map visibility and the social page have normal
  packet parsers.
- Do not overload title role-list `actorinfo` for this feature. Title role rows
  and scene nearby-role rows are different contracts.
- Do not send `30/7` for this UI: its valid parser belongs to the task-hall
  room list, not the surrounding-player cache.

## Validation

- `make -j2` succeeded after the changes.
- Dual-client runtime validation confirmed that nearby actors are created and
  refreshed through the normal `2/10` and `2/2` packet paths.

## 2026-07-10 Surrounding List and Friend List Split

### Correct protocol ownership

The initial handler treated the 37-byte `1/10/1` page request as another source
of surrounding players. That made every player in the current scene appear as a
friend. Further client analysis establishes that these are separate lists:

- `1/2/7 { pgIdx }` opens/pages the current-scene player list. Its response is
  a `2/7` object containing `allpgs`, `othernum`, and raw `otherinfo`.
- `1/10/1 { index, pageSize }` is the persistent friend list. Merely sharing a
  scene must not add a row.
- `1/10/3 { id }` is the invitation action reached from the surrounding-player
  menu. The mock currently auto-accepts this invitation and creates the
  relationship in both directions.

The address evidence is:

- `JianghuOL.CBE:InitSceneCtrlState(0x0103014C)` emits `2/7`.
- `JianghuOL.CBE:HandleFactionOtherInfoResponse(0x01031162)` consumes the
  `2/7` page table.
- `JianghuOL.CBE:SendPagedListReq(0x0101A5EE)` creates `10/1` and writes
  `index` as `u32` plus `pageSize` as `u8`.
- `JianghuOL.CBE:HandleFriendInfoResponse(0x0102FF54)` consumes `10/1` fields
  `allpgs` and raw `friendinfo`.
- The surrounding-player menu action calls `0x0101A2EA`, which constructs
  `10/3 { id }` and immediately displays the local GBK text `邀请已发送`.

### Friend row format

`friendinfo` starts with a typed `i16` count, followed by:

```text
u32 id
len16 string name
u8 state
u32 friendDegree
u8 attr8
```

`SortFriendListByOnline(0x0102FD86)` treats state `1` and `2` as the live-row
family. `HandleFriendResponse(0x0102157A)` matches `friendid` and adds `addedfd`
to the fourth value, proving that the `u32` is friend degree rather than the
role level. The final `u8` still has no recovered field name; the mock preserves
the prior job value there without claiming that semantic as confirmed.

The `10/1` handler now reads only persisted friend records. Before an invitation
it returns a valid empty list (`rowCount=0`) even if another player is visible in
the same scene. Online state and the current displayed name are refreshed from
the matching live session when available; otherwise the persisted snapshot is
returned with state zero.

Both `2/7` and `10/1` read `allpgs` through `LookupItemByteField`, so the mock
serializes this field as `u8` and caps the represented page count at `255`.

### Invitation handling and persistence

Friend records are stored in `nvram/mock_service_friends.bin` with `JHF1`
versioning. Each direction uses the composite key:

```text
(ownerAccountId, ownerRoleId) -> (targetAccountId, targetRoleId)
```

The account component is necessary because independent test accounts can both
own role ID `10001`. A successful invitation upserts both directions, is
idempotent, and persists the target name/job/sex/level snapshot plus friend
degree.

No unsupported `10/3` response object is invented. `0x0101A2EA` performs its
confirmation locally, while the confirmed friend dispatcher at `0x0102FF54`
handles `10/1`, `10/2`, and `10/24`, but not `10/3`. The mock therefore returns
a five-byte zero-object WT transport acknowledgement. The next real `10/1`
query exposes the auto-accepted relationship.

### Initial surrounding-player display

When page zero is opened before the client has uploaded movement,
`InitSceneCtrlState` is concrete evidence that the map UI exists. If no scene
transition is pending, the mock promotes the session from its saved role
scene/position. This allows an idle observer to populate `2/7.otherinfo` without guessing
a coordinate.

Page zero emits `othernum=0` with an empty raw `otherinfo` field when there are
no live peers. `HandleFactionOtherInfoResponse` clears the old cache before it
parses the row count, so this completes and clears the UI. The `pgIdx` decoder reads the confirmed `u8`
shape, with a `u32` compatibility fallback.

### Runtime validation

A server-only dual-session replay used existing `guest00019` and `guest00020`
roles in an isolated NVRAM directory. With a fresh friend database:

```text
2/7 request length:       20, response contains 2/7.otherinfo
10/1 request length:      37
friend rows before add:   guest00019=0, guest00020=0
10/3 request length:      20, response length=5 (empty WT ack)
friend rows after add:    guest00019=1, guest00020=1
```

After restarting the service against the same isolated NVRAM, both `10/1`
queries returned one row before another invitation was sent. Repeating `10/3`
kept the count at one, confirming persistence and idempotence. `make -j2`
succeeds.

### Stale server launcher failure

An earlier manual run still reported the 37-byte `10/1` assertion because the
assertion dialog named the stale `main - 副本.exe`, while current handlers and
build output live in `main.exe`. `bin/start-server.bat` now launches:

```text
main.exe --mock-service-only --mock-service-port=19090
```

The current handled sources are `builtin-scene-ctrl-page`,
`builtin-friend-page`, and `builtin-friend-add`. The service must be restarted
after rebuilding; an already running old executable cannot pick up the new
handlers.
