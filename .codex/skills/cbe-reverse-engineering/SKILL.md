---
name: cbe-reverse-engineering
description: Use when working on this repository's CBE firmware analysis, emulator behavior, packet/protocol recovery, Jianghu OL network mocking, or future private-server implementation. Covers how to inspect evidence, where to record findings, and how to turn reverse-engineering results into emulator or server code changes.
---

# CBE Reverse Engineering

Use this skill for any task involving:

- firmware inspection and unpacking
- CBE or VM code analysis
- protocol reconstruction
- emulator runtime behavior in `src/`
- future server work in `server/`
- current server mainline in `docs/re/server-mainline.md`

## First Pass

1. Read `AGENTS.md`.
2. Read `docs/re/README.md`.
3. For private-server or network mock work, read `docs/re/server-mainline.md` and the tail of
   `docs/re/session-log.md` before opening long protocol notes.
4. Read only the most relevant reverse-engineering note:
   - battle work: `docs/re/battle-mainline.md`
   - protocol work: `docs/net_mock_protocol.md` and `docs/re/protocol.md`
   - runtime behavior: `docs/re/runtime-contracts.md`
   - firmware layout: `docs/re/firmware-map.md`
   - unresolved work: `docs/re/open-questions.md`
5. Inspect only the code or tools directly related to the request.

## Working Style

- Preserve a clear boundary between evidence and hypothesis.
- Prefer extracting facts from strings, xrefs, traces, and repeatable runtime behavior.
- Implement the minimum emulator or server behavior needed to validate the next step.
- Reuse existing helpers before adding one-off logic.

## Repository Conventions

- `src/` is the emulator/runtime implementation.
- `tools/` holds repeatable helper scripts.
- `docs/re/` is the durable memory for findings.
- `firmware/` stores firmware-related inputs and manifests.
- `samples/` stores supporting evidence such as captures and screenshots.
- `server/` is reserved for private-server code and protocol harnesses.

## Reverse-Engineering Workflow

For a new problem:

1. locate the narrowest entry point
2. identify data flow and side effects
3. document confirmed findings in `docs/re/`
4. make the smallest code/tooling change needed
5. validate with a rebuild or repeatable run when possible

For server work:

1. start from the current request in packet logs or an unhandled WT summary
2. map it to the smallest `src/mock-server.c` mock builder
3. recover the exact client parser contract before inventing fields
4. validate inside the embedded mock first
5. extract to `server/` only after the packet family is stable enough to test

## Documentation Targets

- packet shape or sequence -> `docs/re/protocol.md`
- battle parser/state model or experiment gates -> `docs/re/battle-mainline.md`
- current server target or extraction boundary -> `docs/re/server-mainline.md`
- emulator/platform semantics -> `docs/re/runtime-contracts.md`
- firmware/container/offset facts -> `docs/re/firmware-map.md`
- unresolved threads -> `docs/re/open-questions.md`
- brief dated progress -> `docs/re/session-log.md`

## Existing Project-Specific Context

- The repository already contains a Unicorn-based CBE emulator in `src/`.
- `docs/net_mock_protocol.md` contains verified Jianghu OL mock-server behavior and should be treated as a primary source.
- `tools/disasm_thumb.py` is the first built-in helper to reuse for Thumb code inspection.

## When To Read References

- Read `references/repo-layout.md` when you need a quick reminder of how artifacts should be organized.
- Read `references/task-recipes.md` when you need concrete workflows for firmware, protocol, runtime, or server tasks.
