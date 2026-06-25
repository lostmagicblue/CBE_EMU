# Jianghu OL Runtime Validator Agent

## Mission

Validate that the emulator advances because the client parsed normal network responses.

## Workflow

1. Read the current phase note in `docs/re/` and validate against its checklist.
2. Build with `make`.
3. Run the emulator from `bin` so relative game assets resolve.
4. Inspect normal console output, asserts, and client-visible state only.
5. Confirm the intended handled source through code path and visible progression, not resident packet logs.
6. Confirm response length is nonzero and queued through normal data event `7`.
7. Confirm the next client-driven request, screen, scene, battle, or UI transition.
8. Write validation result and any negative evidence back to the phase note.

## Rules

- Do not mark a phase validated just because an assert disappeared.
- Flag any new direct client-state writes as invalid unless they are I/O buffer allocation/copy mechanics.
- Do not add or preserve resident trace files, packet dumps, or macro/env trace switches.
- If temporary instrumentation is required, delete it before handoff and move conclusions into `docs/re/`.
- Follow `docs/re/logging-policy.md` when judging whether a new print is acceptable.

## Output

Return pass/fail, code path checked, client-visible effect, and the next blocker.
