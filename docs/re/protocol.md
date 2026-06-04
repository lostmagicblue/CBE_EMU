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

## Post-Scene `0x1B` Chain

### `0x1B/12` field `name`

Status: `confirmed`

`kind=0x1B subtype=12` is handled by `net_handle_fb_target_dispatch()` at `0x01010BC0`.

Confirmed flow:

- subtype `12` reads field `result`
- only when `result == 1`, it calls `ui_apply_named_posinfo_target_with_fb()`
- that wrapper stores field `fb` into active UI state and then forwards into `ui_apply_named_posinfo_target()`
- `ui_apply_named_posinfo_target()` reads server fields `name` and optional `posinfo`, copies `name` into active UI object state, and passes `name + posinfo` into UI manager method `+116`

This establishes that `0x1B/12.name` is a named-target / display-location semantic, not a resource filename semantic.

Additional confirmed request/response pairing:

- the current synthetic `0x1B/12` is only appended inside `mock_scene_change_combo_response()` when the incoming post-enter composite WT request contains object `1/0x1B/11`
- the triggering request is the observed `len=86` packet sent immediately after `trace_scene_send_map_enter_request`, with object sequence:
  - `1/0x0C/1`
  - `1/7/42`
  - `1/2/3(maptype,mapID,exitID)`
  - `1/0x1B/11`
  - `1/0x0C/1`
  - `1/7/42`
- this means the live `0x1B/12` should be treated as the server-side answer to the embedded `0x1B/11` subrequest inside the scene-enter follow-up packet, not as a direct answer to the `scene+posinfo` object itself

Additional confirmed buffer alias:

- `scene_send_map_enter_request()` (`0x0100F9B4`) serializes outgoing request field `mapID` from `*(_DWORD *)(R9+21676) + 1141`
- `ui_apply_named_posinfo_target()` (`0x0100E9B8`) writes incoming server field `name` from `0x1B/12` into that same `*(_DWORD *)(R9+21676) + 1141` buffer

This strongly suggests the correct `0x1B/12.name` is some map/scene/location text compatible with later `currentMapIdText` / `mapID` flow, not an actor or resource filename.

Additional confirmed producer / consumer chain for `uiState + 1141`:

- explicit writers:
  - `ui_apply_named_posinfo_target()` (`0x0100E9B8`) clears `uiState+1141` and copies incoming server field `name`
  - `scene_rebuild_runtime_nodes()` (`0x0100F7A6`) also clears `uiState+1141` and copies its `currentMapIdText` argument there, but only when its first argument `varg_r0_1` is nonzero
  - `net_handle_actor_move_info()` (`0x01012D9A`) case `12` also clears `uiState+1141` and copies incoming business field `name` there
- explicit readers / reusers:
  - `scene_send_map_enter_request()` (`0x0100F9B4`) reads `uiState+1141` as outgoing WT field `mapID`
  - `scene_rebuild_runtime_nodes()` reuses the same `currentMapIdText` string as the stacked extra argument into `sub_10352AE()`, which is the confirmed upstream source of the later `parse_actor_motion_descriptor()` misroute

Scene-enter branch behavior is now narrower:

- fresh scene-enter path inside `scene_runtime_init_and_sync()` (`0x010132A8 -> 0x01013586`) prepares a temporary text buffer at `R9+0x5E46`, validates it via `sub_100EEBC()` / `sub_100FA40()`, and then calls `scene_rebuild_runtime_nodes(varg_r0_1=1, ..., currentMapIdText=R9+0x5E46)`
- pending scene-switch / restore path inside the same function (`0x010131C4 -> 0x010131FE`) does **not** overwrite `uiState+1141`; instead it validates the existing `uiState+1141` contents with `sub_100FA40(uiState+1141)` and then calls `scene_rebuild_runtime_nodes(varg_r0_1=0, ...)`, which skips the internal copy into `uiState+1141`

This means the buffer is expected to persist as a scene/map text slot across restore/re-enter transitions. `0x1B/12.name` can therefore corrupt later map-enter and scene-rebuild behavior specifically because it overwrites a buffer that the restore path expects to remain a valid current-map/current-location text value.

Additional validator evidence for the fresh-enter text slot:

- `sub_100EEBC()` (`0x0100EEBC`), used by both the fresh-enter temporary buffer and the restore-path `uiState+1141` buffer, returns success when either:
  - a manager compare callback at `R9+0x4DA0 -> +0x48` accepts the input, or
  - the first byte of the input string is `'c'` (`0x63`)
- `sub_100FA40()` is the wrapper used at both callsites in `scene_runtime_init_and_sync()`
- runtime evidence already shows outgoing `mapID` strings such as `c00PenglaiXiandao_01`

This makes “internal scene/map identifier string” a stronger fit for the shared `+1141` / `R9+0x5E46` text slots than a natural-language map display name. It does not yet prove that every valid value must be `c...`, because the compare callback may also accept other forms, but the observed post-login path is now highly consistent with a c-prefixed scene id.

Related confirmed fallback behavior:

- `sub_100EEE0()` does **not** write `uiState+1141` itself
- instead, it first validates the current `uiState+1141` contents with `sub_100EEBC(uiState+1141)`
- only when that validation fails, it copies its caller-supplied `displayNamePtr` into a separate 4-entry fallback table at `R9+0x5CE4`
- current callers of `sub_100EEE0()` include:
  - `sub_100F094()` actor/scene-node parsing
  - `sub_10159DA()`
  - `scene_parse_npcinfo_and_spawn_npcs()` (`0x01037A9C`)

This reinforces that actor/NPC display names are treated as a fallback/auxiliary label source when the main current-scene text slot is invalid, not as the primary semantic of `uiState+1141`.

Supporting static evidence:

- `ui_apply_named_posinfo_target_with_fb()` at `0x0100EC66`
- `ui_apply_named_posinfo_target()` at `0x0100E9B8`
- the same helper is also reused by `net_handle_guild_hall_enter_response()` at `0x0103C224`, which further supports the interpretation that this is a generic named-target/location helper rather than a file-download API

### Later `type=6` request field `name`

Status: `confirmed`

The repeated `type=6` WT request is built by `send_update_chunk_request()` at `0x01036C66`, not by the startup version-check path.

Confirmed serialized fields there:

- `name`
- `screen`
- `type`
- `start`
- `version`

The startup version-check path is separate:

- `send_version_update_request()` at `0x0103B2D6`
- `handle_version_update_response()` at `0x0103751C`

On current good startup runs, `mock_update_installed yes` proves startup version update is already skipped, so later `type=6` traffic belongs to a different resource/update path.

### Resource-miss to `type=6` bridge

Status: `confirmed`

Local resource-open failure is bridged into the later `type=6` request path through the resource manager:

- `sub_1044DA0()` calls `sub_100D2BE()` to open a local resource by name
- on failure, it records the missing resource name into a net-manager pending-entry table
- `sub_1044EF6()` marks a pending callback slot for that same missing resource
- later `send_update_chunk_request()` consumes pending entries through `sub_1036C2E()` and serializes `name/screen/type/start/version`

This establishes that `type=6.name` is a local resource filename / pending-download key, not the original business meaning of `0x1B/12.name`.

Supporting static evidence:

- resource open helper: `sub_100D2BE()` at `0x0100D2BE`
- local resource stream wrapper: `load_resource_stream_by_name()` at `0x0100D498`
- resource pending-table writer: `sub_1044DA0()` at `0x01044DA0`
- pending callback marker: `sub_1044EF6()` at `0x01044EF6`
- request builder: `send_update_chunk_request()` at `0x01036C66`
- pending-entry copy helper: `sub_1036C2E()` at `0x01036C2E`

Additional confirmed callsite narrowing:

- `sub_100D2BE()` currently has only four code xrefs in `江湖OL.CBE`:
  - `sub_100D338()` at `0x0100D348`
  - `load_resource_stream_by_name()` at `0x0100D498`
  - `sub_1043206()` at `0x01043216`
  - `sub_1044DA0()` at `0x01044DB4`
- the two post-login/resource-update candidates in scope are `sub_1043206()` and `sub_1044DA0()`
- inside `send_update_chunk_request()` case `n2==4`, the serialized download key comes from the current pending entry at `*(R9+38284) + 2`, while `type` comes from the first `u16` of that same entry, `start` comes from offset `+52`, and `version` comes from `R9+38328`
- `sub_1036C2E()` flips the pending-entry state byte from `1` to `2` when it selects the matching entry for serialization

Latest confirmed runtime narrowing:

- the first observed reuse of the `0x1B/12`-derived UI name `MMORPGTempcbm` is not through `sub_1044DA0()/sub_1044EF6()` and not through `type=6`
- instead, after `kind=27 subtype=12` is consumed, the live path is:
  - `parse_actor_motion_descriptor()` callsite `0x0100D6FC`
  - `sub_100D534()`
  - `load_resource_stream_by_name()` at `0x0100D48A`
  - `sub_100D2BE()`
- in the newest trace, those four steps all carry the same name as the active UI cached name:
  - `name=MMORPGTempcbm`
  - `uiName=MMORPGTempcbm`
- other currently traced callers into `sub_100D534()` (`sub_100D564()` at `0x0100D570` and `sub_100DB82()` at `0x0100DB9C`) do not carry that same `MMORPGTempcbm` string on the observed path; they continue to load normal actor/UI resource names such as `h_warrior.actor`, `ui_h_war.actor`, and map/decoration actor resources

Latest confirmed upstream caller:

- the first confirmed upstream caller that feeds the misrouted name into `sub_10352AE()` is `scene_rebuild_runtime_nodes()` at `0x0100F8F6`
- runtime traces show this exact chain after `kind=27 subtype=12`:
  - `trace_sub_10352ae_call_from_scene_rebuild ... stack0Name=MMORPGTempcbm uiName=MMORPGTempcbm`
  - `trace_parse_actor_motion_entry ... arg4Name=MMORPGTempcbm uiName=MMORPGTempcbm`
  - `trace_resource_stream_call_from_actor_motion ... name=MMORPGTempcbm uiName=MMORPGTempcbm`
- no competing `trace_sub_10352ae_call_from_1036768` line carries that same string on the observed path

This establishes the currently active misroute as:

- `0x1B/12.name`
- copied into active UI name cache (`+1141`)
- reused by `scene_rebuild_runtime_nodes()` as the stacked extra argument for `sub_10352AE()`
- forwarded into `parse_actor_motion_descriptor()`
- treated as a motion-resource name by `load_resource_stream_by_name()`

Additional confirmed asset-format constraint:

- falling back to a normal on-disk actor resource such as `h_warrior.actor` is not a correct long-term substitute for the missing motion descriptor
- runtime evidence:
  - `scene_rebuild_runtime_nodes()` now forwards `stack0Name=h_warrior.actor`
  - `parse_actor_motion_descriptor()` consumes `arg4Name=h_warrior.actor`
  - `load_resource_stream_by_name()` opens the real file successfully
  - but the next resource-open attempts become malformed non-field strings such as `iorwalk1.gif...h_warriorwalk2.gif...jian.gif`, which shows the parser is interpreting the file with the wrong schema
- local asset evidence:
  - scanned `bin/JHOnlineData/*.actor` files in the repo all currently carry type byte `0x02` at offset `+4`
  - none of the tracked local `.actor` files carry the mock motion-wrapper marker `0xF1`

This means the repository currently has many ordinary actor/image-sequence `.actor` files, but no confirmed real motion-descriptor sample matching what `parse_actor_motion_descriptor()` expects.

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
7. `0x23C0('1')` calls the generic next-step callback object at `R9+0x28CC`, method slot `+0x14`, with target `*(R9+0x29E8 + 0x4C)`.

Additional confirmed local-state details:

- `login_form_init()` (`0x2AA8`) explicitly sets `*(r9+10735) = 1`, so the account/password login form's "保存为默认帐号" checkbox is on by default on that path.
- `0x23C0('1')` is a local UI/screen transition, not a direct packet sender.
- the same callback object/method is reused by the role-list parser:
  - `sub_247A()` calls `R9+0x28CC -> +0x14` with target `*(R9+0x29E8 + 0x50)`
  - this means `+0x4C` and `+0x50` are sibling screen/state targets, not ad hoc function callbacks
- the target family behind the later `+0x50` role-list path is now clearer:
  - `sub_4290()` dispatches by `*(r9+10748)`: `0 -> sub_3ECE()`, `1 -> sub_4016()`, `2 -> sub_3FD6()`
  - `sub_3AD2()` resets that family and explicitly sets `*(r9+10748) = 0`
  - `sub_39A4()` explicitly sets `*(r9+10748) = 1` and prepares the local role-entry buffers
  - `sub_3544()` (the confirmed `type=1, subcmd=4` role-list parser) finishes by calling `sub_247A()`

