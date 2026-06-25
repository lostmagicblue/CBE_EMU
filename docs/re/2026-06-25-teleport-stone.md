# Teleport Stone Phase

## Status

Implemented server-side handling for the teleport-stone request family:

- `1/16/1`: list teleport destinations.
- `1/16/2`: return the selected destination scene.
- `1/16/3`: return the selected destination scene with the result value that
  lets `mmGame` continue into the same scene-enter path.
- `1/16/4`: map-UI instant transfer request from the teleport-stone world map.

Runtime validation has covered the crash point and the first transfer request:

- `16/1` is now handled by `builtin-teleport-stone-list`.
- `16/2` is now handled by `builtin-teleport-stone-transfer`.
- `16/4` is now handled by `builtin-teleport-stone-map-transfer`.
- the first post-enter combo after `16/2` was observed as
  `27/11 + 12/1 + 7/42`; a narrow completion handler was added for it.

Build validation passes with `make`.

## Request Evidence

User crash report:

```text
[error][network] unhandled wt=16/1 len=9 objects=1 first=1/16/1:0 last_source=- last_resp=0
Assertion failed!
```

The project log labels `wt` from the first object's kind/subtype. The packet is
an empty object:

```text
object: major=1 kind=16 subtype=1 payload_len=0
```

IDA evidence:

- `mmGameMstarWqvga.cbm:sub_8A8(0x000008A8)`
- branch `a1 == 14` calls the registered game-event allocator as
  `event(5, 1, 16, 1)`, producing the empty teleport-list request.
- later selection code sends `16/2` and `16/3` through `sub_194E`.

## Response Contract

### `1/16/1`

Parser evidence:

- `mmGameMstarWqvga.cbm:sub_11CE(0x000011CE)`
- object kind `0x10`, subtype `1`
- reads raw field `exitinfo`
- stream reader order:

```text
u8 count
repeat count:
  u32 exit_id
  len16 string label
  u32 reserved_or_extra
```

Implementation:

```text
exitinfo:
  count = 1
  exit_id = 1
  label = "Penglai Home"
  reserved_or_extra = 0
```

`exitinfo` must be a raw object field, not a length-wrapped blob. This matches
the existing `iteminfo`/`posinfo` stream fields.

### `1/16/2`

Parser evidence:

- `mmGameMstarWqvga.cbm:sub_11CE(0x000011CE)` subtype `2`
- if `result` is neither `2` nor `4`, calls `sub_BCC`
- `sub_BCC(0x00000BCC)` reads:

```text
scene
posinfo
exitid
```

- `mmBattleMstarWqvga.cbm:0x00001868` has the same subtype-2 scene contract.

Implementation returns:

```text
result = 1
scene = CBE_TELEPORT_STONE_SCENE or default scene
posinfo = tagged i16 x, tagged i16 y
exitid = selected exit id
```

### `1/16/3`

Parser evidence:

- `mmGameMstarWqvga.cbm:sub_11CE(0x000011CE)` subtype `3`
- requires `result == 2`, then calls `sub_BCC`.

Implementation returns the same scene fields as `16/2`, with:

```text
result = 2
```

### `1/16/4`

Runtime request evidence from the automated teleport-stone map path:

```text
WT 00 25 01  01/16/4 len=33
payload:
  curid = 1
  objid = 22
```

Raw packet:

```text
5754002501100400210563757269640006000400000001056f626a69640006000400000016
```

IDA evidence:

- `Jianghu OL.CBE:SendItemUseReq(0x0103573A)` allocates
  `event(5, 1, 16, 4)`.
- It writes `curid` and `objid` from the teleport-stone map list item.
- `objid=22` matches the UI selection `HuoYanShan / YeHuoGu` and local SCE
  prefix `22HuoYanShan`.

Response strategy:

- return a `1/16/2` object instead of a same-subtype `16/4` object;
- reuse the already validated `result + scene + posinfo + exitid` scene-enter
  contract consumed by `mmGame:0x11CE` / `mmBattle:0x1868`;
- current validated mapping:

```text
curid=1 objid=22 -> 22HuoYanShan_01.sce @ (120,120)
```

### Post-enter Combo

Runtime negative evidence after `16/2`:

```text
unhandled wt=27/11 len=19 objects=1 first=1/27/11:0,1/12/1:0,1/7/42:0
```

This combo appears after `EnterSceneByMapName` has already started the same
screen scene lifecycle. It is now handled only while a teleport-stone target is
pending. The response mirrors the existing current-scene completion family and
does not append a second `30/2` scene-position commit:

```text
27/12 target scene + posinfo
27/11 empty NPC info
27/4 ready/finalize
7/42 empty books
```

## Server Behavior

Handled sources:

```text
builtin-teleport-stone-list
builtin-teleport-stone-transfer
builtin-teleport-stone-map-transfer
builtin-teleport-stone-post-enter
```

Autotest notes:

```text
mock_teleport_stone_list entries=1 exitinfo_len=...
mock_teleport_stone_transfer subtype=... exit=... scene=... pos=(...)
mock_teleport_stone_map_transfer curid=... objid=... scene=... pos=(...)
mock_teleport_stone_post_enter scene=... pos=(...) objects=4
```

Default target:

```text
scene = vm_net_mock_default_scene_name()
x = 223
y = 382
exitid = 1
```

Runtime override knobs are intentionally data overrides, not feature switches:

```text
CBE_TELEPORT_STONE_SCENE
CBE_TELEPORT_STONE_X
CBE_TELEPORT_STONE_Y
CBE_TELEPORT_STONE_LABEL
CBE_TELEPORT_STONE_EXIT_ID
```

## Automation Path

Validated route:

```text
enter map
key:3
key:3
tap a world-map destination
tap bottom-left confirm
tap a regional-map destination when needed
tap bottom-left instant-transfer
confirm if a prompt appears
```

The current deterministic smoke path uses the HuoYanShan world node and reaches
`22HuoYanShan_01.sce @ (120,120)` without unhandled packets.

## Unknowns

- The third `exitinfo` per-row `u32` is stored by the client but has not been
  semantically named yet. It is currently emitted as `0`.
- Full UI validation should confirm whether the game sends `16/3` in other
  teleport-stone screen states. The validated path currently sends `16/2`
  followed by `27/11 + 12/1 + 7/42`.
- Only `objid=22` has been mapped to a concrete SCE so far. Other world-map
  destinations should be added as their `curid/objid` pairs are observed.
