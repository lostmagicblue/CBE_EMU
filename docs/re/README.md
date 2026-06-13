# Reverse Engineering Docs

This directory is the durable memory for reverse-engineering work in this repository.

## Project Snapshot

- primary goal 1: implement all interfaces required to simulate and run `江湖OL.CBE`
- primary goal 2: emulate all packet responses required for the `江湖OL.CBE` gameplay flow, effectively replacing the original game server
- current progress:
  - most emulator interfaces are implemented
  - some interfaces are still `stub` or fully unimplemented
  - version-check response is implemented
  - account-login response is implemented
  - the active server behavior is now isolated in `src/mock-server.c`, still included by `src/main.c`
  - `server/` is reserved for extracted backend code and protocol tests once a packet family is stable
- current blocker:
  - the old post-login loading/crash blocker is historical; current work has moved later into scene and battle flow
  - the latest active blocker is battle operate semantics: `4/2 -> 1/4/6` action records parse and display damage, but Battle.cbm local fighter HP is not committed
  - the next debugging target is the read-only `trace_battle_anim_effect_delta_detail` path around `DrawBattleAnimEffect` and `sub_4B38`

## IDA Entry Points

When using IDA during this project, start from these already-open databases/windows:

- firmware import function table and related implementations: the IDA window whose filename starts with `8533n_7835`
- main game CBE disassembly: the IDA window for `江湖OL.CBE`
- title/login dynamic module: the IDA window for `mmTitleMstarWqvga.cbm`

Use these files:

- `firmware-map.md`: firmware containers, offsets, loaders, memory maps
- `protocol.md`: live packet structure and request/response sequencing
- `battle-mainline.md`: current battle parser/state model and experiment gates
- `server-mainline.md`: short current entry point for private-server implementation work
- `runtime-contracts.md`: emulator-facing platform semantics
- `open-questions.md`: hypotheses, blockers, and next experiments
- `session-log.md`: dated progress notes
