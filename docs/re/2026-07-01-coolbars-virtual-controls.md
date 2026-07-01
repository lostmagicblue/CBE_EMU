# CoolBars virtual controls

## Symptom

Battle screens can show a large virtual direction pad plus two round buttons. Hiding those images in the draw path leaves black damaged regions, because the overlay is part of the CoolBars/touch-panel layer and must be disabled before it is created.

## Evidence

- Main battle modules draw the RPG battle UI, but the large direction pad is not part of the battle menu drawing path.
- Jianghu OL CBE ends with a 44-byte CoolBars footer. The final 12 bytes are a big-endian footer offset followed by `CoolBars`.
- For `bin/CBE/江湖OL.CBE`, the footer starts at `0xCBAFD`. Bytes at `footer + 0x14` are `03 0E 05 00`.
- Existing reverse notes around `模拟器输出日志.txt` describe this footer region as the CoolBars platform/version descriptor; many keyboard-style CBE games use `02 0E 05 00` at the same position.
- The same trace file shows the virtual layer is created through CoolBars touch-screen window flow: `CoolbarGame_InitTouchPanel`, `vmOpenTScreenWinScr`, and repeated `TryChangeSubTScreen` / `vmScreenPatchUpdate` logs.

## Host-side handling

Do not hide this by filtering draw calls. The draw-filter attempt left black damaged rectangles because the overlay is a platform sub-screen, not normal battle UI.

Do not rewrite the CBE footer. Normalizing the first footer byte from `03` to `02` did not remove the virtual controls and is now treated as negative evidence.

The host must expose the same platform profile that the Jianghu OL package declares:

- `GetCurrentScreenType` returns `0x0e`.
- `GetCurrentPlatformType` returns `5`.

This keeps the fix at the platform capability boundary. It does not filter draw calls, patch game code, or write CBE globals.

## 2026-07-01 Runtime Evidence

Manual battle entry produced three visible CoolBars controls through the main CBE picture library:

- dpad: `DrawPicLibSpriteAlpha -> BlitSpriteClippedAlpha`, destination `(8,226,102,102)`.
- button A: destination `(160,279,40,40)`.
- button B: destination `(200,236,40,40)`.

All three use `piclib = Global_R9 + 0x2838`, so they are not SDL host overlays and are not a battle-module bitmap. The stack includes `0x010059AF` (`DrawPicLibSpriteAlpha`) and mixed main-CBE / mmBattle addresses, but stack candidates alone are not enough to identify the real caller because stale return addresses remain on the stack.

The first entry probe at `0x010059AE` did not emit useful output even though the stack consistently included `0x010059AF`, so the probe was moved to the stable in-function call sites:

- `0x010059EA`: clipped-alpha branch, immediately before `BlitSpriteClippedAlpha`.
- `0x01005A06`: direct-to-screen alpha branch, immediately before `BlitSpriteAlphaToScreen`.

At those points `R5` is the sprite index, `R6/R7` are the final destination coordinates, and `SP+0x24` is the saved LR for the caller of `DrawPicLibSpriteAlpha`. The next manual run should emit:

`[info][coolbar] draw_piclib_alpha_callsite part=... pc=... sprite=... x=... y=... caller=...`

Use `caller` from this trace as the authoritative draw caller for the next IDA pass. Remove or gate the exact-coordinate trace after the control flag is identified.

## 2026-07-01 Touch-Panel Capability Test

The draw caller trace proved the three controls are rendered from the main CBE CoolBars picture library during battle rendering:

- dpad: `pc=01005a06`, `sprite=12`, `x=8`, `y=226`, `caller=05184017`.
- button A: `pc=01005a06`, `sprite=11`, `x=160`, `y=279`, `caller=05184031`.
- button B: `pc=01005a06`, `sprite=10`, `x=200`, `y=236`, `caller=05184049`.

The next host-side test is `vMIsSupportTP`. This is a platform capability query, so returning false is preferable to filtering the later PicLib draws. The emulator now returns `0` for `vMIsSupportTP` and logs one line:

`[info][coolbar] vMIsSupportTP result=0 caller=...`

Manual validation should check whether this line appears before battle and whether the later `draw_piclib_alpha_callsite` / `virtual_control_visible` lines disappear. If the controls still render after `vMIsSupportTP result=0`, the touch-panel capability is not the controlling flag and the next target is the CoolBars virtual-key initialization/configuration path.

Manual validation showed no `vMIsSupportTP result=0` line before the controls rendered, so this capability query is not on the active path.

