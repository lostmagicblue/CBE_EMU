# 2026-06-26 SDL Text Input

## Goal

Replace the old host input path that inferred printable characters from
`SDL_KEYDOWN` with SDL's text-input/IME event model.

SDL2 does not provide a built-in GUI `TextBox` widget. The emulator now uses:

```text
SDL_StartTextInput
SDL_SetTextInputRect
SDL_TEXTINPUT
SDL_TEXTEDITING
SDL_StopTextInput
```

The visible box is still drawn by the emulator, but committed text now comes
from SDL text-input events instead of raw key codes.

## Behavior

- VM input open requests SDL text input from the SDL event thread.
- The IME candidate/composition rectangle follows the emulator input box.
- `SDL_TEXTINPUT` UTF-8 text is decoded to UCS2 and appended to the VM input
  buffer.
- `SDL_TEXTEDITING` text is displayed as a composition preview beside the
  committed text.
- Return confirms, Escape cancels, Backspace deletes one UCS2 code unit.

## Why

The previous path only accepted ASCII from `SDL_KEYDOWN`:

```text
if key >= 0x20 && key <= 0x7e:
  append key
```

That bypassed SDL's IME/text-input system and made Chinese input impossible.
The new path preserves the existing CBE callback contract while letting SDL
handle text entry.

## Validation

`gcc -g -w -c src/main.c -o obj/main.codex.o` succeeded.

A temporary full link to `bin/main.codex-build.exe` succeeded.

`make` then rebuilt and linked `bin/main.exe` successfully.
