# 2026-06-28 Item Discard

## Problem

Discarding a backpack item crashed because the mock server did not handle the
client request:

```text
WT 7/4 len=38 objects=1 first=1/7/4:29
```

## Request Evidence

- Runtime signature: one WT object, `major=1 kind=7 subtype=4`, payload length
  `29`.
- The request payload is parsed narrowly as an item selector:
  - `seq`, `itemseq`, or `itemSeq`
  - optional `id`, `itemId`, `itemID`, or `itemid`
  - optional `count` or `num`
- Lookup prefers `seq` when present, because older CBE request builders may
  reuse `id` for a non-item identifier in mixed UI flows.
- If no count is present, discard removes the full selected stack. This matches
  the UI-level discard action better than the item-use path, which consumes one.

## Parser Evidence

- `JianghuOL.CBE:0x01033544 HandleItemOperationResponse`
  - subtype `4` clears the item-operation waiting flag at the scene object.
  - subtype `11/12` can update a row count by `seq`, but for ordinary items it
    only writes a count field and does not rebuild the visible list.
- `mmGameMstarWqvga.cbm:0x418C`
  - `17/1` reparses the full visible backpack item list from `iteminfo`.
  - `7/42` is the companion empty book-list object used by the backpack screen.

## Response Contract

On success:

```text
1/7/4  { result=1 }
1/17/1 { maxnum, iteminfo=<full active-role backpack list> }
1/7/42 { booknum=0, booksinfo=<empty> }
1/7/11 { info=<row_count=1, seq, remaining_count> }
```

On failure:

```text
1/7/4 { result=2 }
```

The role DB is saved with reason `item-discard` after the stack is removed or
decremented. `17/1` is the authoritative visible-list refresh; `7/11` is a
CBE-side row-count fallback for callbacks that see the main kind-7 dispatcher.

## Runtime Validation

Expected handled source:

```text
builtin-item-discard
mock_item_discard ... refresh=7/4+17/1+7/42+7/11
```

Manual checks:

1. Open backpack.
2. Discard an item stack.
3. Confirm no assert.
4. Confirm the item list and the bottom slot counter both reflect the active
   role backpack after discard.
