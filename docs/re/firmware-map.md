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
