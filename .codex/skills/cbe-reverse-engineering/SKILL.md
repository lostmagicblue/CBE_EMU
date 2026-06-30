---
name: cbe-reverse-engineering
description: Reverse engineer CBE feature-phone games and build request-driven server/mock support for Jianghu OL or similar RPG CBE titles. Use when working with IDA-opened .CBE/.cbm ARM binaries, emulator network hooks, WT packets, mock-server responses, protocol reconstruction, or gameplay progression that must be driven by response packets instead of patching client logic or forcing CBE global variables.
---

# CBE Reverse Engineering

## Operating rule

Build server behavior around the client contract. Let the CBE advance because it parses valid response packets and receives normal network events.

Do not patch game logic, bypass branch conditions, or force gameplay by writing CBE globals such as `Global_R9` offsets, scene gates, battle state, role state, or screen-manager internals. Do not add one-off register/memory writes to "unstick" the client. If state changes are needed, discover the packet field or event order that naturally causes the client to perform them.

Allowed host-side writes are limited to emulator framework mechanics already required for I/O: allocating/copying response buffers with `vm_malloc`/`uc_mem_write`, queueing scheduler network events, and read-only tracing/instrumentation. Treat existing compatibility shims as legacy surface; do not expand them as a solution pattern.

Runtime mock behavior must not depend on reverse-engineering exports such as `tmp/all_sce_bundle/*/scene.json`. Use those exports only as human reference material. Implement data-driven behavior from client request packets, real game resources such as SCE/DSH files, and IDA parser evidence.

## First pass

1. Inspect local context before guessing:
   - `src/mock-server.c` for request detectors, packet builders, and response dispatch order.
   - `src/main.c` around network manager hooks, `scheduler_queue_net_event`, `scheduler_dispatch_net_tasks`, and trace helpers.
   - `logs/net_trace.log`, packet logs, and any unhandled packet dumps after running the emulator.
   - `docs/re/` and `当前工作记录.txt` for prior protocol notes.
2. Identify the exact boundary:
   - Incoming request bytes from `vm_net_mock_on_send`.
   - Existing detector or missing detector in `vm_net_mock_build_response`.
   - Callback/event type expected by the client, usually queued as normal data event `7`.
   - Client parser function in IDA that consumes the response.
3. State the current hypothesis with evidence. Prefer `runtime trace + request bytes + IDA parser path` over a single observation.

## IDA workflow

Use IDA MCP when IDA is open. Start with `list_instances`; for Jianghu OL the active CBE instance is the one whose `binary_name` is `江湖OL.CBE`. Instance IDs change, so do not hardcode them in documentation or scripts.

Use compact calls first:
   - `analyze_function` for one parser or request builder.
   - `analyze_batch` for a small set of related functions.
   - `decompile` only after choosing a function.
   - `decompile_to_file` only for a bounded set of addresses, not whole binaries unless explicitly needed.

Do not bulk-decompile huge binaries as a first move. Follow xrefs from strings, WT kind/subtype dispatch, packet field names, callback addresses, and trace PCs.

## Packet-driven implementation pattern

When adding server behavior:

1. Add a narrow detector near related handlers in `vm_net_mock_build_response`.
   - Match WT header kind/subtype when available.
   - Also match stable semantic fields, object counts, or request body markers to avoid hijacking unrelated packets.
2. Add a named builder:
   - Name it by client phase and packet contract, for example `vm_net_mock_build_scene_resource_followup_response`.
   - Use existing helpers for WT headers, objects, typed fields, sequence fields, strings, blobs, and length finalization.
   - Keep packet construction deterministic and bounds-checked.
3. Log the evidence:
   - Emit a `vm_net_trace` line with source name, key fields, length, and parser evidence.
   - Call `vm_net_log_handled_packet` for handled requests.
   - Let unhandled cases reach `vm_net_log_unhandled_packet` while investigating.
4. Queue response through existing network flow:
   - Return bytes from `vm_net_mock_build_response`.
   - Let `vm_net_mock_on_send` allocate/copy the response and call `scheduler_queue_net_event`.
   - Do not call game callbacks directly unless the existing scheduler path cannot represent the real event.

## Reverse engineering checklist

For each newly supported phase, document:

- Request signature: WT header, object kinds/subtypes, key fields, sample length.
- Response contract: top-level WT header, objects, typed fields, arrays, strings, blob lengths.
- Client parser evidence: IDA function address/name, dispatch case, relevant field reads, failure branch.
- Runtime evidence: trace line, packet dump name, screen/battle/menu transition produced by the client.
- Negative evidence: what malformed or missing field caused an assert, stall, or wrong screen.

## Debugging rules

Prefer read-only observation:

- Add temporary trace helpers that read registers/memory and log.
- Add watch logs for writes only to explain what the client did, not to change it.
- Use GDB breakpoints/watchpoints for investigation.
- Remove or gate noisy tracing after the packet contract is understood.

Avoid these shortcuts:

- Writing `Global_R9 + offset` to flip a gate.
- Changing PC/LR/R0 return values to skip parser failures.
- Patching CBE code bytes.
- Injecting screen changes, battle outcomes, or role state directly.
- Making broad packet detectors that return a "safe" generic packet without parser evidence.

## Validation

After edits:

1. Build with the repo's existing command, usually `make`.
2. Run the emulator from `bin` so relative assets and `CBE/江湖OL.CBE` resolve.
3. Confirm trace progression:
   - Request is detected by the intended source.
   - Response length is nonzero and logged.
   - Scheduler queues a normal data event.
   - Client reaches the next screen/state without forced memory writes.
4. If the emulator asserts, preserve the unhandled packet and trace context, then return to IDA parser evidence.

## Reporting

When reporting results, include:

- Files changed and phase supported.
- The exact packet/source name added.
- The IDA and runtime evidence used.
- Build/run status.
- Any remaining uncertain fields or placeholders.
