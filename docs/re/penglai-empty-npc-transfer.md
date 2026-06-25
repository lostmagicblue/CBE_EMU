# Penglai Empty NPC And Transfer Notes

Date: 2026-06-24

## Current Contract

- Scene NPC seeding is not part of the current server contract. The mock server returns empty NPC data while map transfer work continues.
- `5/10`, `12/1`, scene task subset, actor-other `2/10`, and actorinfo NPC seed paths must default to zero NPCs.
- Penglai local SCE portal rectangles are documented in `tmp/all_sce_bundle/*/scene.json` and mirrored in the mock portal fallback tables for route lookup.

## Verified

- Fresh startup still reaches `蓬莱仙岛_十二域`.
- Direct startup into saved `00蓬莱仙岛_02.sce` reaches visible map `蓬莱一线剑谷` without any env override.
- Reproduced old crash by starting at `00_蓬莱仙岛01.sce` position `(200,486)` and stepping through the bottom portal:
  - old failing chain ended after `2/3 -> builtin-scene-change`;
  - process asserted in `hookRam.c` at `pc=0x01014ee0`.
- Current chain no longer asserts:
  - `2/10 -> builtin-actor-other-only10`
  - `2/3 -> builtin-scene-change`
  - optional `25/5 -> builtin-scene-default-event`
- Position persistence is active for scene-change targets and moveinfo uploads. After the failed runtime transfer saves `_02`, the next launch reads `nvram/jhol_mock_player_pos.bin` and enters `_02` directly.

## Negative Probes

- `2/3` as only `2/9 {result,posinfo}` did not trigger a second scene `ScreenInit` and left the loading screen active.
- Appending `2/9` to the `2/3` response, before or after `30/2`, did not trigger the scene switch.
- `2/3` as only `30/1 {scene,posinfo}` is parser-safe and saves the target, but the runtime transfer still leaves `sceneRuntimeReady=0, sceneAssetsReady=1`.
- `2/3` as `30/1` followed by `2/9 {result=1,posinfo}` changes parser state from `7` to `4`, but still leaves `sceneRuntimeReady=0`.
- Answering the preceding Penglai `2/10 Type=1` with a full resource-followup plus `30/1` did not stop the client from emitting the later `2/3`/`25/5` chain.
- Forcing immediate dispatch of the pending `25/5` completion response did not change the loading-screen outcome.
- Echoing request `maptype` in `30/2` is parser-safe, but by itself does not complete the runtime scene switch.
- Emulator lifecycle probes showed the same-class `vmChangeScreen` shim is part of the visible symptom:
  - disabling same-screen re-init avoids the loading screen, but leaves the player on the old map and causes repeated `2/10` requests;
  - destroy+init still enters the pending branch and leaves `sceneRuntimeReady=0`;
  - resource-load without init also avoids loading, but does not change maps.

## Remaining Gap

The transfer no longer crashes, but the client remains on the loading screen after `2/3` when entering `00蓬莱仙岛_02.sce` from `00_蓬莱仙岛01.sce`. Direct `_02` startup renders correctly because `scene_runtime_init_and_sync()` takes the fresh scene-enter path and sets `sceneRuntimeReady=1`. Runtime transfer takes the pending same-screen path at `0x010131DA`, which explicitly sets `sceneRuntimeReady=0`, and the observed server response variants do not make the client re-enter the fresh path.

Next probe should focus on what clears or bypasses the pending same-screen path before `scene_runtime_init_and_sync()` runs, not on NPC data. The most relevant IDA targets are the producer of `R9+0x5C6B`, `FindEmptyActorSlot(0x01018166)`, `scene_handle_enter_with_scene_pos(0x010396D6)`, and `ProcessSceneState/HandleSceneTransition(0x01003CFC/0x0100369C)`.

## Reset Evidence

- Re-running the runtime portal path on a clean baseline, with the render/assert compatibility shims removed, reproduced the original stall without any host-side state forcing:
  - after `2/3 -> builtin-scene-change`, scene data switches to `_02` and `parserState=7`,
  - but `sceneRuntimeReady` falls to `0` while `sceneAssetsReady` remains `1`,
  - and no additional `ScreenInit` is logged after the `2/3` response.
- Direct startup into saved `_02` is the contrast point:
  - it reaches `_02` with `pending=1, ready=1, assets=1, parserState=7`,
  - so `pendingSceneSwitch` by itself is not the blocker; the critical difference is `sceneRuntimeReady`.
