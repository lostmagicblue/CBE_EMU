# Teleport Stone Phase

## Status

Implemented server-side handling for the teleport-stone request family:

- `1/16/1`: list teleport destinations.
- `1/16/2`: confirm prompt only; save the selected destination server-side.
- `1/16/3`: consume the confirmation request and return the main-business
  scene-enter family (`resources + 30/1`) for the saved destination.
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
- `result == 2` opens the confirmation callback path.
- `result == 4` shows a hint.
- Any other result falls through to `sub_BCC()` and performs scene entry.

Current implementation follows the confirmation branch for `16/2`, but still
includes the selected target fields:

```text
result = 2 as typed-u8: 00 01 02
scene = selected target .sce
posinfo = tagged i16 x, tagged i16 y
exitid = selected exit id
```

Runtime on 2026-06-27 showed why this matters: returning `result=1` from
`16/2` made the client call `EnterSceneByMapName()` before the later `16/3`
confirm request, producing two visible loading sequences. The later
`16/3-scene-saved-target pending=1` trace still stuck on loading, so the
`16/2` fall-through scene-entry shortcut is treated as negative evidence, not a
valid protocol shortcut.

Re-check on 2026-06-27 identified the actual object-field getter used by
`mmGame:0x11CE`: object offset `0x4C` points to
`JianghuOL.CBE:0x01033C6C`, which returns `value[2]` for the matched field.
Therefore `result` must use the typed-u8 object encoding `00 01 xx`.
`raw-u32` (`00 00 00 xx`) and typed-u32 (`00 04 ...`) are both read back as
`0` by this getter. Returning a confirmation-only `16/2` object without
`scene/posinfo/exitid` will still crash if the field encoding is wrong, because
the branch falls through to `sub_BCC()` and tries to read missing `posinfo`.

Runtime crash evidence on 2026-06-27 then showed a second reason not to send a
bare result-only object. The response reached the client as:

```text
mock_teleport_stone_transfer subtype=2 ... response=16/2-confirm pending=0 confirm=1 resp=23
address unable access:4 ... pc=01033a42 lr=05183ba7
```

`JianghuOL.CBE:0x01033A42` is `stream_read_i16_be_tagged`; the crash state had a
null blob pointer. `mmGame:0x11CE` ignores `scene/posinfo/exitid` when
`result == 2`, but another synchronous reader can still require the target blob
shape during the same network callback. The server therefore returns
`16/2-confirm-target`: `result=2` plus the full target fields, while still
saving the target in the separate confirmation slot and not arming the actual
scene-change target until the follow-up `16/3` request.

### `1/16/3`

Parser evidence:

- `mmGameMstarWqvga.cbm:sub_11CE(0x000011CE)` subtype `3`
- requires `result == 2`, then calls `sub_BCC`.

Latest implementation for the teleport-stone path does not return a top-level
`1/16/3` object. It uses the confirmation target saved by `16/2`, records that
target for same-scene re-entry guarding, then answers the `16/3` request with a
single main-business scene-enter object:

```text
30/1 { scene=saved target .sce, posinfo=tagged i16 x/y }
```

Resource/task/actor-other data is intentionally left for the post-screen
follow-up request, matching the normal scene-change chain:

```text
30/1 -> same-screen ScreenInit -> 25/5+6/1+6/13+6/14+2/10 -> scene-task-subset
```

The runtime source name is still `builtin-teleport-stone-transfer`, but the
response kind is logged as:

```text
mock_teleport_stone_transfer subtype=3 ... response=scene-channel-enter-confirm-target ...
```

An empty `16/3` no-op avoided the crash but left the client on the loading
screen after:

```text
mock_teleport_stone_transfer subtype=3 ... response=16/3-noop pending=1 resp=11
```

The older two-scene-entry implementation needed to re-arm the scene-change
target serial before queueing the second response. Without that serial refresh,
the host screen manager suppressed the duplicate lifecycle request:

```text
screen_mgr same-suppressed caller=01018150 serial=1 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
```

A later attempt to answer the `16/3` request with a `16/2` scene-entry object
did pass the screen guard, but it caused an infinite reload loop:

```text
mock_scene_target_remember serial=22 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
mock_teleport_stone_transfer subtype=3 ... response=16/3-ack-via-16/2-scene pending=1 resp=82
screen_mgr same caller=01018150 ... serial=22 ...
```

The current implementation instead avoids creating a scene-change target during
`16/2`. It stores a separate teleport-stone confirmation target and logs:

```text
mock_teleport_stone_transfer subtype=2 ... response=16/2-confirm-target pending=0 confirm=1
mock_teleport_stone_transfer subtype=3 ... response=16/3-scene-confirm-target pending=0 confirm=1
```

Manual runtime on 2026-06-27 then reached the `16/3` response without crashing,
but no `mmGame` parser or scene follow-up log appeared after the normal queued
data event:

```text
mock_teleport_stone_transfer subtype=3 ... response=16/3-scene-confirm-target resp=81
net_send connect=2 wt=16/3 ... source=builtin-teleport-stone-transfer resp=81
net_queue_data connect=2 event=7 resp=81 ...
```

