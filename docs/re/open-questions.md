# Open Questions

Track unresolved work here so future Codex sessions can pick it up quickly.

## Post-Login Loading Crash

- status: open
- area: runtime | protocol
- current belief: the emulator can already return a successful login response, but something required after the login-success transition is still missing or wrong; likely candidates are a still-stubbed interface, a bad runtime contract during screen/module transition, or a missing/incorrect follow-up packet for the loading stage
- confirmed update:
  - a prior emulator hook that directly rewrote scene tile table entries to backfill tiles `25/26`, plus a paired "skip missing tile draw" return hook, has now been removed from `src/runtime/emulator_runtime.c` because it was mutating in-game state to push progress.
  - because of that, older evidence that depended on reaching `scene_draw_status_panels` with valid tile `25/26` must now be treated as historical evidence from a modified runtime, not as proof of the unmodified path.
- confirmed evidence:
  - after removing the progress-forcing scene hook and reproducing from a clean runtime, the first real blocker moved earlier than scene/status drawing: at `tick=1`, main CBE resource init calls `vmDfDataPackageManager` entry `idx=0` twice from the `0x0103B9FC` path, and the earlier stubbed behavior caused an immediate client-side abort.
  - wiring `vmDfDataPackageManager idx=0` to the existing `vm_DF_DataPackage_LoadPackage()` implementation removed that first blocker; the next exposed missing entry is `idx=4`, reported by the runtime as `[impl]vmDfDataPackageManager调用位置:4`.
  - `src/vmFunc.c` already contains a matching `VM_DF_DataPackage_DoLoading()` implementation for `idx=4`, and the runtime hook now routes `idx=4` to that function as the next minimal confirmed fix.
  - after wiring `vmDfDataPackageManager idx=0/4`, the client now gets far enough to display the loading/version UI and only aborts later at `tick=66`, right when queued network event `5` fires into callback `0x0103489B` (`net_wrapper_event_dispatch`).
  - the final `lastAddress` before that abort is `0x01034524`, which is inside `sub_1034518`, a packet-field/event-packet initializer used by `event_packet_add_field` and `event_packet_parse_WT`, not a scene/status draw site.
  - `vm_initMemoryBlock()` in `src/vmFunc.c` documents that `VM_MF_MemoryBlock_FUNC_LIST_ADDRESS` slot `0/1/2` correspond to `MF_MemoryBlock_Malloc/Reset/Release`; however `hook_vm_mf_memoryblock_func()` had still been stubbing the entire `vmMfMemoryBlockManager` table.
- confirmed code change:
  - `src/runtime/emulator_runtime.c` now routes `vmMfMemoryBlockManager idx=0/1/2` to the existing `vm_MF_MemoryBlock_Malloc()`, `vm_MF_MemoryBlock_Reset()`, and `vm_MF_MemoryBlock_Release()` implementations, with trace logs added for each call.
- existing `bin/net_trace.log` shows the login-success path does advance past `mmTitleMstarWqvga.cbm` into the main CBE loading/scene path: after the login actor response, the title screen removes itself, the runtime resumes the lower screen, and a new dynamic scene/loading screen is pushed (`01053f78`) with the correct pool-module `R9` saved earlier.
- existing `bin/net_trace.log` also shows the first post-login follow-up requests are not absent: the client sends three `type=1/2/3` game packets on connect `2`, and the current mock returns built-in responses of lengths `129/34/36` before the crash.
- in an older modified-runtime trace, the path reached `scene_runtime_tick` (`0x0101517A`) and `scene_draw_status_panels` (`0x0101466A`) before failing with a client-side `SIGABRT` through `sub_104D538 -> sub_104E7C8 -> sub_104EA2C`.
- refined hypothesis:
  - the post-login loading path depends on the DF_DataPackage manager contract much earlier than the old modified-runtime trace suggested; more missing manager entries may need to be reconnected to the existing `src/vmFunc.c` implementations before the client can reach scene/status drawing on an unmodified runtime.
  - the current `tick=66` abort is most likely caused by incomplete `vmMfMemoryBlockManager` semantics during event-packet allocation or reset, because the guest reaches `sub_1034518 -> sub_103496C/0x103498C` right before the crash and those helpers dispatch through the memory-block function table that had been stubbed.
  - after removing the progress-forcing hook, the true unmodified blocker may reappear earlier than `scene_draw_status_panels`; this needs a fresh reproduction.
  - if the path still reaches `scene_draw_status_panels`, the most likely immediate cause remains a divide-by-zero or similarly uninitialized status-panel field during one of the two ratio calculations at `0x0101477E` or `0x010147AE`.
- missing evidence:
  - on the unmodified runtime, what is now the first failing PC/LR after login success
  - which exact denominator/value pair is zero at the failing `scene_draw_status_panels` divide site
  - which scene/status structure owns that zero field, and whether it is supposed to come from login `actorinfo`, later scene sync packets, or local scene initialization
  - whether the current built-in `type=1` response is semantically incomplete for this loading stage
- next experiment:
  - rerun with the freshly rebuilt binary that now also implements `vmMfMemoryBlockManager idx=0/1/2`, then capture whether event `5` proceeds into a version-request send path or exposes the next missing manager/interface.
  - rerun with the freshly rebuilt binary that now implements `vmDfDataPackageManager idx=0` and `idx=4`, then capture the next missing DF_DataPackage entry or the first non-DataPackage failure after those two calls are satisfied.
  - reproduce after removing the tile backfill / missing-tile skip hook and capture the first failing PC/LR plus surrounding screen/network logs
  - only if the unmodified path still reaches the status-panel divide sites, use the new minimal divisor trace in `src/runtime/emulator_runtime.c` to map the zero denominator back to the owning scene/status structure
  - if the failure comes from network-fed state, extend the mock with the minimum confirmed field needed rather than forcing the scene forward

Suggested entry format for additional questions:

```text
## Question
- status: open
- area: firmware | protocol | runtime | server | assets
- current belief:
- missing evidence:
- next experiment:
```
