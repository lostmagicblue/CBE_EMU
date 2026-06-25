# 2026-06-25 Battle Server Flow

## phase

Restore the first monster battle loop:

- touch a monster and enter battle
- render the requested enemy instead of the player role
- make the normal attack button produce a physical attack
- include an enemy counter action while the enemy is alive
- send a battle settlement with visible experience fields

## request

Battle entry is driven by the existing scene interaction request:

```text
WT object: 1/4/1
stable fields: id, index, posx, posy
optional field: moveinfo
```

Battle operation is driven by:

```text
WT object: 1/4/2
stable fields: index, Operate
normal attack: Operate == 0
```

IDA evidence for the normal attack request is `mmBattleMstarWqvga.cbm`
`Callback_Unknown2(0x2B50)`: when `a2 == 4 && a3 == 2`, the client writes
`index = sub_2B26(*(a1 + 5))`; if no selected skill record exists, it writes
`Operate = 0`. Therefore a visible skill animation after pressing normal attack
is caused by the server `actioninfo` response, not by the request.

## response

Battle start response:

```text
WT object: 1/4/10
fields:
  side: 1 by default when CBE_BATTLE_PLAYER_ON_RIGHT=1
  battleinfo: tagged stream
```

`battleinfo` subtype 10 grammar from `HandleBattleStartMsg(0x66CC)`:

```text
u8 own_count
repeat own_count:
  u32 id
  u32 hp
  u32 hp_max
  u32 mp
  u32 mp_max
  string name
  u8 visual_group
  u8 visual_variant
u8 enemy_count
repeat enemy_count:
  u32 id
  u32 hp
  u32 hp_max
  u32 mp
  u32 mp_max
```

Important placement correction: `HandleBattleStartMsg(0x66CC)` places the
first count at x=40 and the second count at x=200. Runtime visual evidence
showed the player sprite is on the right, so the default mock layout is:

```text
first record / left: monster id 105, hp 20
second record / right: role id 10001, hp 120
```

The right-side record still goes through `sub_66A4(0x66A4)` template lookup,
so the mock sends a preceding `1/5/5 groupinfo` template seed for role id
`10001` when `CBE_BATTLE_PLAYER_ON_RIGHT=1`.

`sub_66A4(0x66A4)` matches ids against four local template rows at
`*(R9 + 13472)`, row stride `0x4C`, id at row offset `0x24`. If a scene
challenge request sends the player id `10001` or `0` as the monster id, the mock
normalizes it to `CBE_BATTLE_DEFAULT_ENEMY_ID`, default `105`.

Battle action response:

```text
WT object: 1/4/6
fields:
  actionnum: u8
  actioninfo: tagged stream
```

`HandleBattleActionMsg(0x6EB0)` grammar:

```text
repeat actionnum:
  u8 action_type
  u8 actor_wire_slot, remapped by CalcTargetSideIndex(0x6CE8)
  u8 child_count
  repeat child_count:
    u8 target_wire_slot, remapped by CalcTargetSideIndex(0x6CE8)
    u8 child_flag
    u32 value_a
    u32 value_b
  if action_type == 1 or action_type == 2:
    u32 effect_index
    string effect_text
    u8 tail0
    u8 tail1
    u8 tail2
```

Important correction: for physical attacks, do not write the `effect_index`,
`effect_text`, and tail bytes. Those fields are only parsed for action type 1
or 2. Writing them after action type 0 shifts the next action record and can
make the second record look like a malformed or missing enemy turn.

With default `playerOnRight=1` and `side=1`, `CalcTargetSideIndex(0x6CE8)` uses
the first/second counts copied by `sub_6C5E(0x6C5E)`:

```text
wire 0 -> internal slot 1, the right-side player
wire 1 -> internal slot 0, the left-side monster

player attack: actor wire 0 -> target wire 1
enemy counter: actor wire 1 -> target wire 0
```

For the terminal action record, the actor slot must be the fighter that reached
zero HP. With the default right-player layout, victory after killing the monster
therefore uses terminal actor wire `1`, not wire `0`. Sending terminal actor
wire `0` describes the right-side player as the fallen actor and can drive the
client into the "player died" prompt.

Runtime evidence after the start fix:

```text
battle_probe side=1 cmd=1/17/24/0
battle_probe_unit[0] id=105   hp=20  hpMax=20
battle_probe_unit[1] id=10001 hp=120 hpMax=120
```

Battle settlement response:

```text
WT object: 1/4/7
fields:
  exp
  lastexp
  curexp
  persentexp
  energy
  energymax
  gold
  level
  result
  bagstatus
  hp
  mp
  itemnum
  iteminfo
```

`HandleBattleSettleMsg(0x743C)` reads `exp` first, then `lastexp`, `curexp`,
`persentexp`, and the remaining status fields. Bytes at `0x771C` confirm the
first key is the string `exp`.

## implementation target

Minimum server fix:

- map invalid battle enemy ids `0` and `10001` to `CBE_BATTLE_DEFAULT_ENEMY_ID`
  with default `105`
- default `CBE_BATTLE_PLAYER_ON_RIGHT=1`, putting the monster in the left
  battleinfo record and the role id in the right record
- prefill a right-side role template with `1/5/5 groupinfo` so the second
  record can pass `sub_66A4`
- default `CBE_BATTLE_ACTION_TYPE` to `0`, keeping type 1 and 2 available by env
  override for later skill experiments
- encode action type 0 records without effect tail fields
- default battle wire slots to player `0`, enemy `1` under `side=1`
- choose terminal action actor by HP-zero side: enemy slot for victory, player
  slot for defeat
- send subtype 7 settlement with `exp`, `lastexp`, `curexp`, and visible reward
  defaults
- use negative two's-complement HP deltas in subtype `4/6`; positive values heal

## unknowns

- The exact semantic name of `child_flag` is still unknown. Current code keeps
  it configurable and defaults it to zero for both directions.
- Monster template availability still depends on the battle module resource
  table at runtime. If id `105` is absent on a later map, the next step is to
  document that table and choose the map-local template id from client data.
- The current `key:f` autotest trigger for touching a monster is not stable
  enough to validate every battle operation run. The server-side start contract
  is verified by `battle_probe`, but the next work item should add a reliable
  battle-entry automation path before tuning settlement UI.

## validation

Build:

```text
make
result: passed
```

Runtime smoke:

```text
make
result: passed

bin/main.exe --autotest --shot-ms=1500 --max-ms=70000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f,45000:key:f,54000:key:f,61000:key:f
env: CBE_SCENE_POS_X=225, CBE_SCENE_POS_Y=116
result: entered battle without crash; battle start units are left monster id 105 and right role id 10001
```

The same key-only script is still unreliable for producing `4/2` after battle
entry, so final visual validation of the first normal attack should be done by
manual click or a future dedicated battle autotest helper. The expected server
contract for that first attack is actor wire `0`, target wire `1`.
