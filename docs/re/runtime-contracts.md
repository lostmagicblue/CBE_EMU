# Runtime Contracts

Use this file for client-facing platform behavior that the emulator must honor.

Examples:

- screen stack semantics
- timer behavior
- file I/O expectations
- VM callback/event ordering
- socket lifecycle behavior
- text/input/lcd quirks

Each entry should describe the contract and how it was observed.

## Network Mocking Principles

- let the game code advance itself; the emulator should only implement platform/API semantics and server-like network responses
- do not mutate CBE globals directly from the network mock
- do not force CBE function return values to push progress
- temporary read-only logging is acceptable while validating a path, but remove hooks that change game state after the behavior is confirmed
- when manually validating startup/login flows, launch `bin/main.exe` with `E:\DevOs\CBE_EMU\bin` as the working directory and drive clicks manually

## Screen Stack Removal/Resume

Confirmed screen-manager contract:

- when the update screen calls screen-manager idx `6` to remove itself, the emulator must resume the new top-of-stack screen underneath it
- this is required for the startup/update flow to continue naturally after the update/version callback chain finishes

## Dynamic CBM Module Context

Confirmed contract for pool/dynamic screens:

- when a dynamic CBM screen calls `vmAddScreen` from pool code, the emulator must preserve the call-time `R9` / module base
- when that screen later becomes active again, the emulator must restore the same module context before calling its entries

Without this, lower-screen resume and dynamic-title/login transitions will run under the wrong module state.

## Nullable Screen Entrypoints

Confirmed screen lifecycle behavior:

- `init`
- `resourceLoad`
- `destroy`

may legitimately be `0` in a screen function table.

The emulator must treat null screen entries as no-op callbacks and skip them without failing the lifecycle.

## Pool Screen Idle Logic

Current contract is still intentionally conservative:

- the simulator should not synthesize idle/inputless logic events for pool screens yet
- the no-input logic tick semantics for pool screens are still unconfirmed

Future work should confirm from logs whether the CBE or firmware provides a dedicated idle event path for those screens.