- IDA reconfirmed the two same-class scene-change callsites:
  - `FindEmptyActorSlot` calls the platform screen-change API at `0x010182A6`;
  - `EnterSceneByMapName` calls the same API at `0x01018150`.
- The current emulator screen-manager shim only treats `0x010182A6` as a real lifecycle request. A narrow validation run that also accepted `0x01018150` produced one more `ScreenInit Ok` and moved the runtime past the old loading stall, but immediately exposed a new unhandled request:
  - top-level `25/5`, `len=79`,
  - first objects `1/25/5:0,1/2/3:55,1/27/11:0,1/7/42:0`.
- That validation patch was reverted after capture. The important conclusion is that the old "stuck loading" symptom sits in front of a missing post-`EnterSceneByMapName` follow-up contract, not in front of NPC data or the loading overlay itself.

## Recheck Findings

- Re-enabling the same-screen lifecycle for both `FindEmptyActorSlot(0x010182A6)` and `EnterSceneByMapName(0x01018150)`, then adding a narrow handler for the previously unhandled post-enter combo `25/5 + 2/3 + 27/11 + 7/42`, moves the runtime one phase further:
  - runtime portal repro now reaches visible `_02` map screenshots instead of staying permanently on loading;
  - verified in `bin/autotest/screens/000014_00042035.bmp` and `000015_00045036.bmp`, which show `蓬莱-铸剑谷`.
- The new remaining failure is a repeat re-entry loop after the map becomes visible:
  - repeated requests `2/3 len=89 -> 25/5 len=94 -> 6/1 len=39`,
  - all still target `00蓬莱仙岛_02.sce` with `exit=0`,
  - and eventually the client falls back to the loading screen again (`000016_00048037.bmp`).
- Runtime probe shape on the new baseline:
  - before the first `2/3`, `_01` is at `pos=(200,486)`, `parserState=7`, `assets=1`;
  - after the initial `2/3` and the new post-enter handler, `_02` reaches `pos=(396,473)`;
  - `ready=1, assets=1` appears while the map is visible, but later repeated same-target `2/3` responses drag it back into loading.
- The repeated `2/3 len=89` combo is narrower than the original stall:
  - same target scene `_02`,
  - same `exit=0`,
  - object family `books=1, fb11=1, fb4=1`,
  - so the next step is to recover why the client keeps re-emitting that same `_02` transition contract after the first visible render, rather than adding more NPC-side behavior.

## Runtime Map Switch Fixed

- Treating the repeated `_02` scene-change combo as its own completion phase, instead of replaying the full bootstrap packet, stabilizes the runtime `_01 -> _02` portal path:
  - initial `_01 -> _02` still uses `WT 2/3 len=74` with the full Penglai `_02` bootstrap;
  - repeated same-target `_02 exit=0` requests now match a narrow handler and return only `30/2 ack-without-posinfo + 27/11 + 27/4 + 7/42`;
  - this stops the old `2/3 len=89 -> 25/5 len=94 -> 6/1 len=39` re-entry loop.
- The scene-change completion path also needs prompt host-side send-ready rearming while a scene transfer is active:
  - rearming send-ready only for the active scene-change contract avoids the old `alloc_outgoing_game_event()` exhaustion at `scene_runtime_tick(0x01014EE0)`;
  - broad unconditional rearming reaches the same stable map-transfer state, but adds noisy `99/1` traffic during login; the current code keeps the rearm scoped to scene-transfer phases.
- Verified stable runtime evidence on the current baseline:
  - autotest command: `bin/main.exe --autotest --shot-ms=3000 --max-ms=50000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f`;
  - after the repeat `_02` request, the client advances to `25/5 len=59 -> builtin-scene-task-subset-followup` instead of crashing or returning to loading;
  - scene probes remain stable through the end of the 50s run:
    - `39032ms`: `ready=1, assets=1, parserState=7, pos=(396,473)`;
    - `42036ms`: `ready=1, assets=1, parserState=7, pos=(396,473)`;
    - `45038ms`: `ready=1, assets=1, parserState=7, pos=(396,473)`;
    - `48040ms`: `ready=1, assets=1, parserState=7, pos=(396,473)`.
- Visual confirmation:
  - `bin/autotest/screens/000013_00039032.bmp`,
  - `bin/autotest/screens/000014_00042036.bmp`,
  - `bin/autotest/screens/000015_00045038.bmp`,
  - `bin/autotest/screens/000016_00048040.bmp`
  all show the `_02` map (`蓬莱-铸剑谷`) still rendered, with no fallback to the loading overlay.

## Direct `_03` Startup Fixed

