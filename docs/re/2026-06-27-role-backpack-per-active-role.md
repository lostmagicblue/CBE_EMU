# 2026-06-27 Role Backpack Follows Active Role

## Problem

Backpack responses still used mock-wide constants: capacity `40`, one
teleport-stone row, and process-global delivery flags. Role selection could
therefore change name/job/position while the backpack still behaved like a
single global inventory. The remaining legacy position mirror also contradicted
the role-row-only state model.

## IDA Evidence

- `mmGameMstarWqvga.cbm:0x418C` handles `1/17/1` by reading raw `iteminfo` as:
  `u8 row_count`, then repeated `u32 itemId` plus the common item-extra block.
  It uses local `item.dsh` / `equip.dsh` for display metadata.
- `JianghuOL.CBE:0x01039952` handles `1/30/21` by reading `result`,
  `gridnum`, and raw `iteminfo` rows: `u32 itemId`, `i16 seq`, `u32 count`,
  then the common item-extra block.
- `mmShopMstarWqvga.cbm:0x9DE` handles buy completion as kind `14` subtype `3`
  and reads `seq` before `result`; on success, the client inserts the item into
  its item manager.

## Implementation

- Role DB version is now `2`.
- Each role row stores:
  - `backpackItemCount`
  - `nextBackpackSeq`
  - `backpackItems[40]` as `itemId/seq/count`
- New roles start with an empty backpack.
- Version-1 role DB files are migrated into version 2 by copying role data and
  keeping an empty backpack for each existing role.
- `17/1` full backpack list and `30/21` grid bootstrap both serialize the active
  role's backpack rows.
- Shop buy `17/2` now adds or stacks `shopId` into the active role backpack
  before returning `14/3 { seq, result }`.
- The old `jhol_mock_player_pos.bin` path, process position cache, and legacy
  mirror writes were removed. Scene position reads/writes now use the active
  role row only.

## Negative Evidence

Do not bring back the old mock-wide hardcoded backpack rows. The client parsers
do not require global state; both item-list contracts are pure response data and
can be built from the active role.

The session-level grid duplicate guard remains only to avoid sending the same
`30/21` rows twice to one fresh client item manager. It stores only the role ID
seeded in this login session, not backpack item state.

## Validation

- `gcc -g -w -c src/main.c -o obj/main.o` succeeded through `make`.
- The normal `make` relink to `bin/main.exe` failed with `Permission denied`,
  which indicates the executable was in use.
- A same-object temporary link to `bin/main.codex-build.exe` succeeded, and the
  temporary executable was removed afterward.
- `git diff --check` reported only CRLF normalization warnings, no whitespace
  errors.
