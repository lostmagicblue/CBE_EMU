# Session Log

Add short dated notes here when a change or discovery is important but not yet worth a larger write-up.

Suggested entry format:

```text
## YYYY-MM-DD
- changed:
- verified:
- evidence:
- next:
```

## 2026-06-02
- changed: added a minimal `vmMfMemoryBlockManager` bridge in `src/runtime/emulator_runtime.c` so slots `0/1/2` now call the existing `vm_MF_MemoryBlock_Malloc()`, `vm_MF_MemoryBlock_Reset()`, and `vm_MF_MemoryBlock_Release()` helpers instead of being uniformly stubbed.
- verified: after the earlier `vmDfDataPackageManager idx=0/4` fixes, the client now gets far enough to show the loading/version UI and only aborts later at `tick=66` when queued network event `5` fires into callback `0x0103489B`; the last guest PC before the abort is `0x01034524` inside the event-packet field initializer `sub_1034518`.
- evidence: fresh `bin/net_trace.log` shows `vmDfDataPackageManager_impl idx=4` followed by normal UI drawing, then `open_channel ... cb=0103489b`, delayed dispatch of event `5`, `fire_event slot=0 event=5`, and finally `host_signal sig=22 ... last=01034524`; IDA shows `sub_1034518` is used by `event_packet_add_field` / `event_packet_parse_WT`, and `src/vmFunc.c:1919` documents `VM_MF_MemoryBlock_FUNC_LIST_ADDRESS` slot `0/1/2` as the memory-block `Malloc/Reset/Release` entrypoints.
- next: rerun the rebuilt binary to confirm whether satisfying `vmMfMemoryBlockManager` slot `0/1/2` allows the version-request/event-packet path to continue, or whether another manager/interface becomes the next confirmed blocker.

## 2026-06-02
- changed: reconnected `vmDfDataPackageManager idx=0` to `vm_DF_DataPackage_LoadPackage()` and `idx=4` to `VM_DF_DataPackage_DoLoading()` in `src/runtime/emulator_runtime.c`, using the existing implementations in `src/vmFunc.c` instead of stubbing or forcing the client forward.
- verified: on the unmodified runtime, the first true post-login blocker was earlier than the old scene/status trace: `idx=0` was hit and aborting at `tick=1`; after fixing that, the next exposed missing entry is `idx=4`, matching the DF_DataPackage manager layout already documented in `vm_initDFDataPackage()`.
- evidence: fresh `bin/net_trace.log` lines show `vmDfDataPackageManager_impl idx=0` from the `0x0103B9FC` resource-init path where earlier runs aborted, and the user-observed runtime print changed to `[impl]vmDfDataPackageManager调用位置:4`; `src/vmFunc.c` defines `VM_DF_DataPackage_DoLoading()` at the corresponding slot.
- next: rerun the rebuilt binary and capture whether another DF_DataPackage slot is missing next, or whether control finally moves on to the later loading/scene path.

## 2026-06-02
- changed: removed the earlier progress-forcing scene hook from `src/runtime/emulator_runtime.c` that backfilled scene tile entries `25/26` from fallback tiles and short-circuited missing tile draws by returning success to the client draw path.
- verified: rebuilt successfully with `make` after removing that in-game-state mutation hook; the runtime now exposes the post-login path without those tile-table rewrites and draw skips.
- evidence: deleted `vm_runtime_patch_bootstrap_tiles` / `vm_runtime_skip_missing_tile_draw` and their call sites; `make` linked a new `bin/main.exe`.
- next: reproduce the login-success path again on the unmodified runtime and treat earlier status-panel/divide findings as historical evidence from the previously modified run until reconfirmed.

## 2026-06-02
- changed: narrowed the `Post-Login Loading Crash` with existing `bin/net_trace.log`, IDA call chains, and a new minimal post-preload runtime trace at the two `scene_draw_status_panels` divide sites (`0x0101477E`, `0x010147AE`) to capture the next failing divisor/value pair.
- verified: the post-login path already gets past the title/login module and into main-scene initialization; the client sends follow-up `type=1/2/3` game requests, receives the current built-in mock responses, reaches `scene_runtime_tick` / `scene_draw_status_panels`, and then aborts through the C runtime divide/error path instead of failing earlier in screen transition.
- evidence: `bin/net_trace.log` shows `mock_group_type1_response` plus `mock_game_type_response type=2/3`, then `post_preload_status_draw`, valid tile states for `25/26`, and finally `host_signal sig=22` with a crash ring that runs from `scene_draw_status_panels` into `sub_104D538 -> sub_104E7C8 -> sub_104EA2C`; IDA confirms `sub_104D538` is the signed divide helper that aborts on zero divisor.
- next: rerun with the new divisor trace enabled, identify which status field is zero at the failing divide, and decide whether the fix belongs in the mock scene/type-1 payload or in local scene/status initialization semantics.