That older interpretation was incomplete. A later runtime with the direct
`30/1` response showed that reentrant flushing is also wrong for this phase:
it makes `ScreenInit` happen inside the `16/3` send call stack, and the fresh
screen can stall before emitting its post-enter follow-up. The `16/3` response
therefore stays on the normal async queue. The expected first diagnostic is:

```text
net_queue_data connect=2 event=7 resp=56 ...
```

The 2026-06-27 follow-up trace showed the event being consumed by the main
business callback, not by the mmGame transfer parser:

```text
net_fire ...
net_wrapper_state ...
net_wrapper_business ...
net_business_gate ...
net_done ...
```

IDA evidence in `JianghuOL.CBE`:

- `net_wrapper_event_dispatch(0x0103489A)` calls the net-manager callback
  first, then the main business callback at `0x01012E4D` when state is not `3`.
- `handle_version_update_response(0x01037473)` only scans kind `18`, so it
  ignores `16/3`.
- `net_business_response_dispatch(0x01012E4D)` parses the event packet, but its
  switch sends kind `16` to the default branch.
- `net_handle_scene_channel_dispatch(0x01039B8A)` handles kind `30`; subtype
  `1` calls `parse_scene_response_entry(0x010396D6)`, which reads `scene` and
  tagged `posinfo`.

Therefore a top-level `1/16/3` response can be syntactically valid and still
leave the client on the loading screen, because this callback path ignores
kind `16`. Returning `30/1` makes the response consumable by the observed
dispatcher while avoiding premature resource delivery into the old screen
instance.

After this direct scene-enter response, a narrow cleanup handler catches only
the pending teleport-stone flow's standalone `25/5` default-scene ack:

```text
mock_teleport_stone_direct_enter_ack scene=... pos=(...) keep_pending=1 response=25/5
```

This response does not clear the saved target. The saved target is cleared by
the later `scene-task-subset` completion path:

```text
mock_teleport_stone_direct_enter_followup scene=... pos=(...) response=scene-task-subset
```

If a duplicate `16/3` arrives after the real scene-enter response but before
the normal scene follow-up clears the pending target, it still returns
`16/3-duplicate-noop` and does not re-arm the scene target serial.

Negative evidence: `16/3` with `result` encoded as raw-u32 (`00 00 00 02`) is
read as `0` by `JianghuOL.CBE:0x01033C6C`, so `mmGame:0x11CE` skips
`sub_BCC()` and remains on the loading screen.

Do not re-parse `exitID` from the `16/3` request when a pending target exists.
The observed `16/3` request can contain bytes that make the generic field scan
mis-read an exit value such as `13633`; the correct exit id is the one selected
and saved during the `16/2` phase.

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
- In the first validated sample, `objid=22` matches the UI selection
  `HuoYanShan / YeHuoGu` and local SCE prefix `22HuoYanShan`; `curid=1`
  also matches the `_01.sce` landing scene.
- A later random-map validation produced `curid=20 objid=4` and correctly
  landed on `c04LinAnFu_01.sce`, so `curid` is only treated as a preferred
  exact scene index, not as an authoritative sub-scene id.

Response strategy:

- return a `1/16/2` object instead of a same-subtype `16/4` object;
- reuse the already validated `result + scene + posinfo + exitid` scene-enter
  contract consumed by `mmGame:0x11CE` / `mmBattle:0x1868`;
- implemented mapping:

```text
objid=NN curid=M -> first existing JHOnlineData/[c]NN..._%02M.sce
fallback -> JHOnlineData/[c]NN..._01.sce, then any [c]NN*.sce, then default scene
validated: curid=1 objid=22 -> 22HuoYanShan_01.sce @ (120,120)
validated: curid=20 objid=4 -> c04LinAnFu_01.sce @ (120,120), then normal local scene flow
```

The runtime scan intentionally excludes `b_*.sce` regional-map index scenes.
Town-style resources such as `c04LinAnFu_01.sce`, `c08YanMenGuan_01.sce`,
`c14ShuShan_01.sce`, `c18DaLi_01.sce`, and `c24XiaKeDao_01.sce` are accepted
through the `cNN` prefix and normalized by the existing scene-enter helper when
needed.

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
mock_teleport_stone_transfer subtype=2 ... response=16/2-confirm-target pending=0 confirm=1
mock_teleport_stone_transfer subtype=3 ... response=scene-channel-enter-confirm-target pending=0 confirm=1
mock_teleport_stone_direct_enter_followup scene=... pos=(...) objects=... response=scene-task-subset
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
`22HuoYanShan_01.sce @ (120,120)` without unhandled packets. The handler now
uses the same `curid/objid` packet shape for any local map group that has a
matching SCE resource under `JHOnlineData`.

## Unknowns

- The third `exitinfo` per-row `u32` is stored by the client but has not been
  semantically named yet. It is currently emitted as `0`.
- Full UI validation should confirm whether the game sends `16/3` in other
  teleport-stone screen states. The validated path currently sends `16/2`
  followed by `27/11 + 12/1 + 7/42`.
- `curid` meaning is still partly unknown. It is useful as an exact-scene hint
  when a matching `[c]NN..._%02curid.sce` exists, but at least one validated
  request (`curid=20 objid=4`) requires falling back to the first local scene.
- The scan maps by local resource naming, not by an authoritative server table.
  If a live server later proves a different first landing scene for a specific
  `objid`, add a narrow override before the scan.
