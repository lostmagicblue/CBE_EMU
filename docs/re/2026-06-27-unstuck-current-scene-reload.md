# 2026-06-27 Unstuck Current Scene Reload

## Symptom

After clicking the menu item `脱离卡死`, the client recreates the map loading
screen but stays on `正在加载中...`.

No fresh `bin/logs/net_trace.log` or `bin/logs/net_packets.log` was present for
this report. The current fix therefore uses:

- user screenshot: loading overlay after clicking `脱离卡死`
- IDA evidence from `mmGameMstarWqvga.cbm`
- existing scene-enter parser evidence from `江湖OL.CBE`
- the already documented scene-loading failure mode around default `25/5`
  responses as a fallback only

## IDA Evidence

`mmGameMstarWqvga.cbm` contains the settings/menu string group:

```text
0x5B28 拾取设置
0x5B34 复活设置
0x5B40 脱离卡死
0x5B4C 音乐设置
0x5B58 返回标题
```

`mmGameMstarWqvga.cbm:0x5BCA` is the settings menu input handler. On confirm it
uses the selected list node at `r9+10668`. If the confirmation selection is yes
and the row is server-backed, it calls the module request builder with:

```text
request builder args: 5, 0, 12, 3
field: id = *(u32 *)selected_row
```

`mmGameMstarWqvga.cbm:0x11CE` is the response handler used by this menu module.
For scene transfer it does not consume response object `12/3` directly. It loops
response objects and calls `sub_BCC()` only for object kind `16`, subtype `3`,
when `result == 2`.

`mmGameMstarWqvga.cbm:0x0BCC` (`sub_BCC`) reads response fields:

```text
scene
posinfo
exitid
```

It then calls the mmGame scene-entry vtable with `(scene, scene_len, x, y,
exitid)` and clears the waiting state.

`江湖OL.CBE:0x010396D6` (`scene_handle_enter_with_scene_pos`) reads the `scene`
string and tagged `posinfo` stream from scene-enter objects. This remains the
normal parser path for `30/1` scene enter and current-map reload.

`江湖OL.CBE:0x01006204` loads local SCE data after the scene-enter path gets a
valid map name and position.

## Request Signature

Primary accepted request shape:

```text
WT */* object: 0/12/3 { id = u32 }
WT */* object: 1/12/3 { id = u32 }
```

The `major == 0` form follows the `0x5BCA` builder arguments. The `major == 1`
form is accepted as a conservative compatibility form because most mock-server
objects use major `1`, while the client response handler here only checks
kind/subtype.

The detector scans all WT objects in the request and matches when any object has
kind `12`, subtype `3`, and an `id` field marker. This avoids missing the
settings request if the client sends it with extra synchronization objects in
the same packet.

The older reload fallback remains intentionally narrow and only upgrades default
scene event requests when runtime state shows a loading scene without an active
scene-change target.

Fallback trigger shapes:

```text
WT */* object: 1/25/5, empty payload
WT */* objects: 1/2/10 { Type = 1 }, 1/25/5 empty
```

Additional runtime gates:

- `g_vm_net_mock_last_scene_change_target_valid == false`
- `Global_R9 + 0x54AC` scene object exists
- `Global_R9 + 0x5C6B` scene pending flag is nonzero
- same scene was not just completed by normal scene-change
- same scene was not just answered by this reload handler

This avoids turning normal idle `25/5` events into repeated `30/1` scene enters.

## Response Contract

Primary source name:

```text
builtin-settings-unstuck
```

Primary response shape, phase 1:

```text
12/3 {
  result = 1 as typed-u8: 00 01 01,
  id = request id as typed-u16: 00 02 xx xx
}
16/3 {
  result = 2 as typed-u8: 00 01 02,
  scene,
  posinfo,
  exitid
}
```

The first object satisfies `mmGameMstarWqvga.cbm:0x6512`, which clears the
settings/menu busy flag at `unk_2844 + 0x10` and request pointer at
`unk_2844 + 0x0C`. This is required before the scene-transfer side effect,
because the request being answered is still a settings `12/3` request.

