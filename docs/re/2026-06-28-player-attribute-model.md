# 2026-06-28 Player Attribute Model

## Goal

Build one server-side player stat model for Jianghu OL mock-server gameplay:

- role level supplies base attributes;
- equipped items add bonuses from local `equip.dsh`;
- HP/MP maxima, normal attack, and defense all come from the same model;
- battle damage uses a soft defense curve instead of raw subtraction;
- persisted role data keeps per-role equipment slots for later equip/unequip work.

## Client Boundary

No new client field was introduced in this pass.

- `JianghuOL.CBE:parse_actorinfo_response(0x0100FA88)` still consumes the
  existing scene/login `actorinfo` blob. The mock now fills the known HP/MP and
  attribute scalar slots from the unified model.
- `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` still consumes battle
  start HP/MP records. The mock now seeds those records from the same model.
- `mmBattleMstarWqvga.cbm:HandleBattleActionMsg(0x6EB0)` still consumes action
  damage deltas. The mock now derives the delta from player attack and monster
  defense, or monster attack and player defense.
- `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` remains the persistence
  point for HP/MP/EXP/money.

## Role DB

The role DB is upgraded from version 2 to version 3.

Version 3 keeps all previous per-role fields and adds:

```text
equippedItemIds[8]
```

Slot mapping:

```text
0 weapon  (equip.dsh category 7 sword, 8 dagger, 9 staff)
1 helmet  (category 0)
2 chest   (category 1)
3 cloak   (category 2)
4 belt    (category 3)
5 legs    (category 4)
6 boots   (category 5)
7 ring    (category 6)
```

Version 1 and version 2 files migrate in-place to version 3. Migrated and newly
created roles receive one starter weapon based on job:

```text
job 1: item 1001 木制宽剑
job 2: item 1501 木制匕首
job 3: item 2001 木制杖
```

The starter weapon is stored as equipped gear, not as a backpack stack. The
backpack remains the existing per-role 40-slot inventory.

## Equipment Data

`bin/JHOnlineData/equip.dsh` is parsed lazily. Relevant columns:

```text
0  ID
3  等级
7  类别
8  护甲
9  攻击
10 生命变化
11 法力变化
12 力量变化
13 敏捷变化
14 智力变化
15 爆击变化
16 命中变化
17 躲闪变化
18 抗性变化
```

Equipment bonuses only apply when:

- the equipped item id exists in `equip.dsh`;
- the item category matches its stored slot;
- role level is at least the equipment level requirement.

## Base Attributes

The existing job tables remain the base model:

```text
job 1: strength 12 +3/L, agility  8 +2/L, wisdom  7 +1/L, endurance 11 +3/L, charm 3 +1/L
job 2: strength  9 +2/L, agility 14 +3/L, wisdom  8 +2/L, endurance  8 +2/L, charm 4 +1/L
job 3: strength  7 +1/L, agility  9 +2/L, wisdom 15 +4/L, endurance  7 +2/L, charm 5 +1/L
```

The increment uses `(level - 1)`, so level 1 is exactly the base row.

Visible attributes:

```text
strength = baseStrength + equipment.strength
agility  = baseAgility  + equipment.agility
wisdom   = baseWisdom   + equipment.wisdom
endurance = baseEndurance
charm = baseCharm + money / 100000
```

`equip.dsh` does not currently expose an endurance column, so endurance remains
level/job based until a recovered item field proves otherwise.

## Derived Combat Stats

The mock derives combat stats as:

```text
maxHp   = 90 + level * 8 + endurance * 2 + equipment.hp
maxMp   = 70 + level * 9 + wisdom * 3 + equipment.mp
attack  = 6 + level * 2 + strength / 2 + equipment.attack / 3
defense = 4 + level + endurance / 2 + equipment.armor / 5
hit     = 75 + level + agility * 2 + equipment.hit
dodge   = 3 + level / 2 + agility / 2 + equipment.dodge / 2
crit    = 1 + agility / 3 + wisdom / 5 + equipment.crit / 2
resist  = wisdom / 2 + endurance / 3 + equipment.resist
```

Rationale:

- equipment primary attributes are visible at full value;
- weapon attack and armor are folded into combat at a reduced rate so early
  `equip.dsh` rows matter without making monster stats unusable;
- HP/MP scale gently with level so role persistence remains readable on the
  original feature-phone UI.

## Defense Formula

Damage now uses a soft mitigation curve:

```text
damage = max(1, attack * 100 / (100 + defense))
```

This avoids the old `attack - defense` cliff where a small stat mismatch could
collapse damage to `1`, while still making defense meaningful at all levels.

## Implementation Points

`src/mock-server.c` now centralizes the model in:

```text
vm_net_mock_load_equipment_catalog()
vm_net_mock_role_build_player_stats()
vm_net_mock_role_sync_derived_vitals()
vm_net_mock_damage_after_defense()
```

Call sites now using the model:

```text
vm_net_mock_build_actor_info()
vm_net_mock_build_battle_start_info_blob()
vm_net_mock_build_battle_scene_start_info_blob()
vm_net_mock_build_challenge_interaction_response()
vm_net_mock_battle_role_attack_default()
vm_net_mock_battle_role_defense_default()
vm_net_mock_battle_player_damage_to_enemy()
vm_net_mock_battle_enemy_damage_to_role()
vm_net_mock_role_apply_battle_settlement()
```

## Validation

Validated with:

```text
make
```

Result: passed.

`git diff --check` should still be run before finalizing a larger batch because
this repository emits Git CRLF conversion warnings for touched text files.
