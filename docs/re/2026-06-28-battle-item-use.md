# 2026-06-28 Battle Item Use

## Runtime Trigger

Using a backpack item from the battle UI produced an unhandled request:

```text
WT 4/3 len=30 objects=1 first=1/4/3:21
```

This is separate from normal scene/backpack item use (`7/1`). The battle module
has its own request path.

## IDA Evidence

`mmBattleMstarWqvga.cbm:Callback_Unknown2(0x2B50)` constructs battle requests:

- subtype `4/2`: writes `index` and `Operate`
- subtype `4/3`: writes `index` and `seq`
- subtype `4/11`: writes `type`

The subtype `4/3` `seq` field is read from the selected item row at offset
`+276`, matching the role backpack row sequence used by the mock DB.

`mmBattleMstarWqvga.cbm:HandleServerBattleCmd(0x7BD0)` does not dispatch a
server response subtype `4/3`. Server-side action playback is subtype `4/6`,
which calls `HandleBattleActionMsg(0x6EB0)`.

`HandleBattleActionMsg(0x6EB0)` parses `actioninfo` records. Action type `2`
uses the action effect fields and then decrements the currently selected battle
item row (`+242`) when the target side is the player side. This identifies
action type `2` as the battle item-use action.

Negative evidence: `4/4` is the escape result branch. The strings at
`0x8044/0x8050` decode as escape success/failure, so `4/4` must not be used as
the item-use acknowledgement.

## Mock Contract

Request detector:

```text
WT object 1/4/3
fields:
  index
  seq
```

Response:

```text
WT object 1/4/6
fields:
  actionnum
  actioninfo
```

The first action record is:

```text
action_type = 2
actor = player wire slot
target = player wire slot
value_a = HP actually restored
value_b = MP actually restored
effect_index = CBE_BATTLE_ITEM_EFFECT_INDEX or 0
```

The mock consumes one active-role backpack row by `seq`, applies the item effect
from `item.dsh`, updates/persists role HP/MP/EXP, and returns the action packet.
By default a live enemy counterattack is bundled after the item action; set
`CBE_BATTLE_ITEM_USE_COUNTER=0` for focused rollback experiments.

If the selected `seq` cannot be resolved or the item has no usable effect, the
mock returns a narrow battle no-op action and does not consume DB state.

## Runtime Source

Handler source name:

```text
builtin-battle-item-use
```

Trace line:

```text
mock_battle_item_use index=... seq=... item=... remaining=... response=4/6-actionType2
```