The `id` object is encoded as typed-u16 because `sub_6512` reads it through the
object getter at offset `0x44`, the same getter used by `sub_BCC()` for
`exitid`.

The scene-transfer `result` is not a raw integer field. Re-check on 2026-06-27 found that
`mmGame:0x11CE` reads object offset `0x4C`, whose target is
`JianghuOL.CBE:0x01033C6C` (`LookupItemByteField`). That getter returns
`value[2]`, so only the typed-u8 object value `00 01 02` compares equal to `2`.
The raw-u32 value `00 00 00 02` is read as `0`, causing the client to skip
`sub_BCC()` and stay on the loading screen.

Primary response shape, phase 2 when the scene screen asks for default loading
data (`25/5` or `2/10 + 25/5`) while the mmGame scene-transfer target is still
pending:

```text
12/1 skill summary
7/42 books
17/1 backpack items
6/1 empty taskinfo
6/13 empty task types
6/14 empty task actions
2/10 actor-other empty rows
25/5 { result = 4 }
30/1 { scene, posinfo }
```

This matches `mmGameMstarWqvga.cbm:0x6512` for the original settings request
and `mmGameMstarWqvga.cbm:0x11CE`, which routes `16/3 result=2` into
`sub_BCC()`. The scene and position are the current scene and a safe adjusted
player grid.

Runtime smoke on 2026-06-27 accidentally hit the teleport-stone `16/3` path
instead of the settings path. It showed `16/3` alone moves the client into a
scene loading/parser state (`parserState=2`) but does not by itself produce the
resource/scene-enter family needed to leave the loading screen.

Manual runtime output later showed a `16/3 + resources + 30/1` single response
being built:

```text
[info][network] mock_mmgame_scene_transfer_combo scene=00_蓬莱仙岛01.sce pos=(223,382) objects=10 resp=429
```

but the client still remained on loading. This is negative evidence that the
extra Jianghu scene-resource objects were being delivered through the mmGame
module callback and were not consumed as a normal main scene-business response.
The implementation now sends phase 1 as a single `16/3` object, records a
pending target, and answers the next loading/default scene request with phase 2.

Manual runtime output after the two-phase split showed only the phase-1 line:

```text
[SCR_FUNC](init:5183e45,destory:5182e0f,logic:5182d0b,render:518251b,pause:51823e1,remuse:5182375,resLoad:0)
ScreenInit Ok
[SCR_FUNC](init:5183e45,destory:5182e0f,logic:5182d0b,render:518251b,pause:51823e1,remuse:5182375,resLoad:0)
ScreenInit Ok
[info][network] mock_mmgame_scene_transfer_start scene=00_蓬莱仙岛01.sce pos=(223,382) resp=81
```

Later runtime output added:

```text
[info][screen] screen_mgr same caller=01018150 screen=01053f78 serial=1 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
```

That means `EnterSceneByMapName()` successfully asked the host screen manager to
re-enter the main scene. The next `SCR_FUNC` was still the mmGame screen,
therefore the stall was no longer explained by a missing same-screen lifecycle
accept. The remaining mismatch was the host screen stack removal path: mmGame's
post-response callbacks can pass a screen owner/object pointer or an interior
screen-table pointer, but `vm_screen_stack_remove()` only matched exact screen
table addresses.

The `0x05183e45/0x05182d0b/0x0518251b` screen functions map to
`mmGameMstarWqvga.cbm` offsets `0x3E45/0x2D0B/0x251B`, so the client is still
inside the mmGame module around the transfer UI. Because no phase-2 console
line followed, the follow-up detector was too strict for this runtime. It now
matches any pending mmGame scene-transfer target whose request contains
`1/25/5`, instead of only the exact `25/5` or `2/10 + 25/5` shapes.

