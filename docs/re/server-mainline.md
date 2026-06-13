# Server Mainline

This is the short entry point for turning the current Jianghu OL mock behavior into a real
private-server implementation.

## Goal

Build a server that satisfies the unmodified client/runtime contract. The emulator may expose
platform behavior and traces, but server progress should come from recovered request/response
contracts, not from forcing guest game state.

## Current Source Of Truth

- `src/mock-server.c` contains the active embedded mock server and WT packet builders.
  It is currently included by `src/main.c` so the mock can still access emulator-local static
  helpers and runtime globals without changing linkage.
- `docs/re/battle-mainline.md` is the current battle parser/state model. Use it before changing
  battle packet behavior.
- `docs/re/protocol.md` is the complete protocol notebook, but it is long and includes historical
  dead ends. Use it for evidence lookup, not as the first thing to reread.
- `docs/re/session-log.md` tail records the latest confirmed experiment and next validation.
- `docs/net_mock_protocol.md` is a compact historical baseline for early startup/update/login.
- `server/` is still a target area for extracted backend code and protocol tests.

## Server-First Workflow

1. Start from the latest `docs/re/session-log.md` entry and this file.
2. Identify the next client request from `bin/logs/net_packets.log` or `unhandled_packet`.
3. Map that request to the smallest relevant builder in `src/mock-server.c`.
4. Recover the parser contract from static xrefs, trace-only probes, or packet evidence.
5. Update the embedded mock only enough to validate the contract.
6. Promote confirmed packet shapes into `docs/re/protocol.md`.
7. When a flow is stable, extract the builder shape into `server/` tests or implementation.

Do not start by broad-reading all of `protocol.md` or by rewriting `server/`. The fastest path is
one request, one parser, one validated response.

## Current Mainline State

Confirmed working areas:

- WT framing and object field encoding helpers exist in `src/mock-server.c`.
- startup version/update responses are implemented.
- login and role-entry responses are implemented enough to reach game flow.
- scene/resource/movement handling has many parser-safe built-ins and route-specific fixes.
- battle entry can populate an enemy template before `4/10` battle start.

Current active blocker:

- Battle operate `4/2` replies are parser-safe, and type-1 action records reach
  `HandleBattleActionMsg()`, but the client's local Battle.cbm fighter HP still remains unchanged.
- Latest evidence shows `valueA/valueB=12/8` and `8/0` materialize, while canonical enemy HP stays
  `20/20` and pending deltas stay `0,0`.
- The next useful check is the read-only `trace_battle_anim_effect_delta_detail` windows for
  `DrawBattleAnimEffect`, `sub_4582`, and `sub_4B38` state-4 HP merge.
- Battle-specific packet experiments should report which `docs/re/battle-mainline.md` gate they
  advance or reject.

## Migration Boundary

Keep `src/mock-server.c` as the live lab until a packet family is confirmed. Move code toward
`server/` only after the request trigger, object sequence, field grammar, and visible/runtime side
effects are all known enough to write a deterministic test.

When extracting to `server/`, preserve the same builder semantics as the confirmed mock and add
fixtures from packet logs where practical.
