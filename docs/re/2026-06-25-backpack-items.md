# Backpack Items Phase

## Goal

- Player backpack capacity: 40 slots.
- Initial item: item `800` (`传送石`) at seq `1`, stack count `5`.

## IDA Evidence

### Capacity Source

`江湖OL.CBE:parse_actorinfo_playerinfo_blob(0x0100FA88)` parses the scene/login `actorinfo` blob.

For the scene-side `a2 == 0` path:

- `0x0100FC04` reads one `u8` from `actorinfo`.
- It writes that value to `R9 + 24678`, which is the main item manager at `R9 + 24640` plus offset `38`.
- `TimerControl_ProcessItem(0x01032EB8)` compares current item count at manager `+36` with this manager `+38` value before it searches for an empty item slot.

Runtime negative evidence: when this actorinfo byte was `0`, item insertion returned before scanning empty slots. Setting it to `40` allowed normal client-side insertion.

### Default Item Insert

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
actorinfo backpackCapacity = 40
```

This value is configurable for experiments through `CBE_ACTOR_BACKPACK_CAPACITY`, defaulting to `40`.

### Initial Grid Item

The mock server appends one `1/30/21` object once after the first type-1 group response:

```text
result = 1
gridnum = 1
iteminfo =
  u32 itemId = 800
  i16 seq = 1
  u32 count = 5
  common item-extra block
```

Runtime validation showed the main item manager reached:

```text
item_count=1 item0=800 seq0=1 stack242=5
```

### Full Backpack List

The `1/17/1` full list response exposes:

```text
maxnum = 40
iteminfo =
  u8 item_count = 1
  u32 itemId = 800
  common item-extra block with stack byte = 5
```

## Removed Failed Paths

Do not reintroduce these as initial-backpack setup:

- `25/6`: this is a shop/acquire-style result path, not capacity initialization.
- `7/7`: duplicates the item stack after `30/21` and produced `x10`.
- `7/37` and `7/12`: acquire/message/count side paths, not the default inventory contract.

## Current Status

Implemented and validated for the main client item manager:

- capacity field is packet-driven through `actorinfo`;
- default teleport stone is inserted through `30/21`;
- stack remains `5` after removing duplicate update packets.

The mmGame backpack UI full-list path (`17/1`) is implemented as a server response, but visual inventory-screen confirmation should be handled in a follow-up once the exact UI open automation/parser window is isolated.