## 2026-06-02
- changed: documented the current reverse-engineering operating context for future sessions, including the main IDA analysis entry points, the project goals, current protocol/emulator progress, and the active blocker after login success.
- verified: the project currently has successful version-check and account-login mock responses, but still crashes after the login screen transitions into the loading stage.
- evidence: user-provided project status and workflow notes; current durable references now live in `docs/re/README.md`, `docs/re/firmware-map.md`, and `docs/re/open-questions.md`.
- next: capture the post-login crash context, determine whether the fault is caused by a missing emulator interface or missing follow-up packet flow, and record the confirmed cause once reproduced.

## 2026-06-02
- changed: split the emulator entry/assembly flow out of `src/main.c`; bootstrapping now stays in `src/main.c`, shared emulator globals moved to `src/core/emulator_state.c`, and the large runtime/hook implementation moved to `src/runtime/emulator_runtime.c`.
- verified: rebuilt successfully with `make` after converting `vmFunc.c`, `hookRam.c`, and `vmEvent.c` from `main.c` inclusions into normal object files in `Makefile`.
- evidence: `make` completed and linked `bin/main.exe`; `src/main.c` is reduced to 175 lines while the extracted runtime module carries the prior emulator/runtime behavior.
- next: continue peeling `src/runtime/emulator_runtime.c` by subsystem, with screen/input/network manager hooks as the next clean split points.

## 2026-06-02
- changed: extracted a new runtime service layer into `src/runtime/emulator_services.c` plus `src/runtime/emulator_runtime_internal.h`; LCD helpers, input overlay/event handling, SDL event loop, and NV/profile persistence no longer live in `src/runtime/emulator_runtime.c`.
- verified: rebuilt successfully with `make` after wiring the new runtime service object into `Makefile` and switching the remaining runtime code to the shared internal header.
- evidence: `src/runtime/emulator_services.c` now holds 609 lines of service code; `src/runtime/emulator_runtime.c` dropped to 8789 lines and still links into `bin/main.exe`.
- next: split the remaining `vm manager` dispatch block out of `src/runtime/emulator_runtime.c`, likely into one or more manager-focused modules using the new internal runtime header.

## 2026-06-02
- changed: pulled the top-level `vm manager` dispatch table (`vMInit*` / `vMGet*`) into `src/runtime/emulator_managers.c`, leaving `src/runtime/emulator_runtime.c` focused on concrete manager bodies plus the wider hook orchestration.
- verified: rebuilt successfully with `make` after wiring `obj/runtime/emulator_managers.o` into `Makefile` and exposing the shared manager callback through `src/runtime/emulator_runtime_internal.h`.
- evidence: `src/runtime/emulator_managers.c` now holds 361 lines for the top-level manager dispatch shim while `src/runtime/emulator_runtime.c` remains buildable at 8908 lines and still links into `bin/main.exe`.
- next: continue peeling the concrete sub-manager dispatchers out of `src/runtime/emulator_runtime.c` in groups such as LCD, screen, and network.

## 2026-06-02
- changed: split the concrete LCD, screen, and network vm manager bodies into `src/runtime/emulator_manager_lcd.c`, `src/runtime/emulator_manager_screen.c`, and `src/runtime/emulator_manager_network.c`; `src/runtime/emulator_runtime.c` now keeps only thin dispatch shims plus the shared runtime helpers/state those modules still call.
- verified: rebuilt successfully with `make` after adding the three new manager objects to `Makefile` and widening the internal runtime header for the shared helper/state boundary.
- evidence: `src/runtime/emulator_runtime.c` dropped to 7793 lines, while the extracted manager modules now carry 868 lines for LCD, 126 lines for screen, and 118 lines for network, and the link step still produced `bin/main.exe`.
- next: keep peeling the remaining concrete manager families out of `src/runtime/emulator_runtime.c`, with `fileio/stdout/timer/ctrl` or the DF/game utility clusters as the next clean candidates.