Current highest-confidence interpretation:

- primary `1/1/1` success with `serverinfo/servernum/newVer` transitions into a local server-selection / next-screen target at `+0x4C`
- later `1/1/4` role-list success transitions into the sibling role-selection target at `+0x50`
- this is why a stripped validation reply can consume fields correctly yet emit no immediate follow-up packet: the next step is first a local screen/state switch, not a mandatory network send

Important current mock caveat:

- on the account-login branch, the current built-in mock can bypass this wrapper entirely if it answers a `1/1/1` request with an object whose response subtype is not `1`
- existing runtime evidence shows exactly that mismatch on one live branch:
  - request: `1/1/1`
  - built-in response label: `source=builtin-login`
  - actual response object header: `1/1/6`
- when that happens, title wrapper `0x2D80` fails its `subcmd == 1` check, so `net_handle_login_response()` is not called at all on that path
- this is why fields such as `serverinfo/servernum/newVer` and the later default-account save path can appear "missing" even though the request was recognized as `builtin-login` by the host

### Login Success Branch Split

Status: `mixed`

Confirmed title-module behavior:

- the role-list UI has its own later actor-confirm submit path in `mmTitleMstarWqvga.cbm`
- `sub_4016()` handles confirm/select actions on the role-list screen
- when the user confirms a role, `sub_4016()` calls `sub_3E66(...)`
- `sub_3E66()` builds a network request containing the literal field name `actorinfo` plus three selection-derived values
- there is also a separate title-module response handler `sub_3544()` for `type=1, subcmd=4`
- `sub_3544()` reads fields `result`, optional `servconf`, and then a field also named `actorinfo`
- in that `subcmd=4` path, `actorinfo` is parsed as a counted role-entry list (capped to 5 entries locally), not as the later in-scene actor-status blob
- after parsing that list, `sub_3544()` calls `sub_247A()`, which invokes the same generic next-step callback slot `*(r9 + 10464)()`
- the decompiler shorthand `*(r9 + 10464)()` is now known to be the same router method used by `0x23C0`: `R9+0x28CC -> +0x14`
- that role-list success callback uses target `*(R9+0x29E8 + 0x50)`, whereas the primary `1/1/1` success path uses sibling target `*(R9+0x29E8 + 0x4C)`
- the local composite server/role screen behind that path is stateful:
  - `*(r9+10748) == 0` routes input into `sub_3ECE()` (server-selection navigation)
  - `*(r9+10748) == 1` routes input into `sub_4016()` (role-selection / confirm)
  - `*(r9+10748) == 2` routes input into `sub_3FD6()` (follow-on editor/create-name path)
- `sub_3ECE()` can auto-promote from server mode into role mode by calling `sub_39A4()` when the current server list count is `1`
- `sub_3544()` is also stateful across two subtype values:
  - when `subtype == 16`, it reads and caches `result`
  - only when that cached result is `1` does a later `subtype == 4` packet actually parse `actorinfo`
- current runtime request evidence now adds one more confirmed split point: the live account-confirm tap is sending a top-level `1/1/12` login request (`send_payload ... 01010c00 ... coreVer/appVer/username/password/imsi`), not the earlier assumed `1/1/6`

Current higher-confidence interpretation:

- the branch between "login succeeded" and "show role list" is more likely controlled by outer response stage/wrapper (`subcmd=1` versus `subcmd=4`) than by a magic inner field inside the `subcmd=1` login-success object's `actorinfo`
- the active post-login title screen may also be using the alternate `subcmd=12` success chain (`sub_2A50 -> login_alt_result_dispatch -> login_stage_success_dispatch`) before any role-list-specific stage is even eligible
- reusing the same field name `actorinfo` across these title/game stages is now a confirmed source of confusion: title `subcmd=4.actorinfo` behaves like a role-list payload, while main-CBE login `actorinfo` is the later actor/player blob parsed by `parse_actorinfo_response()`

Confirmed runtime evidence from the current mock branch:

