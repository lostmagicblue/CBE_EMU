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
WT object: 1/4/5 by default for scene monster challenge
old fallback: 1/4/10
fields:
  side: 1 by default when CBE_BATTLE_PLAYER_ON_RIGHT=1
  battleinfo: tagged stream
```

`battleinfo` subtype 5 grammar from `HandleBattleStartMsg(0x66CC)`:

```text
u8 left_count
u32 scene_monster_index
u32 scene_monster_posx
u32 scene_monster_posy
u8 right_count
repeat right_count:
  u32 id
  u32 hp
  u32 hp_max
  u32 mp
  u32 mp_max
```

For subtype 5 the left fighter is not read from the stream. The client uses
`scene_monster_index/posx/posy` to select a scene actor row at `*(R9+13476)`,
then copies the monster id, name, HP/MP, and sprite from that row. If the index
row does not match `posx/posy`, `0x66CC` scans up to 25 active rows looking for
kind `2` with matching coordinates. This is the correct path for "touch scene
monster -> battle", because the left-side monster stays tied to the scene node
that was touched.

The scene actor HP/MP fields copied by subtype 5 are not populated by
`scene_node_find_or_create(0x0100EFC4)`. They live inside the raw move blob that
`scene_node_update_move_blob(0x01012A76)` copies to `ActorSceneNode+0x88`:

```text
node+0xB4 = moveBlob+44 = hp
node+0xB8 = moveBlob+48 = mp
node+0xBC = moveBlob+52 = hp_max
node+0xC0 = moveBlob+56 = mp_max
```

The server-side seed object is `1/2/2 { moveinfo }`, parsed by
`net_handle_actor_move_info(0x01012ADC)` case 2:

```text
moveinfo:
  i16 grid_pos_x
  i16 grid_pos_y
  u32 actor_id
  len16 raw_move_blob
```

Runtime correction: the bundled SCE has several 毒泥怪 rows with the same
`actor_id=105`. `scene_node_update_move_blob` matches by `actor_id` only and
therefore updates the first active 105 row, not necessarily the request
`index/posx/posy` row. The mock now chooses that same first-active row for both
the `1/2/2` moveinfo seed and the following subtype 5 battle start. This keeps
the battle start row, monster name/sprite, and HP/MP coherent until the unique
live monster instance id contract is recovered.

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
first record / left: scene monster selected by challenge index/posx/posy
second record / right: role id 10001, hp 120
```

The right-side record still goes through `sub_66A4(0x66A4)` template lookup, so
the mock sends a preceding `1/5/5 groupinfo` template seed for role id `10001`
when `CBE_BATTLE_PLAYER_ON_RIGHT=1`.

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

With the legacy subtype 10 `playerOnRight=1` and `side=1` layout,
`CalcTargetSideIndex(0x6CE8)` uses the first/second counts copied by
`sub_6C5E(0x6C5E)`:

```text
wire 0 -> internal slot 1, the right-side player
wire 1 -> internal slot 0, the left-side monster

player attack: actor wire 0 -> target wire 1
enemy counter: actor wire 1 -> target wire 0
```

Runtime subtype 5 evidence changes the mock default. The scene-monster start
path enters with `side=1`, then the post-start battle probe exposes the role as
internal slot 0 and the touched monster as internal slot 1. Therefore subtype 5
uses:

```text
player attack: actor wire 1 -> target wire 0
enemy counter: actor wire 0 -> target wire 1
terminal victory actor: wire 0
```

For the terminal action record, the actor slot must be the fighter that reached
zero HP. Sending the role wire describes the player as the fallen actor and can
drive the client into the "player died" prompt.

Runtime evidence after the subtype 5 start fix:

```text
mock_challenge_battle_start id=105 requested=105 index=2 pos=(87,179) subtype=5 side=1 scene_start=1
battle_probe_unit[0] id=10001 type10=0 sprite=054045a8 name0=a3bdc0cf hp=120 hpMax=120
battle_probe_unit[1] id=105   type10=0 sprite=05412150 name0=e0c4beb6 hp=0   hpMax=0
battle_probe_action[0] type=0 actor=1 target=0 valueA=4294967286
battle_probe_action[0] type=3 actor=1
```

Runtime evidence after the moveinfo seed fix:

```text
mock_scene_monster_moveinfo actor=105 pos=(142,310) hp=20/20 mp=20/20 len=80
mock_challenge_battle_start id=105 requested=105 index=1 pos=(142,310) reqIndex=2 reqPos=(87,179) subtype=5 side=1
scene_actor_update_move actor=105 len=64 gridX=142 raw44=20 raw48=20 raw52=20 raw56=20
scene_probe_node[1] actorId=105 battlePos=(142,310) battleHp=20/20
battle_probe_unit[1] id=105 sprite=05412150 name0=e0c4beb6 hp=20 hpMax=20 mp=20 mpMax=20
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
- default `CBE_BATTLE_PLAYER_ON_RIGHT=1`
- for scene challenge entry, default `CBE_BATTLE_SCENE_MONSTER_START=1`, send
  battle start object `1/4/5`, and let the left monster come from the touched
  scene actor row selected by `index/posx/posy`
- before the scene-monster battle start, default
  `CBE_BATTLE_SCENE_MONSTER_MOVEINFO=1` and send `1/2/2 moveinfo` for the same
  scene row so subtype 5 copies nonzero HP/MP from `node+0xB4..0xC0`
- keep subtype `1/4/10` only as a fallback/experiment path for non-scene starts
- prefill a right-side role template with `1/5/5 groupinfo` so the second
  record can pass `sub_66A4`
- default `CBE_BATTLE_ACTION_TYPE` to `0`, keeping type 1 and 2 available by env
  override for later skill experiments
- encode action type 0 records without effect tail fields
- for subtype 5 scene-monster starts under `side=1`, default battle wire slots
  to player `1`, enemy `0`; keep the old subtype 10 layout as fallback
- choose terminal action actor by HP-zero side: enemy slot for victory, player
  slot for defeat
- send subtype 7 settlement with `exp`, `lastexp`, `curexp`, and visible reward
  defaults; `lastexp` is the current level start threshold after reward,
  `curexp` is the next level start threshold after reward, and `persentexp` is
  current-level percentage progress after reward. Encode all three EXP fields
  as normal integer object fields: `HandleBattleSettleMsg(0x743C)` reads them
  through object getter `+0x44` and only narrows `persentexp` after parsing.
- use negative two's-complement HP deltas in subtype `4/6`; positive values heal

## unknowns

- The exact semantic name of `child_flag` is still unknown. Current code keeps
  it configurable and defaults it to zero for both directions.
- Subtype 5 currently copies the touched scene monster art/name correctly, but
  duplicated local SCE actor ids mean the moveinfo seed can only target the
  first active row with that actor id. Recovering the live server's unique
  monster instance id remains the next precision step.
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

bin/main.exe --autotest --shot-ms=3000 --max-ms=100000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f,42000:tap:45:216,44000:key:f,50000:tap:63:247,52000:key:f,58000:key:f,64000:tap:48:214,66000:key:f,72000:key:f,78000:key:f,84000:key:f,90000:key:f
env: CBE_SCENE_POS_X=225, CBE_SCENE_POS_Y=116
env: CBE_BATTLE_SCENE_MONSTER_START=1
result: touched actor id 105, entered battle without crash, monster row uses name0=e0c4beb6 and sprite=05412150, no player-death prompt after terminal action
```

The key-only script is still unreliable for producing `4/1`/`4/2`; mixed
tap+key scripts are more repeatable until a dedicated battle-entry automation
helper exists.
