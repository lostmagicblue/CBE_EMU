# Firmware Map

Use this file for durable facts about firmware layout and extraction results.

## Known IDA Workspaces

These are the primary analysis entry points for this repository:

### Firmware import table and platform-side implementations

- status: `confirmed`
- source: IDA window whose filename starts with `8533n_7835`
- purpose: inspect the game firmware's imported function table, platform entry points, and related implementation code needed by the emulator
- usage note: start here when you need to map an imported interface used by `江湖OL.CBE` back to firmware-side behavior

### Main game binary

- status: `confirmed`
- source: IDA window for `江湖OL.CBE`
- purpose: inspect the game's main CBE disassembly, request builders, response parsers, state transitions, and runtime expectations
- usage note: start here for gameplay logic, protocol handlers, and crash-path tracing inside the main game payload

### Title/login module

- status: `confirmed`
- source: IDA window for `mmTitleMstarWqvga.cbm`
- purpose: inspect the dynamic title/login module that drives login UI, login submit logic, and post-login transitions
- usage note: this is the first place to check when login succeeds but the game crashes during or immediately after the loading transition

Suggested entry format:

```text
## Component name
- status: confirmed | hypothesis
- source: firmware path or container name
- offsets: relevant file or memory offsets
- purpose: loader, VM blob, asset pack, network code, etc.
- evidence: strings, xrefs, logs, runtime checks
```

## CBE embedded DF_DataPackage

- status: `confirmed`
- source: `bin/CBE/江湖OL.CBE`
- offsets:
  - fixed CBE header is `0x98` bytes with `0xFE*8 + be32` interval markers
  - code/data offsets are derived exactly as in `src/cbeParser.c`
  - after code/BSS/RW segments, one big-endian `DF_DataPacakge_Size` field is followed by the embedded package at `DF_Data_Pacakage_Offset`
- purpose: carry the startup/UI resources embedded directly in the main CBE
- format:
  - top-level package starts with `u32le headerSize`
  - header body starts with `u32le rootMarker` and `u32le packageCount`
  - package `0` is the root block immediately after the header; later packages are `len+name` plus `u32le relativeOffset` from package start
  - each package block starts with `u32le blockSize`, then a block body of `u32le dataSize`, `u32le itemCount`, `itemCount * u32le itemOffsets`, `itemCount * (u8 len + gbk bytes itemName)`, followed by `dataSize` bytes of concatenated resource bodies
- evidence:
  - `src/cbeParser.c` for CBE segment and embedded-package offsets
  - `src/vmFunc.c:1373-1620` (`vm_DF_DataPackage_LoadFormTCardEx`) for the standalone/in-file package layout
  - `src/vmFunc.c:2164-2247` for resource lookup by id/name against the same offset and name tables
  - local parser validation against `bin/CBE/江湖OL.CBE`, which currently yields `2` package blocks and `16` embedded resources

## XSE scene/NPC script resource

- status: `confirmed`
- source: `bin/JHOnlineData/*.xse`
- purpose: NPC dialog / quest / scene interaction script resource referenced from `.sce` actor records
- outer wrapper:
  - ordinary loose `.xse` files use the same `type=2` resource wrapper as `.sce` / `.map` / ordinary `.actor`
- inner payload:
  - starts with `XSE0`
  - followed by a bytecode/metadata region
  - then a `u32le-count + (u32le-len + gbk-bytes)` text pool
  - then a function table with entries `(u32 codeOffsetWords, u32 reservedZero, 0x00 + len + ascii functionName)`
  - then a command table `u32le-count + (len + ascii commandName)`
- confirmed function names:
  - every current loose sample exports `_MAIN`
  - most quest/dialog scripts also export `INIT` and `DOTASK`
- confirmed host-command names:
  - `SHOWDIALOG`, `INITTASK`, `GETCURRENTTASKID`, `GETTASKSTATE`, `ADDTASK`, `TOCONTINUE`, `SETTASKSTATE`, `REMOVETASK`, `ADDTASKOPTION`, `CHECKTASK`, and rare `GIVEITEM`
- bytecode findings:
  - `codeOffset` values are indexed in logical 32-bit words from the body start, not in raw bytes
  - confirmed opcode families currently visible in loose scripts:
    - `0x1A`: load/push operand
    - `0x1E`: call host command by zero-based local command-table index
    - `0x20`: end / return
    - `0x13` and `0x14`: control-flow nodes with target-like immediates
    - `0x1B` and `0x1D`: comparison / branch-structure nodes used around task-state decisions
  - high-confidence `0x1A` operand modes:
    - mode `0`: integer literal
    - mode `2`: text-pool string reference
    - mode `3`: local variable / state-slot reference
    - mode `8`: temporary / last-result slot reference
- evidence:
  - local payload inspection over all `149` current loose `.xse` samples
  - structural recovery captured in `tools/inspect_xse.py`
  - direct correlation between `0x1E ... operand=N` and per-file command tables, e.g. pure-dialog `_MAIN` scripts reduce to `push string -> call command[0]=SHOWDIALOG -> return`