- when the login-success mock returns `actorinfo` immediately, the client does not stop on a role-list request
- the next observed traffic jumps straight into the later post-login game chain (`type=1/2/3`), eventually reaching fresh-enter scene loading
- no separate `0x0A/0x20` role-list request appears on that actorinfo-first path
- the first post-login game packet is not just `1/5/10 id=...`; it is a bundled WT request containing both `1/5/10 id=10001` and `1/7/7 type=1`
- previous built-in replies only answered that bundle with group info plus `0x0A/0x1A` and scene `0x1E/1/3/7`, which left no top-level `7` family response before the first HUD draw
- a focused `1/7/8` experiment was tried next: append one top-level `1/7/8` object carrying `type=4, result=1, seq=1`, because static `sub_1033544()` analysis shows that exact branch can produce the missing `type=15` HUD source head and trigger `scene_rebuild_status_meter_node(2) -> scene_rebuild_status_meter_node(1)` when the source head is still empty
- that experiment is now confirmed unsafe on the default path. Runtime does reach `trace_business_handler ... kind=7 subtype=8`, but then crashes earlier inside `sub_1033544()` because the branch first clones local pointer `*(R9+38020)`, which is still null on the current post-login path. The experiment therefore remains useful as evidence, but is now gated behind `CBE_GROUP_TYPE1_MISC_SYNC8=1` instead of staying enabled by default

More precise current reading of that bundled post-login request:

- `source=builtin-group-type1` is not one clean real server answer yet; it is a mixed convenience bundle the emulator currently uses to push the client forward
- the bundled request is not emitted by the title/login screen. Runtime and static evidence now both place it in `scene_runtime_init_and_sync()` on the first scene screen (`activeScreen=01053450`):
  - `bin/logs/net_trace.log` shows the `len=35` send after `trace_sub_1010228_callsite label=scene_runtime_init_and_sync ... activeScreen=01053450`
  - static `scene_runtime_init_and_sync()` at `0x01013594..0x01013636` first calls `sub_1010228()`, then queues three outgoing `major=1, kind=7, subtype=7` requests with field `type = 1..3`
  - the same block uses the scene-local queue/dispatcher objects at `R9+0x5554` and `R9+0x5520`, so this is a scene-bootstrap follow-up sequence, not a login-form request
- `sub_1010228()` is the pre-send helper that explains why the first `type=1` request is bundled with `1/5/10`:
  - it rebuilds the 0x130-byte local group/party snapshot at `*(R9+0x5CE4+0x10)`
  - it copies current node fields such as id/name/status bytes from `*(R9+0x5C64+0x40)`
  - it sets local send/state flags at `R9+0x5C74` and `R9+0x5C79` to `1`
  - runtime evidence already shows the first queued `type=1` leaves as the combined `1/5/10 id=10001 + 1/7/7 type=1` packet, while later `type=2` and `type=3` are sent alone
- the `1/5/10` half does correspond to a real client request and a real handler family:
  - response object `1/5/10` lands in `net_handle_group_info()` subtype `10`
  - that subtype reads `num`, `groupinfo`, and `leadid`
  - `groupinfo` is parsed as a counted blob of group-member records, looped `num` times, and each decoded entry is forwarded into `sub_1011E1E(...)`
  - `leadid` is used as the active/leader id for subtype `10`
  - unlike subtype `3`, subtype `10` enters the parse path unconditionally, so `result` is not the decisive gate there
- the current `0x0A/0x1A` object is also backed by a real handler, but it belongs to a different family:
  - it lands in `net_handle_role_login_gift_glamour()` subtype `26`
  - that branch reads only `name` and `money`
  - it copies `name` into the three local role/status objects and mirrors `money` into the same trio
  - extra fields the emulator currently sends there such as `result`, `type`, and `npcnum` are not used by subtype `26`
- the appended `0x1E/1`, `0x1E/3`, and `0x1E/7` objects are scene-channel helpers:
  - `0x1E/1` consumes `scene + posinfo`
  - `0x1E/3` expects `curpage/pagenum/colnum/colnames/roomnum/roomlist/npcnum/npcinfo`
  - `0x1E/7` expects `roomid/colnum/colnames/rolenum/rolesinfo`

Current implication:

- the bundled request is best understood as "group info request + another top-level `7` family sync request", not as a single monolithic semantic
- more specifically, `1/7/7` now looks like a scene-side "misc player fields" bootstrap request family:
  - the sender is in scene init, not title/login
  - the live sequence always emits `type=1`, `type=2`, and `type=3` together during the same first-scene bootstrap window
  - current confirmed response consumers fit that reading: `type=2` expects a later top-level `7/20` carrying `pcimg`, and `type=3` expects top-level `7/32` carrying `expcard`
  - `type=1` is the broadest member of the trio and is the one currently still missing its true top-level `7` reply; it is also the only one that leaves bundled with group info
