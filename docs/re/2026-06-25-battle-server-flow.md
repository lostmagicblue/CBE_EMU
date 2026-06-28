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
skill/spell: Operate == selected_skill_id + 2
```

IDA evidence for the normal attack request is `mmBattleMstarWqvga.cbm`
`Callback_Unknown2(0x2B50)`: when `a2 == 4 && a3 == 2`, the client writes
`index = sub_2B26(*(a1 + 5))`; if no selected skill record exists, it writes
`Operate = 0`. Therefore a visible skill animation after pressing normal attack
is caused by the server `actioninfo` response, not by the request.

For selected skills, the same request builder reads the selected battle-skill
record pointer at `r9+0x40B4` and sends `Operate = skillId + 2`. The server
must map `skillId` through `skill.dsh` column `法术标示` before writing the
action effect index. Sending the raw skill id as the effect index is invalid:
runtime with `Operate=203` for skill id `201` tried to use effect `201` and
crashed during effect allocation. The corrected response maps skill id `201`
to effect `15`.

The same `skill.dsh` row also drives MP cost and damage. Column `耗费法力` is
the battle skill cost; for `201 绯炎幻法1` it is `5`. Column `生命变化` is a
signed base HP effect; offensive skills use its absolute negative value as the
minimum damage. Columns `力量系数`, `敏捷系数`, and `智慧系数` add scaled player
attributes before the normal soft defense curve. For example, `201 绯炎幻法1`
has `生命变化=-30` and `智慧系数=110`, so it must not reuse the normal-attack
damage path. Runtime and IDA evidence now separates three MP paths. The mock
server persists `role->mp -= 耗费法力`.
Subtype `4/6 actioninfo.valueB` for a type-1 child is the target MP change
paired with that child, not the caster skill cost and not the post-cost
absolute MP; for an offensive HP-only skill, keep this field `0`. The same
subtype-6 object must carry `teaminfo` for the caster: `InitActionSlot_B(0x6DBC)`
reads one triplet per team member and copies the third value into `unit+1344`.
`DrawBattleSceneBg(0x4BE8)` later assigns `attacker.currentMP = attacker+1344`
at the end of type-1 playback, so omitting `teaminfo` leaves that cache as `0`
and clears visible MP after a spell. Subtype `4/7` field `mp` remains the
settlement/recovery display delta; current temporary tuning defaults battle-end
MP recovery to `50`, still overrideable through `CBE_BATTLE_RECOVER_MP`. The
mock applies the same recovery to the persisted role MP before saving the
battle settlement, capped by the role's derived MP max.

Current contract: a skill response keeps type-1 scoped to the enemy target only.
That record carries skill-derived HP damage in `valueA`, target MP delta `0` in
`valueB`, and the mapped spell effect index from `skill.dsh`. The same `4/6`
object carries `teaminfo = roleId, currentHp, postCostMp`, and the mock server
also updates the selected role's MP in the local role database. `actionnum`
still permits multiple records: if the enemy survives the skill hit, append the
normal monster counterattack record in the same subtype-6 response. If the skill
kills the enemy, append the terminal record instead and defer settlement as
usual.

Negative evidence: encoding the caster as a second child in the same type-1
record made both the monster and the player receive the spell visual, because
the type-1 effect metadata is record-wide, not child-local.

Negative evidence: using action type `2` for the separate MP-cost record enters
the battle-item branch in `HandleBattleActionMsg` around `0x70a8..0x70b0`.
That branch dereferences the selected battle item pointer at `r9+0x40B8`;
spell operation does not populate that pointer, so runtime crashed with
`r1=0`, `r2=0x322`, and a null read.

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
  teaminfo: tagged stream, optional but required for type-1 caster MP cache
  actionnum: u8
  actioninfo: tagged stream
```

`InitActionSlot_B(0x6DBC)` parses `teaminfo` before `actioninfo`. This is not
three independent tagged u32 values. The function calls the tagged-i32 reader
three times, but rewinds the cursor by two bytes after the first two calls:

```text
repeat current_team_count:
  u8  0
  u8  4
  u32 role_id
  u32 current_hp
  u32 current_mp
```

The three read starts are `row+0`, `row+4`, and `row+8`, so the returned values
are `role_id`, `current_hp` (ignored by current code), and `current_mp`.
For every active battle unit whose id matches `role_id`, the third returned
value is copied to `unit+1344`. Type-1 playback later restores the attacker's
current MP from that cache.

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

Subtype 5 has a different runtime wire default than the old subtype 10 fallback.
`CalcTargetSideIndex(0x6CE8)` still remaps through the battle-start group counts,
but the scene-monster start path uses the touched scene actor row directly. With
`side=1` and one touched monster plus one role, runtime evidence shows:

```text
wire 1 -> right-side player
wire 0 -> left-side monster

player attack: actor wire 1 -> target wire 0
enemy counter: actor wire 0 -> target wire 1
terminal victory actor: wire 0
```