- Direct startup into saved `c00蓬莱仙岛_03` no longer dies in `scene_runtime_tick(0x01014EE0)` and now stays on the map instead of bouncing forever between loading phases.
- The failing startup chain turned out to be narrower than a full portal transfer:
  - first the client emits a same-target `WT 2/3 len=71` with `27/11 + 7/42`;
  - then it requests `25/5 len=91` and `6/1 len=39`;
  - finally it re-emits a same-target `WT 2/3 len=86` with `27/11 + 27/4 + 7/42`.
- The fix is to treat those startup packets as current-scene completion phases, not as fresh scene enters:
  - `2/3 len=71` now returns the deferred scene-completion family `27/12 + 27/11 + 27/4 + 7/42 + 30/2`;
  - the current-scene `6/1 len=39` follow-up is rerouted to the lighter task-subset response instead of the generic scene-resource handler, so it no longer injects another `30/1` scene-enter;
  - the later same-target `2/3 len=86` is handled as a repeat completion ack (`30/2 + 27/11 + 27/4 + 7/42`) without reopening the scene-change target loop.
- Scope note:
  - this current-scene completion path must stay narrow to `_03` startup; letting it also consume direct `_02` startup hijacks the older Penglai `_02` bootstrap (`2/3 len=74`) and brings back the `scene_runtime_tick(0x01014EE0)` crash.
- Verified runtime evidence on the current baseline:
  - direct `_03` startup run reaches `25/5 len=91 -> builtin-type27-followup` and `6/1 len=39 -> builtin-scene-task-subset-followup-current-scene`;
  - no further crash occurs, and scene probes remain stable through the end of the run:
    - `39029ms`: `ready=1, assets=1, parserState=7, pos=(105,395)`;
    - `42034ms`: `ready=1, assets=1, parserState=7, pos=(105,395)`;
    - `45037ms`: `ready=1, assets=1, parserState=7, pos=(105,395)`.
- Visual confirmation:
  - `bin/autotest/screens/000085_00042715.bmp`
  - `bin/autotest/screens/000093_00046735.bmp`
  both show the `_03` map (`蓬莱迷境`) still rendered instead of the loading overlay.

## Next Gap

- Runtime `_01 -> _02` transfer and direct saved `_03` startup are now stable on the mock server.
- Runtime `_02 -> _03` transfer is now also stable on the mock server.
- The next protocol step is smoothing the remaining empty prompt/panel behavior after `_03` entry and then extending the same empty-NPC scene-completion style to later Penglai transitions such as `_03 -> _04`.

## Runtime `_02 -> _03` Fixed

- Reproduced the old `_02 -> _03` crash with runtime evidence:
  - after `_02` was stable on screen, the client emitted `2/10 len=19`, then `2/3 len=90`;
  - the older generic `builtin-scene-change` response left `_03` in a half-entered state and later asserted at `scene_runtime_tick(0x01014EE0)`.
- The missing contract turned out to be two separate pieces:
  - the first runtime `_02 -> _03` `2/3 len=90` must use the same full same-class transfer bootstrap style as the older Penglai `_02` portal path (`scene resource subset + 30/2 scene-pos + 30/1 scene-enter`), not the lighter generic `30/2 ack-without-posinfo` path;
  - the later `_03` `6/1 len=39` follow-up must be recognized as the current-scene task-subset completion path even when the remembered target was saved as `c00..._03.sce` while the current scene key had already been normalized to extensionless `c00..._03`.
- Implementation notes:
  - generalized the same-class Penglai transfer bootstrap so live `_02 -> _03` uses the full bootstrap path without regressing direct saved `_02` startup;
  - added loose scene-name comparison for current-scene completion checks so `c00..._03` and `c00..._03.sce` are treated as the same scene;
  - widened the current-scene task-subset detector to accept the active remembered `_03` transfer target, not only the already-completed target.
- Verified runtime evidence on the current baseline:
  - `2/3 len=90 -> builtin-scene-change resp=378`
  - `25/5 len=95 -> builtin-type27-followup resp=116`
  - `6/1 len=39 -> builtin-scene-task-subset-followup-current-scene resp=177`
  - no extra `_03` re-entry family is emitted afterward:
    - no second `25/5 len=95`,
    - no later `2/3 len=86` current-scene repeat-ack,
    - so the earlier visible repeated loading-bar loop is gone at the packet level, not only hidden by a final stable screenshot.
  - scene probes remain stable through the end of the 65s run:
    - `45033ms`: `ready=1, assets=1, parserState=7, pos=(105,395)`
    - `48034ms`: `ready=1, assets=1, parserState=7, pos=(105,395)`
    - `51038ms`: `ready=1, assets=1, parserState=7, pos=(105,395)`
    - `63054ms`: `ready=1, assets=1, parserState=7, pos=(105,395)`
  - `bin/autotest/state_portal_02_to_03_after_fix4.txt` ends with `autotest_exit`, not an assert.
