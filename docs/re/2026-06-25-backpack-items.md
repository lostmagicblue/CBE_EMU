# Backpack Items Phase

## Goal

- Player backpack initial capacity: 20 slots.
- Initial backpack contents: empty.
- Backpack rows belong to the active local role, not a mock-wide global
  inventory.

## IDA Evidence

### Capacity Source

`江湖OL.CBE:parse_actorinfo_playerinfo_blob(0x0100FA88)` parses the scene/login `actorinfo` blob.

For the scene-side `a2 == 0` path:

- `0x0100FC04` reads one `u8` from `actorinfo`.
- It writes that value to `R9 + 24678`, which is the main item manager at `R9 + 24640` plus offset `38`.
- `TimerControl_ProcessItem(0x01032EB8)` compares current item count at manager `+36` with this manager `+38` value before it searches for an empty item slot.

Runtime negative evidence: when this actorinfo byte was `0`, item insertion returned before scanning empty slots. Setting it to a nonzero capacity allowed normal client-side insertion. The current mock default is `20`.

### Role Item Insert

`江湖OL.CBE:HandleItemGridResponse(0x01039952)` handles WT object `1/30/21`.

Observed parser contract:

- field `result`, success value `1`
- field `gridnum`
- raw field `iteminfo`
- for each grid item:
  - `u32 itemId`
  - `i16 seq`
  - `u32 count`
  - common item-extra block
- calls `MoveBattleActorStep(0x0101918E)`.

`MoveBattleActorStep(0x0101918E)` calls the item-sheet loader, writes seq at item `+276`, count at item `+242`, then calls `TimerControl_ProcessItem(0x01032EB8)`.

`TimerControl_ProcessItem(0x01032EB8)` inserts into the first slot whose first dword is `< 0`, then increments manager `+36` when item type is countable. `InitTimerControl(0x010332BE)` initializes those slots to `0xffffffff`.

### Full Backpack Query

`mmGameMstarWqvga.cbm:sub_418C(0x0518418C)` handles `1/17/1` full backpack refresh. The server keeps a narrow direct handler for the empty `17/1` request and also returns the same object alongside `7/42` backpack-open responses.

## Server Contract

### Actor Info

`vm_net_mock_build_actor_info()` now writes:

```text
actorinfo backpackCapacity = activeRole.backpackCapacity
```

This value is configurable for experiments through `CBE_ACTOR_BACKPACK_CAPACITY`, defaulting to `20`.

### Initial Role Grid Items

The mock server appends `1/30/21` after the first type-1 group response for the
current role in the current login session. The item data comes from the active
role DB row:

```text
result = 1
gridnum = activeRole.backpackItemCount
iteminfo =
  repeat gridnum:
    u32 itemId
    i16 seq
    u32 count
    common item-extra block
```

Runtime validation showed the main item manager can bootstrap directly from the persisted role rows without any mock-wide starter item.

### Full Backpack List

The `1/17/1` full list response exposes:

```text
maxnum = activeRole.backpackCapacity
iteminfo =
  u8 item_count = activeRole.backpackItemCount
  repeat item_count:
    u32 itemId
    common item-extra block with stack byte = min(count, 255)
```

### Persistence

The role DB file was later extended beyond version 2, but this phase introduced the per-role backpack fields:

```text
backpackItemCount
nextBackpackSeq
backpackItems[...] = itemId, seq, count
```

New roles now start with an empty backpack and initial capacity `20`. Legacy role DB files that still carried the old default `40`-slot capacity are migrated forward to the new baseline while preserving occupied rows. NPC/shop buy responses add or stack the purchased item into the active role before returning the parser-facing `14/3 { seq, result }`.

## Removed Failed Paths

Do not reintroduce these as initial-backpack setup:

- `25/6`: this is a shop/acquire-style result path, not capacity initialization.
- `7/7`: duplicates the item stack after `30/21` and produced `x10`.
- `7/37` and `7/12`: acquire/message/count side paths, not the default inventory contract.

## Current Status

Implemented and validated for the main client item manager:

- capacity field is packet-driven through `actorinfo`;
- role backpack rows are inserted through `30/21`;
- `17/1` full-list rows are generated from the same active role backpack.

Backpack expansion now consumes item `806` (`背包扩容`) and raises the role capacity by `5` per use, up to `200`. Because the item-use success parser (`7/1`) does not read capacity directly, the mock follows a successful expand-card use with a separate `17/1` refresh packet so the client updates slot count through its normal backpack parser path.

The mmGame backpack UI full-list path (`17/1`) is implemented as a server response, but visual inventory-screen confirmation should be handled in a follow-up once the exact UI open automation/parser window is isolated.