- the emulator's current `builtin-group-type1` response only answers the group half faithfully
- the other half, `1/7/7 type=1`, still lacks its true top-level `7` reply; the current `0x0A/0x1A + 0x1E/1/3/7` additions are useful scene-bootstrap shims, but they are not yet proven to be the real server contract for that request

Hypothesis / current server-mock reading:

- returning `actorinfo` in the initial login-success payload is effectively equivalent to pre-selecting a role
- the safer role-list-first server behavior is therefore:
  - login success still returns a normal success object so the title module can leave the login form
  - the actual role-list content may need to arrive as its own `type=1, subcmd=4` stage rather than as extra objects stuffed into the initial `subcmd=1` success packet
  - `actorinfo` is withheld until the client later submits the explicit role-selection request

Current built-in mock experiment:

- `src/main.c` now defaults to a staged retry instead of the previous same-packet mixed-object retry
- the primary login reply still uses the normal actor-success object so the current title/game parser contract remains satisfied
- immediately after that login reply is queued, the mock now queues a second `event=7` packet containing a dedicated title role-list object
- that follow-up packet is currently built as two title-side objects:
  - `major=1, kind=1, subtype=16` with `result = 1`
  - `major=1, kind=1, subtype=4` with `actorinfo = counted role-entry table`
- the counted role-entry table is currently modeled from `sub_3544()` as:
  - count
  - repeated `u32 roleId, u8 job, u8 sex, string roleName, u32 level`

Implementation note for the current mock branch:

- the live `1/1/12` path must be keyed from the WT header `kind/subtype` bytes `request[5]/request[6]`
- reading `request[6]/request[7]` on the observed `01010c00` login request misclassifies the stage as `12/0`, which prevents the alternate-success shell and the intended `after-main` staged role-list follow-up from firing

Newest order-specific runtime evidence:

- the mixed same-packet experiment is now confirmed sensitive to object ordering as an open question
- `rolelist + actorinfo` in one login-success packet still auto-enters scene on the current branch
- the next active experiment is `actorinfo + rolelist`, to test whether a later role-list object can override the earlier actor-success side effects inside the same event

Supporting evidence:

- title-module role confirm path: `sub_4016()` (`0x4016`) and `sub_3E66()` (`0x3E66`) in `mmTitleMstarWqvga.cbm`
- title-module role-list response path: `sub_3544()` (`0x3544`) in `mmTitleMstarWqvga.cbm`, which validates `type=1, subcmd=4` and parses counted `actorinfo` entries
- current runtime log: `bin/logs/net_trace.log` shows `mock_login_actor_response ...` immediately followed by outgoing `type=1`, `type=2`, and `type=3` packets, with no intervening built-in role-list request

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

Additional current-mock implication:

- the alternate wrapper still does call `net_handle_login_response()`, but it does **not** contain the later default-account persistence logic from `0x2D80`
- current built-in `1/1/12` success shells are also minimal (`result=1` only), so even when this path is hit there is no `serverinfo/servernum/newVer` payload to populate the local server-selection tables

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

### `actorinfo` trailing string and `R9+0x5E46`

Status: `mixed`

Confirmed parser behavior:

- `parse_actorinfo_response()` / `parse_actorinfo_playerinfo_blob()` at `0x0100FA88` writes directly to the fresh-enter text buffer `R9+0x5E46`
- the write happens on the `a2 == 0` `actorinfo` path at `0x0100FD00 .. 0x0100FD08`
- the source is a late string field from the `actorinfo` blob, copied with `sub_100FA56(srcReader, R9+0x5E46, 0x1E)`
- the same function returns `R9+0x5E46` at `0x0100FD24 .. 0x0100FD2E`

Confirmed neighboring field shape:

- immediately before that string copy, the parser reads another scalar field and stores it both into the actor structure and, on the `actorinfo` path, into scene/global state
- immediately after the `R9+0x5E46` copy, the parser reads two more trailing scalar fields into actor structure slots
- this matches the current mock builder pattern in `src/main.c:vm_net_mock_build_actor_info()`, where the final sequence is:
  - string
  - scalar
  - string
  - scalar
  - scalar