- Latest packet-correct validation:
  - `bin/autotest/state_portal_02_to_03_after_fix5.txt`
  - stable through `65003ms` with the reduced packet family above.

## 2026-06-24 Guard Regression Re-Fix

- A later local regression re-broke the `_02` post-enter chain before the `_02 -> _03` portal handoff:
  - current source still reached `_02` with `2/3 len=74 -> builtin-scene-change` and `25/5 len=79 -> builtin-scene-change-post-enter-followup`,
  - but then it fell straight to `25/5 len=44 -> builtin-scene-task-subset-followup`,
  - while `screen_mgr idx=2 type=same-suppressed caller=01018150` kept firing against the already completed `_02` target.
- Root cause:
  - the same-screen reenter guard in `src/main.c` was still treating the last completed target as suppressible,
  - so `_02` post-enter reentry kept getting blocked even after the server-side scene-change target had been completed and cleared.
- Narrow fix:
  - keep the guard scoped to the in-flight scene-change target only;
  - once `g_vm_net_mock_last_scene_change_target_valid` drops false, clear the guard and stop suppressing `EnterSceneByMapName(0x01018150)` for the completed target.
- Runtime evidence after the fix:
  - the old stable `_02` chain returned with no additional packet-shape changes:
    - `25/5 len=94 -> builtin-type27-followup`
    - `6/1 len=39 -> builtin-scene-resource-followup`
    - `2/3 len=89 -> builtin-scene-change-penglai02-repeat`
    - `25/5 len=59 -> builtin-scene-task-subset-followup`
  - `_02` remained stable with `pending=0, ready=1, assets=1`.
- Re-validated live `_02 -> _03` after the guard fix using:
  - `bin/main.exe` from `bin`
  - autotest actions:
    - `5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f,43000:hold:d:2000`
  - observed chain:
    - `2/10 len=19 -> builtin-actor-other-only10`
    - `2/3 len=90 -> builtin-scene-change resp=326`
    - `25/5 len=95 -> builtin-type27-followup resp=116`
    - `6/1 len=39 -> builtin-scene-task-subset-followup-current-scene resp=177`
- Correct landing point is now verified again:
  - `48037ms`: `pos=(145,47)`
  - `51041ms`: `ready=1, assets=1, parserState=7, pos=(145,47)`
  - persistent save after the run: `c00蓬莱仙岛_03.sce @ (145,47)`
  - screenshot `bin/autotest/screens/000017_00051041.bmp` shows the player standing near the `_03` north entrance rather than the old default `(105,395)` fallback.

## 2026-06-24 `c00_03` East Portal Recheck

- The later "still crashes after switching map" report is now reproduced on the real east edge of `c00蓬莱仙岛_03`, and the route is not the older guessed `_03 -> _04` path.
- Local scene evidence from `tmp/all_sce_bundle/c00蓬莱仙岛_03.sce/scene.json` / `scene.png` shows:
  - north edge portal -> `00蓬莱仙岛_02.sce`
  - east edge portal -> `01桃花岛_01.sce`
- Current `src/mock-server.c` still contains stale fallback rows for `c00蓬莱仙岛_03` that describe:
  - north -> `_02`
  - bottom -> `_04`
- Runtime repro using saved start `c00蓬莱仙岛_03 @ (401,288)` and a late `hold:d:7000` confirms the live east-portal chain:
  - `2/10 len=19 -> builtin-actor-other-only10`
  - then `2/3 len=87 -> builtin-scene-change resp=162`
  - then assert at `scene_runtime_tick(0x01014EE0)`
- The post-`2/3` probe now shows Taohuadao-01 coordinates:
  - current player path near `(230,425) -> (208,432)`
  - paired scene node near `(225,116) -> (240,96)`
- This proves the crash is on the real `c00蓬莱仙岛_03 -> 01桃花岛_01` live transfer path.
- Working hypothesis for code:
  - the generic `builtin-scene-change` contract is still too light for this runtime edge-portal transfer;
  - as with the older `_02 -> _03` crash, the client likely needs the full scene bootstrap family instead of the narrow ack-only path.