The teleport-stone post-enter detector was also relaxed while still gated by a
pending scene-transfer target. It now accepts packets containing the stable
object family `27/11 + 12/1 + 7/42`, even when the client sends extra objects
or changes their order.

Fallback source name:

```text
builtin-scene-current-reload
```

Fallback response shape:

```text
12/1 skill summary
7/42 books
17/1 backpack items
6/1 empty taskinfo
6/13 empty task types
6/14 empty task actions
2/10 actor-other empty rows
25/5 { result = 4 }
30/1 { scene, posinfo }
```

The `30/1` scene comes from the current runtime/active role state. The target
position must not reuse the current player coordinate, because the menu is
usually clicked after that coordinate has already become bad. The server now
uses the current coordinate only as a reference point, then selects the nearest
non-zero SCE edge-entry spawn in the same scene and runs it through the normal
safe-landing adjustment. For interior-only scenes without edge entries, the
secondary target is the SCE-derived scene center, not a hand-written map
constant or a JSON export.

## Implementation

`src/mock-server.c` adds:

- `vm_net_mock_is_settings_unstuck_request()`
- `vm_net_mock_get_current_scene_unstuck_target()`
- `vm_net_mock_append_mmgame_scene_transfer_object()`
- `vm_net_mock_append_settings_unstuck_ack_object()`
- `vm_net_mock_build_mmgame_scene_transfer_start_response()`
- `vm_net_mock_build_mmgame_scene_transfer_followup_response()`
- `vm_net_mock_build_settings_unstuck_response()`
- `vm_net_mock_scene_runtime_pending_without_target()`
- `vm_net_mock_is_short_scene_default_event_request()`
- `vm_net_mock_build_current_scene_reload_response()`
- a small per-scene recent-reload guard

`src/main.c` adds read-only console diagnostics for `EnterSceneByMapName`
same-screen re-entry:

```text
[info][screen] screen_mgr same caller=01018150 serial=...
[info][screen] screen_mgr same-suppressed caller=01018150 serial=...
```

These diagnostics do not change client memory. They only expose whether the
host screen manager accepted or suppressed the same-scene lifecycle request
that the client made after parsing `16/3`.

The same-screen re-entry guard now stores the scene-change target serial instead
of only remembering the scene name. This matters because "脱离卡死" and
teleport-stone destinations can legitimately re-enter the same `.sce` in a new
request cycle. Matching only the scene name can suppress the first
`EnterSceneByMapName()` call of the new cycle, preventing the client from
emitting the follow-up loading request at all.

`src/main.c` now also normalizes screen-stack lookup/removal:

- exact screen function table pointer;
- owner/`this` pointer immediately before the table (`ptr + 0x18 == table`);
- interior table pointers used by some client callbacks.

This keeps the emulator's screen stack compatible with the firmware behavior
documented earlier for contains/remove-style queries. When mmGame's
post-`16/3` callback asks to close itself using a related pointer, the host can
remove the current mmGame screen and resume the main scene instead of
initializing mmGame again.

The teleport-stone path then exposed a protocol-order bug:

```text
old shortcut:
16/2 -> result=1 -> EnterSceneByMapName()
16/3 -> result=2 -> EnterSceneByMapName() again
```

`mmGame:0x11CE` offers a `16/2 result=2` confirmation callback path, but only
when `result` is encoded as typed-u8 (`00 01 02`). Tagged-u32 and raw-u32 are
not valid for this exact compare. If a confirmation-only packet uses the wrong
encoding, the parser falls through to `sub_BCC()` and crashes at
`JianghuOL:0x01033A42` (`stream_read_i16_be_tagged`) while reading missing
`posinfo`.

The correct implementation therefore makes `16/2` a confirmation-only response
with typed-u8 `result=2`, stores the selected target in a separate
teleport-stone confirmation slot, and does not record a scene-change target at
this stage. A later empty-object response to a subsequent pending-target `16/3`
avoided the `posinfo` crash, but left the client on the loading screen:

```text
mock_teleport_stone_transfer subtype=3 ... response=16/3-noop pending=1 resp=11
```

That is consistent with `mmGame:0x11CE`: the `16/3` branch only advances when
`result == 2`. The pending-target `16/3` response therefore needs to be a real
`16/3` scene-entry object using the saved target.

One runtime trace showed that the second-stage scene-entry object was parsed,
but the host same-screen re-entry guard suppressed the lifecycle request:

```text
[info][screen] screen_mgr same-suppressed caller=01018150 serial=1 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
```

That guard is host bookkeeping, not client protocol. It became relevant only
because the old shortcut made both `16/2` and `16/3` call
`EnterSceneByMapName()` for the same target. The revised flow avoids that
duplicate scene entry.

A later attempt to answer the `16/3` request with a `16/2` scene-entry object
passed the screen guard, but caused an infinite reload loop:

```text
mock_scene_target_remember serial=22 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
mock_teleport_stone_transfer subtype=3 ... response=16/3-ack-via-16/2-scene pending=1 resp=82
screen_mgr same caller=01018150 ... serial=22 ...
```

The current implementation expects the normal trace to be:

```text
mock_teleport_stone_transfer subtype=2 ... response=16/2-confirm-target pending=0 confirm=1
mock_teleport_stone_transfer subtype=3 ... response=16/3-scene-confirm-target pending=0 confirm=1
```

Manual crash evidence later showed the result-only `16/2` object was too thin:

```text
mock_teleport_stone_transfer subtype=2 ... response=16/2-confirm pending=0 confirm=1 resp=23
pc=01033a42 lr=05183ba7
```

`01033A42` is the shared tagged-i16 stream reader. The `mmGame:0x11CE` subtype-2
confirmation branch only needs `result == 2`, but a synchronous reader can still
touch the target blob shape during the same callback. The current subtype-2
response therefore includes `result=2` plus `scene/posinfo/exitid`; it still does
not call `vm_net_mock_remember_scene_change_target()` until the later `16/3`
scene-enter response.

Manual runtime then reached the `16/3` scene-enter response and queued the
normal data event, but stayed on loading with no visible parser/follow-up log:

```text
mock_scene_target_remember serial=1 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
mock_teleport_stone_transfer subtype=3 ... response=16/3-scene-confirm-target resp=81
net_queue_data connect=2 event=7 resp=81 ...
```

This is treated as event-order evidence. The mock now immediately flushes only
the `builtin-teleport-stone-transfer` `16/3` data event after queueing it. The
expected diagnostic is:

```text
net_flush_data connect=2 wt=16/3 resp=81 err=0
mmgame_transfer_result pc=05181250 subtype=3 result=2 ...
```

Only the `16/3` stage records a scene-change target. If a duplicate `16/3`
arrives before the normal scene follow-up clears the pending target, it returns
`16/3-duplicate-noop` and does not re-arm the scene target serial.

The 2026-06-27 stuck-loading regression was caused by temporarily changing
`result` to raw-u32 in `vm_net_mock_put_teleport_stone_scene_fields()`. The
server now uses `vm_net_mock_put_object_u8()` again for both `16/2` and `16/3`.

Dispatch order places `builtin-settings-unstuck` before rule matching. It places
`builtin-mmgame-scene-transfer-followup` before the old generic default-event
paths. `builtin-scene-current-reload` remains a fallback before
`builtin-scene-default-event` and
`builtin-actor-other-scene-default-combo`.

## Validation

Source compile passed:

```text
gcc -g -w -c src/main.c -o obj/main.codex-unstuck2.o
make
git diff --check
```

`make` completed successfully and rebuilt `bin/main.exe`. Later rebuilds after
the detector relaxation, scene-change target serial fix, and screen-stack
removal normalization also passed. A short `--autotest` startup smoke exited
normally with no assert and showed normalized screen removal:

```text
[info][screen] screen_mgr remove requested=0105a518 current=0105a518 result=1 current_match=1 new_top=01056204 module=00000000
```

