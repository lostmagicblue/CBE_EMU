# 2026-06-26 Title Create Role Input

## Symptom

On the title-side create-role screen, tapping the rectangle after the `name`
label does not immediately enter the host input overlay.

## IDA Evidence

Module: `mmTitleMstarWqvga.cbm`.

### Touch Handler

`role_manage_screen_handle_input(0x42B8)` handles the create-role form when
`R9 + 10748 == 1`.

It checks three form rows:

```text
row 0: sex
row 1: job
row 2: name
```

For a row hit:

- if `R9 + 11108 != row`, the handler only selects that row:

```text
R9 + 11108 = row
R9 + 10754 = row + 1
```

- if the selected row is already row 2, it still requires
  `*(u8 *)(R9 + 0x2000) == 1`; otherwise it jumps past the input action.
- when the name buffer is empty and the flag is set, it dispatches action
  `0x1000`.

### Action Dispatch

`role_manage_screen_dispatch_mode_input(0x4290)` sends mode `1` input to
`role_manage_screen_handle_action_menu_input(0x4016)`.

In `role_manage_screen_handle_action_menu_input(0x4016)`:

```text
case 0x1000:
  if (*(u8 *)(R9 + 0x2000) == 1 && selectedRow == 2) {
    *(u8 *)(R9 + 0x2000) = 0;
    clear current name buffer;
    login_form_open_editor(4);
  }
```

So a tap on the name rectangle is not an unconditional focus event. It is part
of an old feature-phone action menu:

1. select the row
2. dispatch the input/confirm softkey action
3. open the editor only when the local edit-enable flag is set

### Host Input Overlay

`login_form_open_editor(0x1FC8)` selector `4` opens the role-name input editor
with callback `sub_5A30`.

The emulator side supports this path:

- `src/main.c:vm_input_open()` stores the callback, buffer, max length, and
  draws an input overlay.
- SDL key events then enqueue `VM_EVENT_INPUT_CHAR`, `VM_EVENT_INPUT_BACKSPACE`,
  or `VM_EVENT_INPUT_DONE`.

Therefore the current symptom is before text entry: the client has not reached
`login_form_open_editor(4)`.

## Current Cause

The tap target is a feature-phone menu row, not a direct text-box focus widget.
The first tap on the name row can only select it. Opening input requires a
second input/confirm action and is additionally blocked when `R9 + 0x2000` is
zero.

## Follow-Up Options

- Preserve client behavior: use the left softkey `input` action after selecting
  the name row.
- Improve emulator UX: add a narrow host-side tap helper that, only on the title
  create-role screen and only inside the name field rectangle, synthesizes the
  second normal input action after the row-select tap. This avoids patching CBE
  code or writing CBE globals.
- Avoid forcing `R9 + 0x2000`; that would be a client-state shortcut rather than
  a protocol or input-path fix.