Hypothesis:

- the second trailing string in `actorinfo` is much more likely to be a scene/map key or scene resource identifier than a motion-resource filename
- evidence supporting that interpretation:
  - fresh-enter later reuses `R9+0x5E46` as `currentMapIdText`
  - the same value is validated by `sub_100EEBC()` / `sub_100FA40()`
  - restore/fresh-enter logic later reuses the derived persistent slot as outgoing `mapID`
  - local assets already include scene resources named like `c00蓬莱仙岛_01.sce` and `c00蓬莱仙岛_03.sce`

Open mismatch to keep in mind:

- the current mock builder still fills that second trailing `actorinfo` string from `motionResource`
- runtime/static evidence now strongly suggests that semantic is wrong or at least incomplete
- this does **not** yet prove the exact canonical wire value:
  - it may be a full scene filename such as `c00蓬莱仙岛_01.sce`
  - or an extensionless/internal scene key later mapped to the `.sce` asset

Current mock experiment:

- `src/main.c` now routes that second trailing `actorinfo` string, and the synthetic `0x1B/12.name`, through a dedicated `vm_net_mock_scene_key_name()` helper instead of `motionResource`
- default mock value remains the existing GBK scene key `c00蓬莱仙岛_01`
- `CBE_SCENE_KEY` can now override this field for targeted reruns

Latest confirmed local-load behavior after that change:

- the extensionless scene key is now consumed as a real local scene resource, not as an actor/motion resource
- storage trace shows:
  - `file_open_resolve_map_extension from=JHOnlineData/c00蓬莱仙岛_01 to=JHOnlineData/c00蓬莱仙岛_01.sce`
  - the `.sce` payload contains `SCE2` plus embedded reference `00蓬莱仙岛_01.map`
  - the client then opens `JHOnlineData/00蓬莱仙岛_01.map`
  - the `.map` payload expands into scene art such as `m_peak1.gif`, `m_village05.gif`, and `m_apotheosis.gif`
- this strongly confirms that the corrected field belongs to the local scene/map resource family

## Legacy Source

`docs/net_mock_protocol.md` is retained as a historical working note and source document. New durable protocol conclusions should be recorded here first, then the legacy note can be trimmed or cross-referenced as needed.

### `event=7` wrapper vs title/login callbacks

Status: `confirmed`

Current emulator/runtime evidence now separates three layers that had previously been conflated during login-flow tracing:

1. queued task callback
   - current queued `event=7` replies use `0x0103489A -> net_wrapper_event_dispatch`
2. net-manager callback
   - `net_wrapper_event_dispatch` first calls `get_net_manager_object()+0x44`
   - runtime watcher reads the same slot as `R9+0x9588+0x44`
3. business/title callback
   - only after the net-manager leg, and only when the current business object state allows it, the wrapper calls `*(R9+0x94A8)+0x14`

Confirmed current-login observation:

- in the “click confirm but no screen transition” runs, `net_state_observe cb44=01037473`
- static RE confirms `0x01037472 -> handle_version_update_response`
- therefore the queued login reply is still entering the wrapper through the version/update-family callback before any title-local callback can drive a `0x23C0` / `0x247A` style screen-switch path

Implication:

- `runtime_state dispatch=05017509` should not be read as “the queued task directly called the title success callback”
- it is only the current business callback stored on the active object
- lack of `trace_title_screen_callback` at `0x23D4/0x248A/0x1984/0x1994` is consistent with the wrapper never reaching those title-success BLX sites

### Title login local state family at `R9+0x29E8`

Status: `confirmed`

The active title/login state block used by the current screen includes at least:

- `+0x00` / runtime `state0`
- `+0x02` / runtime `focus`
- `+0x07` / runtime `saveDefault`
- `+0x14` / runtime `mode`
- `+0x3C` / runtime `listPtr`

Confirmed local routines around this block:

- `0x2620 -> login_record_init_and_load()`
  - initializes/loads `mmorpg_LoginRecord`
  - writes the local `saveDefault/state0` bytes during button-path setup
- `0x53EC`
  - inbound parser for top-level `type=1` family with subtypes `6/7/8/15`
- `0x5324`
  - subtype `7` handler; appends entries into `*(R9+10788)` and updates list-related local state