## 2026-07-01 Battle Module Control Flag

IDA evidence from `mmBattleMstarWqvga.cbm` identifies the exact virtual-control draw and its gate:

- `sub_20D8` draws the three virtual controls. It calls the battle-local picture/control object at `R9+0x2838+0x40`, method `+0x38`.
- The three sprite ids are read from `R9+0x2838+0x18`, `+0x16`, and `+0x14`.
- The three draw positions are hardcoded: dpad `(8,226)`, button A `(160,279)`, button B `(200,236)`.
- `BattleScene_DrawMain(0x5444)` gates the draw at `0x5DF8`: it reads `R9+0x283E` and calls `sub_20D8` only when that byte is nonzero.
- `LoadBattleSprite(0x212A)` initializes that byte at `0x22E8` from the return value of a main-CBE callback reached through `*(R9+0x2050) + 0x400 + 0x14`.

The emulator now logs this initialization boundary without changing it:

`[info][coolbar] battle_virtual_control_cap_call ... target=...`

`[info][coolbar] battle_virtual_control_cap_result ... result=...`

Use `target` to identify the main-CBE callback that supplies the virtual-control visibility flag. The fix should be at that callback/capability boundary, not by writing `R9+0x283E` or filtering `sub_20D8` draws.

## 2026-07-01 R9 Boundary Note

Runtime evidence showed `battle_virtual_control_screen ... code=05174000 battleR9=05188000 sourceR9=05188000`, which confirms the inferred battle module R9 is correct for reading battle-local fields such as `R9+0x283E`.

Do not use that inferred R9 as the screen stack module base or force it for every instruction in the battle code range. That experiment crashed at `pc=05184110` (`DrawBattleMenu`, offset `0x10110`) with `r9=05188000`, because the battle render path mixes battle-module code with main-CBE PicLib/interface callbacks. Keep execution R9 restoration on the existing screen/module path; use the inferred battle R9 only as read-only evidence for the control flag.

Manual testing after reverting the execution-R9 override produced only the battle-screen line and no `0x22E8` / `0x5DF8` traces. That means the old target may be inactive for the visible controls in this build/run phase. The next narrow evidence pass logs:

- `battle_virtual_control_path point=sub20d8-entry/sub20d8-draw` for the IDA-confirmed `sub_20D8` path.
- `battle_virtual_control_path point=draw-menu-candidate` for offsets previously seen as stack candidates.
- `virtual_control_visible ... stack=...(+xxxxx)` so stack entries can be matched to battle-module offsets instead of raw pool addresses.

Manual testing hit `draw-menu-candidate pc=05184016 off=10016 battleR9=05188000 curR9=01050bd0`. IDA shows this is inside `DrawBattleMenu` and uses the main CBE drawing context, so the earlier `caller=05184017` stack value is a normal battle menu draw candidate, not the virtual-control gate. The next evidence point is the lower-level main-CBE `BlitSpriteClippedAlpha` callsite `0x01004220`, where clipped `w/h/dstX/dstY` are available on the stack and can be matched to the three fixed virtual-control rectangles.

Manual testing after adding the low-blit rectangle matcher still produced only the battle-screen line and the `draw-menu-candidate` line. The next pass removes the battle-only condition from `low_blit_raw`, logs the first 24 hits of `0x01004220/0x01004246`, and adds one-shot platform capability logs for `vMGetKeyNum`, `GetCurrentScreenType`, and `GetCurrentPlatformType`. It also logs up to 12 `battle_tscreen_frame` entries after battle screen detection to determine whether the controls are drawn by a separate TScreen/CoolBars overlay instead of the main PicLib path.

The first raw low-blit evidence showed small `24x24` icons around `(44,188)`, not the virtual controls. At `0x01004220/0x01004246`, the decoded rectangle is `w=SP+4`, `h=SP+0`, `x=SP+8`, `y=SP+12`. The next matcher therefore logs `low_blit_region` only when the decoded rectangle overlaps the known virtual-control regions instead of consuming a fixed first-N raw budget.

Runtime evidence then showed `vMGetKeyNum result=33 caller=010004a0` before the visible virtual controls, and no `battle_tscreen_frame`. The visible controls still came through `DrawPicLibSpriteAlpha` with `sprite=10/11/12`, fixed coordinates, and main `r9=01050bd0`. The next host-side capability test changes `vMGetKeyNum` to return `255`, which is the main CBE fallback value when the platform vtable slot is absent. This tests whether CoolBars creates the virtual key overlay from the reported key table size, without filtering draw calls or writing CBE globals.
