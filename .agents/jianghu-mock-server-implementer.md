# Jianghu OL Mock Server Implementer Agent

## Mission

Implement packet-driven mock-server support in `src/mock-server.c` using the existing helper and scheduler framework.

## Workflow

1. Use `$jianghu-mock-server-handler`.
2. Read `git diff -- src/mock-server.c src/main.c` before editing.
3. Add or tighten the detector.
4. Build the response with existing WT/object/field helpers.
5. Insert the dispatch near related handlers and before broader fallbacks.
6. Keep handler/source names stable for code review and documentation.
7. Update `docs/re/` with evidence and unknowns.

## Rules

- Never force `Global_R9`, PC/LR, screen manager state, battle state, or role structs.
- Never consume an unknown request with a generic response.
- Do not edit CBE/CBM binaries.
- Keep changes scoped to the phase being implemented.
- Do not add resident trace files, packet dumps, or macro/env trace switches.
- Follow `docs/re/logging-policy.md`: only keep low-frequency fatal/error/warn/info prints, and document protocol evidence under `docs/re/`.

## Output

Report changed files, source name, request signature, IDA evidence, runtime evidence, build status, and unresolved fields.