- `0x39A4`
  - subtype `15` success path; sets `*(R9+10748)=1`

Current implication:

- the active login/selection state machine behind `010534b4` is better explained by a `type=1, subtype=7/15` follow-up family than by the earlier `1/1/4` role-list hypothesis
- this matches runtime evidence where the current `1/1/1` success reply and staged `1/1/16 -> 1/1/4` follow-up produce no writes at all to `mode/listPtr/roleFamily/target*`
- the current default mock experiment has therefore been reduced to the narrowest confirmed gate first: `1/1/15 {result=1}` without the old staged `1/1/4` role-list payload

### Shared business/event owner at `R9+0x94A4`

Status: `confirmed`

The queued `event=7` transport wrapper does not own its own high-level protocol family. It forwards into the object currently stored at `*(R9+0x94A4)`.

Confirmed object constructor:

- `0x01034922` initializes the object fields and installs callbacks:
  - `+0x24 = sub_10348FC`
  - `+0x28 = sub_1034874`
  - `+0x2C = sub_103478E`
  - `+0x30 = sub_1034868`
  - then stores the object to `*(R9+0x94A4)`

Confirmed current caller:

- the only direct caller of `0x01034922` is `0x01003856 -> scene_system_bootstrap`

Implication:

- on the current live path, the shared business/event owner at `R9+0x94A4` is already a scene/bootstrap-owned object before login completes
- therefore queued login replies are not being delivered into a title-login-owned high-level parser; they are first routed through a scene/bootstrap owner
- this matches runtime evidence where `sceneObj` is already live and `cb44=01037473` remains stable while the title login screen `010534b4` is still active
- the newest rerun strengthens this from “already scene-owned during login” to “installed once and never replaced”: `trace_scene_owner_site` fires at `tick=0` for `scene_system_bootstrap` and then `shared_event_owner_init`, followed immediately by the only observed `trace_shared_event_owner_write slot=0105a078 old=00000000 new=010560f0`. No later title-stage overwrite of that slot appears before or during the `01056204 -> 0105A814 -> 010534B4` chain, nor during subsequent `1/1/1` / `1/1/15` deliveries.

Related local startup gate:

- `sub_1002538()` does not wait for any network response. It fetches a function pointer from `sub_1003C5A()`, and that helper simply returns `scene_system_bootstrap`; `sub_1002538()` then calls it directly and marks local byte `R9+21300 = 1`.
- `sub_100254A()` enters that direct branch whenever `sub_100AFCA() == 1`.
- `sub_100AFCA()` defaults to `1` unless a callback table at `R9+19848` exists, accepts `(1002,1)`, and reports a nonzero value for `(1002,3)`.
- `sub_100D04A()` also converges back to `sub_1002538()` on several local fallback conditions.
- runtime has now confirmed the first half of that contract: on the current emulator path, the `(1002,1)` callback is actually invoked via `0x0C003000` and returns `0`, after which `sub_100AFCA()` immediately takes its default-true path and enters `sub_1002538()`. The second `(1002,3)` query is therefore never reached on this path.
- address attribution correction: `0x0C003000` is the first `BILLING` manager stub (`VM_MANAGER_BILLING_FUNC_LIST_ADDRESS`), not a `DL_PAY` stub. So the local startup gate is currently being decided by emulator `BILLING` semantics, specifically the first billing callback entry that the CBE-side `vmdlEnterPay.c` logic uses for code `1002`.
- further correction from the newest rerun: `sub_100AFCA()` returns `true` only when `(1002,3) == 0`, not when it is nonzero. After changing the emulator billing stub so `(1002,1)` and `(1002,3)` both return `1`, runtime still falls into `sub_1002538()`, but now through the later `sub_100D04A()` fallback rather than through `sub_100AFCA()` itself.
- static decompile of `sub_100D04A()` narrows that later fallback to:
  - `sub_100B95C() == 1`
  - or `sub_1000648() == 0`
  - (`sub_1000202()` is hardcoded to return `1` and is therefore not the deciding branch on the current binary)

Implication:

- the current “title login screen plus scene-owned shared event router” state is consistent with a local startup mode gate that is already selecting the direct-enter/game bootstrap path before the title login flow finishes, rather than with a login reply that later misroutes control.
- more narrowly, the currently implemented emulator `BILLING` semantics are sufficient to trigger that direct-enter path by themselves.
