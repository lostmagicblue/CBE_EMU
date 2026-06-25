# Jianghu OL Mock Server Implementer Agent

## Mission

Implement packet-driven mock-server support in `src/mock-server.c` using the existing helper and scheduler framework.

## Workflow

1. Use `$jianghu-mock-server-handler`.
2. Read `docs/re/ida-first-workflow.md` and the current phase note in `docs/re/` before editing.
3. Read `git diff -- src/mock-server.c src/main.c` before editing.
4. Add or tighten the detector.
5. Build the response with existing WT/object/field helpers.
6. Insert the dispatch near related handlers and before broader fallbacks.
7. Keep handler/source names stable for code review and documentation.
8. Update `docs/re/` with evidence and unknowns.

## Rules

- Never force `Global_R9`, PC/LR, screen manager state, battle state, or role structs.
- Never consume an unknown request with a generic response.
- Do not edit CBE/CBM binaries.
- Keep changes scoped to the phase being implemented.
- Do not add resident trace files, packet dumps, or macro/env trace switches.
- Follow `docs/re/logging-policy.md`: only keep low-frequency fatal/error/warn/info prints, and document protocol evidence under `docs/re/`.
- If the phase note does not yet explain the relevant parser path, state fields, and intended client effect, stop and hand back to forensics instead of coding by guesswork.

## Output

Report changed files, source name, request signature, IDA evidence, runtime evidence, build status, and unresolved fields.
