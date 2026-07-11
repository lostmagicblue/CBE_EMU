# 2026-07-07 Empty Initial Role List

## Problem

The local mock role DB still bootstrapped a synthetic default role when an
account had no persisted role DB yet. That made a brand-new account appear to
already have one role row before the player created anything.

## Runtime Contract

The title role-list parser already accepts an empty compact `actorinfo` table:

- count `0` means there are no role rows;
- the client then shows only its create-role sentinel entry.

This was already validated by the title role-delete path, where deleting the
last role returns to the same empty-list state.

## Implementation

- `vm_net_mock_role_db_load()` now initializes a fresh DB with:
  - `roleCount = 0`
  - `activeRoleId = 0`
- the title role-list builder now emits `count=0` instead of falling back to
  one synthetic row when the DB is merely "valid".
- when loading an older persisted DB that contains exactly one pristine
  auto-bootstrap default role, the mock drops that synthetic row and rewrites
  the DB as empty.

Existing real created roles are preserved; only the untouched bootstrap placeholder
is removed.

## Validation

- `make -j2`: passed
- code evidence:
  - empty-list behavior already documented in
    `docs/re/2026-06-26-title-role-delete.md`
  - title role-list compact payload still comes from
    `vm_net_mock_build_title_role_list_actorinfo()`
