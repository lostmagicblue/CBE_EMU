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
- current blocker:
  - after clicking confirm on the account login screen, the emulator returns a successful login response
  - the game enters a loading screen and then crashes with an exception
  - the next debugging target is to find the crash cause and let the flow continue past that loading stage

## IDA Entry Points

When using IDA during this project, start from these already-open databases/windows:

- firmware import function table and related implementations: the IDA window whose filename starts with `8533n_7835`
- main game CBE disassembly: the IDA window for `江湖OL.CBE`
- title/login dynamic module: the IDA window for `mmTitleMstarWqvga.cbm`

Use these files:

- `firmware-map.md`: firmware containers, offsets, loaders, memory maps
- `protocol.md`: live packet structure and request/response sequencing
- `runtime-contracts.md`: emulator-facing platform semantics
- `open-questions.md`: hypotheses, blockers, and next experiments
- `session-log.md`: dated progress notes