## 2026-06-02
- changed: split the `fileio`, `stdio`, `timer`, and `ctrl` vm manager bodies into `src/runtime/emulator_manager_fileio.c`, `src/runtime/emulator_manager_stdio.c`, `src/runtime/emulator_manager_timer.c`, and `src/runtime/emulator_manager_ctrl.c`; `src/runtime/emulator_runtime.c` now dispatches those manager families through thin wrappers.
- verified: rebuilt successfully with `make` after adding the four new manager objects to `Makefile` and exporting the timer scheduler helpers through `src/runtime/emulator_runtime_internal.h`.
- evidence: `src/runtime/emulator_runtime.c` dropped to 7301 lines, while the extracted manager modules now carry 194 lines for file I/O, 230 lines for stdio, 71 lines for timer, and 18 lines for ctrl, and the link step still produced `bin/main.exe`.
- next: continue peeling the higher-churn manager clusters out of `src/runtime/emulator_runtime.c`, with `df_engine / df_script / game_util / gameold` or the media-oriented groups as the next clean split.

## 2026-06-02
- changed: split the `game_util`, `df_engine`, `df_script`, and `gameold` vm manager bodies into `src/runtime/emulator_manager_game_util.c`, `src/runtime/emulator_manager_df_engine.c`, `src/runtime/emulator_manager_df_script.c`, and `src/runtime/emulator_manager_gameold.c`; `src/runtime/emulator_runtime.c` now dispatches those manager families through thin wrappers and keeps the download/video manager stubs local as shared runtime fallbacks.
- verified: rebuilt successfully with `make` after adding the four new manager objects to `Makefile` and exporting the shared DreamFactory helper lookups through `src/runtime/emulator_runtime_internal.h`.
- evidence: `src/runtime/emulator_runtime.c` dropped to 6005 lines, while the extracted manager modules now carry 204 lines for game util, 34 lines for df engine, 25 lines for df script, and 457 lines for gameold, and the link step still produced `bin/main.exe`.
- next: continue peeling the remaining media/download-oriented runtime handlers out of `src/runtime/emulator_runtime.c`, especially `game_lcd`, `audio`, `appstore`, and the dl/video helper families that are still grouped in the core runtime file.

## 2026-06-02
- changed: split the remaining media/download-oriented handlers into `src/runtime/emulator_manager_game_lcd.c`, `src/runtime/emulator_manager_audio.c`, `src/runtime/emulator_manager_appstore.c`, `src/runtime/emulator_manager_dl.c`, and `src/runtime/emulator_manager_video.c`; `src/runtime/emulator_runtime.c` now routes `game_lcd`, `audio`, `appstore`, `dl_*`, and `video` through thin wrappers, while the shared env-flag helper `vm_net_mock_env_u32` is exposed through `src/runtime/emulator_runtime_internal.h`.
- verified: rebuilt successfully with `make` after wiring the five new manager objects into `Makefile` and moving the video-manager log state into its dedicated module.
- evidence: `src/runtime/emulator_runtime.c` dropped to 5759 lines, while the extracted manager modules now carry 42 lines for game lcd, 178 lines for audio, 29 lines for appstore, 55 lines for dl, and 45 lines for video, and the link step still produced `bin/main.exe`.
- next: continue peeling the remaining runtime-local manager families such as `netapp`, `sensor`, and `vmim`, or start a second pass that groups shared download/network stub helpers into a smaller `runtime_support` layer if you want the core runtime file to keep shrinking.

## 2026-06-02
- changed: split the remaining `netapp`, `sensor`, and `vmim` manager bodies into `src/runtime/emulator_manager_netapp.c`, `src/runtime/emulator_manager_sensor.c`, and `src/runtime/emulator_manager_vmim.c`; `src/runtime/emulator_runtime.c` now dispatches those three families through thin wrappers, and the sensor-manager log state moved into its dedicated module.
- verified: rebuilt successfully with `make` after wiring the three new manager objects into `Makefile` and extending `src/runtime/emulator_runtime_internal.h` with the new manager entry declarations.
- evidence: `src/runtime/emulator_runtime.c` dropped to 5645 lines, while the extracted manager modules now carry 35 lines for netapp, 43 lines for sensor, and 57 lines for vmim, and the link step still produced `bin/main.exe`.
- next: the remaining large work in `src/runtime/emulator_runtime.c` is now much more runtime-core oriented; the next useful split is likely a second pass on shared support code such as manager stub helpers, scheduler/network support, or post-preload tracing support.