Negative evidence: unifying subtype 5 with the subtype 10 fallback and sending
`actor wire 0 -> target wire 1` made an offensive player skill visibly target
the player. Runtime also showed post-cost `valueB=35` displayed as MP recovery,
with the visible MP moving from `35` to `65`.

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

Current expected runtime log after the subtype-5 wire and skill-damage
correction:

```text
mock_battle_operate index=0 operate=203 skill=1 action=1 actions=2 effect=15 actor=1 target=0 damage=<skill-derived> enemyhp=<nonzero> rolehp=<after-counter> mpcost=5 valueB=0 teaminfo=10001:<hp>/<postCostMp> mp=<before>/<after> response=4/6
battle_probe_action[0] active=1 type=1 actor=<player-wire> childCount=1 target=<enemy-wire> valueA=-<skill-derived> valueB=0 effect=15
battle_probe_action[1] active=1 type=0 actor=<enemy-wire> childCount=1 target=<player-wire> valueA=-<monster-damage> valueB=0
```

If the skill damage kills the enemy, `actions=2` is still valid but the second
record is terminal action type `3` instead of the monster counterattack.

Older negative evidence collected while debugging battle skill use:

```text
battle_probe_unit[0] before action mp=100; after action mp=0 when valueB=0
mock_battle_operate ... mpcost=5 mp=100/95 response=4/6
battle_probe_action[0] ... childCount=1 ... valueB=95
battle_probe_unit[0] after animation mp=0, so enemy-child valueB is not the caster MP field
runtime after a two-child type-1 experiment showed MP first dropped by 5, then
fell to 0, while both player and monster played the spell effect; therefore MP
cost must not be encoded as another child of the same type-1 record
runtime with separate `costAction=2` crashed in the item-use branch because
the selected battle item pointer was null; use `costAction=0` instead
runtime with `costAction=0,valueA=0,valueB=-5` deducted MP first, then later
showed a 0 HP heal/change line.
runtime with `costAction=0,valueA=currentHP,valueB=postCostMP` removed the 0 HP
line but displayed `currentHP` as a heal. Therefore separate MP-cost actions are
disabled by default; carry MP cost only in the server-side role state.
runtime with a single non-terminal type-1 action and `valueB=-5` drove player MP
from 55 to 0 even though no subtype `4/7` settlement was sent. This proves
`4/6 actioninfo.valueB` is not a signed MP delta.
runtime with `actor=0,target=1,valueB=35` made the skill hit the player and
raised visible MP from 35 to 65, so offensive skill `valueB` cannot be the
post-cost absolute MP.
runtime with `actor=1,target=0,valueB=5` wrote `unit1Mp` from 20 to 25 after the
action response. Since subtype-5 remaps this child to the monster target, this
confirms `valueB` is target MP delta, not caster MP cost. The accompanying
`pc=05186d64 off=12d64` maps to soft-float `Float32_Add(0x12CB0)`, not
`InitActionSlot_A(0x6D1E)`; the earlier iteminfo hypothesis was a bad base
mapping. Follow-up IDA evidence in `InitActionSlot_B(0x6DBC)` showed that
subtype-6 `teaminfo` is exactly the missing caster MP cache: when omitted,
`DrawBattleSceneBg(0x4BE8)` restores the attacker's MP from zeroed `unit+1344`.
Negative runtime: sending `teaminfo` as three normal tagged u32 values made the
third read return `hp_low16 + next tagged header`, observed as `0x210004` when
HP was `33`, then crashed in the renderer. Encode one overlapped row instead:
`00 04, roleId32, hp32, mp32`.
runtime with terminal inline `4/7 mp=0` drove MP to 0 after first showing the
post-cost value, so terminal settlement must be deferred until action playback
has completed.
```

Implementation note: type-1 `effect_text` should carry the display magnitude
for the HP delta (`18` for `valueA=0xffffffee`), not the unsigned two's
complement value.

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

Important timing correction: `HandleBattleSettleMsg(0x743C)` clears and fills
the battle temporary display/state area at `r9+15724`. It writes response field
`hp` to `r9+15728` and response field `mp` to `r9+15724`; then
`BattleSettle_UpdateCharAttrs(0x2C50)` adds those values to the selected battle
unit. Therefore subtype `4/7` must not be sent in the same response before the
terminal `4/6 actioninfo` has finished animating, or the recovery MP field can
overwrite the pending skill MP-cost state. Default behavior is deferred
settlement: terminal action first, pending `4/7+4/11+4/9` afterwards.

Implementation correction: terminal action handling saves the role state before
the client necessarily asks for the deferred settlement packet. Therefore the
mock applies the configured battle-end MP recovery during terminal state save as
well as in the deferred settlement builder, guarded by the current battle serial
so recovery is persisted once. The `4/7 mp` field remains the visible recovery
delta sent to the client.

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
- defer subtype 7 until after the terminal action response; keep recovery
  `hp/mp` as settlement display deltas, not as inline action-state data.
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