`git diff --check` reported only Git CRLF conversion warnings for `src/main.c`
and `src/mock-server.c`; no whitespace errors were reported.

The 2026-06-27 smoke script reached the map, but the guessed key path did not
open the settings/unstuck menu. It instead hit the teleport-stone `16/2` and
`16/3` path, then remained in scene loading with `parserState=2`. That run is
not used as an end-to-end settings validation, but it is used as supporting
runtime evidence that an mmGame scene-transfer object alone is insufficient for
this loading-screen failure mode.

End-to-end validation should first confirm the primary trace lines:

```text
net_send ... source=builtin-settings-unstuck ...
mock_settings_unstuck id=... scene=... pos=(...) response=12/3+16/3
net_send ... source=builtin-mmgame-scene-transfer-followup ...
mock_mmgame_scene_transfer_followup scene=... pos=(...) response=resources+30/1
```

## 2026-06-30 Compact 16/2 Unstuck / Kubao Prompt

Manual runtime after a bad portal landing showed:

```text
net_send connect=2 wt=16/2 len=19 source=builtin-teleport-stone-transfer resp=78
```

The visible client prompt was "酷宝不足，需要充值". IDA evidence from
`mmGameMstarWqvga.cbm:sub_11CE(0x11CE)` explains the branch:

- `16/2 result == 2` opens the confirmation/payment callback path
  (`sub_24A8`) with the local "酷宝不足/充值" text.
- `16/2 result == 4` shows the `hint` text field.
- Other `16/2` result values fall through to `sub_BCC(0x0BCC)`, which reads
  `scene`, `posinfo`, and `exitid` and calls the mmGame scene-entry vtable.
- `16/3 result == 2` is still the validated scene-entry path for the primary
  settings request (`12/3 + 16/3`).

The mock-server's teleport-stone detector accepted any `16/2` packet containing
`type`, so this compact unstuck packet was answered as teleport-stone
confirmation (`result=2`), triggering the Kubao prompt. A new narrow handler
`builtin-settings-unstuck-16-2` now catches only compact `16/2` requests that:

- contain `type`;
- do not contain `exitID`, `exitid`, `scene`, or `posinfo`;
- did not arrive immediately after a `16/1` teleport-stone exitinfo list.

It returns `16/2 result=1 + scene/posinfo/exitid`, using the current-scene
unstuck target, so mmGame enters through `sub_BCC` instead of the payment prompt.

## 2026-06-30 Unstuck Landing Point

Manual runtime after a bad repeat scene-enter showed the compact `16/2`
unstuck path was handled but did not move the character:

```text
mock_scene_target_remember serial=4 scene=00..._02.sce pos=(8,14) exit=0
mock_settings_unstuck_16_2 scene=00..._02.sce pos=(8,14) response=16/2-direct-enter
screen_mgr same ... scene=00..._02.sce pos=(8,14) exit=0
```

Root cause: `vm_net_mock_get_current_scene_unstuck_target()` used the current
runtime grid as both the source coordinate and the destination coordinate. When
the player is already outside the map, this re-enters the same bad point and
then saves it back to the active role state.

Fix:

- keep the current grid only as the distance reference;
- load the current `.sce` through the existing server-side SCE/LZSS reader;
- choose the nearest non-zero `edge_portal.spawn` as the unstuck destination;
- adjust that destination away from portal trigger rectangles with the existing
  `vm_net_mock_adjust_safe_player_pos_for_scene()` helper;
- print the chosen source as `mock_unstuck_target ... source=sce-nearest-entry`.

If a scene has no edge entries, the compact unstuck path uses the SCE file's own
dimensions to choose `source=sce-center`. This is scoped to the explicit
unstuck feature and is not used by portal or teleport destination resolution.

## 2026-06-30 Unstuck Follow-Up Reentry

Manual runtime after the SCE-derived unstuck target showed the position moved to
the intended unstuck point, but loading restarted several times:

```text
mock_scene_target_inherit_pending scene=00..._02.sce pos=(256,256) exit=0
net_send connect=2 wt=2/3 len=74 source=builtin-scene-change resp=428
ScreenInit Ok
mock_scene_change_post_enter_followup scene=00..._02.sce pos=(256,256) objects=6 resp=259
ScreenInit Ok
```

This sequence is not correct. The compact `16/2 result=1 + scene/posinfo`
unstuck response is already a direct scene-enter contract consumed by
`mmGame:sub_BCC(0x0BCC)`, which calls the scene-enter vtable with the supplied
`scene`, `posinfo`, and `exitid`. The later `WT 2/3` and `25/5` packets are
follow-up/completion traffic for the already-entered same scene; they must not
send another `30/1` or `30/2` scene-position object.

Fix:

- settings unstuck responses now call
  `vm_net_mock_mark_direct_scene_enter_completed()`: this still allocates a new
  scene-change target serial for the host same-screen guard, but immediately
  marks the target completed and clears pending state;
- `vm_net_mock_build_scene_change_combo_response()` skips full scene bootstrap
  when the resolved target is the recent completed direct-enter target;
- `vm_net_mock_build_scene_change_post_enter_followup_response()` returns a
  post-enter repeat ack for recent completed targets:
  `25/5 result + 30/2 ack without posinfo + empty 27/11 + 7/42`, with no
  `27/12` and no `30/2 scene+posinfo`.

Expected trace:

```text
mock_scene_target_direct_completed scene=... pos=(...) reason=settings-unstuck-16-2-target
mock_scene_target_inherit_completed scene=... pos=(...) exit=0
mock_scene_change_completed_repeat_ack scene=...
mock_scene_change_post_enter_repeat_ack scene=...
```

There should be no full-bootstrap `builtin-scene-change resp=428` and no
post-enter `objects=6` response for the same completed unstuck target.

For teleport-stone or map-transfer paths, the expected second-stage line may be:

```text
mock_teleport_stone_post_enter scene=... pos=(...) objects=4 resp=...
```

## 2026-06-27 Teleport-Stone Dispatcher Correction

Latest runtime for the teleport-stone `16/3` response showed that the queued
data event is consumed by `JianghuOL.CBE`'s main business wrapper:

```text
net_wrapper_state ... cb=01037473
net_wrapper_business ... cb=01012e4d
net_business_gate ... scene_ready=1
net_done ... remaining_read=0/81
```

The absence of `net_read_data` is not itself a failure on this path; the
response pointer is passed directly as event `r0`.

IDA correction:

- `0x01037473` only scans kind `18` update/resource entries.
- `0x01012E4D` parses the packet but routes top-level kind `16` to the default
  ignored branch.
- `0x01039B8A` handles top-level kind `30`, and subtype `1` reaches
  `0x010396D6`, which reads `scene` and tagged `posinfo`.

So a syntactically valid top-level `1/16/3` response can still leave the client
on the loading screen. The current server response for teleport-stone `16/3`
therefore skips the top-level `16/3` object and returns the main-business
`30/1` scene-enter object directly, while keeping the saved target armed for
same-scene re-entry guarding and the post-screen follow-up. Resource/task data
is intentionally delayed until the normal post-enter `scene-task-subset`
request; sending resources before `30/1` can deliver them into the old screen
instance. A standalone pending `25/5` after that direct enter is handled by
`builtin-teleport-stone-direct-enter-ack`, which returns the normal default ack
but keeps the saved target pending. The later `scene-task-subset` completion
clears the target and logs `mock_teleport_stone_direct_enter_followup`.
The direct `30/1` response is queued normally; it is not reentrantly flushed
from inside the `16/3` send stack, because that ordering can initialize the new
screen too early and suppress the post-enter follow-up request.

If the client still reaches the fallback, traces should show:

```text
net_send ... source=builtin-scene-current-reload ...
mock_scene_current_reload scene=... pos=(...) trigger=...
```
