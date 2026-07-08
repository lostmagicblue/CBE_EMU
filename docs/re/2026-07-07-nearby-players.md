# Nearby Players

## Problem

User-visible symptom:

- no other players were visible on the map;
- the social `周围玩家` list was empty.

The current mock explained that behavior:

- `1/2/10` actor-other requests were answered with `othernum=0`;
- `1/2/7` scene-control page requests were answered only with `2/7 { result, pgIdx }`,
  without the scene-channel role list payload consumed by the nearby-player page.

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

`JianghuOL.CBE:net_handle_scene_channel_dispatch(0x01039BB4)` dispatches scene
channel subtype `7` to `ParseRoleListData(0x01039430)`.

`ParseRoleListData` reads:

```text
roomid
rolenum
rolesinfo
```

The parser also consumes column names through the shared scene list helpers and
then copies four strings per row into the nearby-role page cache.

Inference from the parser and the previous mock behavior:
replying to `2/7` with only `result=1` advances the page state machine, but it
does not populate the actual nearby-player rows. The list data must still be
delivered through scene-channel subtype `30/7`.

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
WT object 1/30/7
  roomid = 1001
  colnum = 4
  colnames = ["name", "job", "level", "state"]
  rolenum = N
  rolesinfo =
    repeat N:
      role name
      job text
      level text
      state text
```

The current mock only appends `30/7` for page `pgIdx=0`, which is the nearby
page initialized by `InitSceneCtrlState`.

## Implementation

- Added a shared nearby-role seed model in `src/mock-server.c`.
- Seeds now prefer other online accounts whose active role is already in the
  same scene. When no live peer is available, the mock fills the remainder with
  deterministic fallback players (`Traveler01..03`).
- `1/2/10` now serializes those seeds through the confirmed
  `ParseSceneOtherNodeData` row format.
- `1/2/7` now keeps the existing page ack and appends `1/30/7` nearby-role data
  for page `0`.
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
- Do not answer every `2/7` page request with a `30/7` list blindly; keep it
  tied to the nearby-player page index that the current scene-control init path
  opens.

## Validation

- `make -j2` succeeded after the changes.
- Runtime validation is still needed in the emulator UI to confirm:
  - nearby actors appear after the normal `2/10` request;
  - the social nearby-player page shows the same mock rows through `30/7`.
