# 2026-06-28 Monster Stat System

## Phase

Build a systematic monster stat layer for the local Jianghu OL mock server.

The goal is no longer a single hard-coded poison slime. The mock server now
knows every monster id referenced by local automatic monster resources, assigns
each one a level/family, and derives HP, MP, attack, defense, EXP, and copper
from one RPG-style rule set.

## Evidence

Local resource evidence:

- `bin/JHOnlineData/automonster.dsh`
  - Columns: `场景`, `怪物id1`, `怪物id2`, `怪物id3`.
  - Rows: 104 scene rows.
  - Unique nonzero monster ids: 73.
- `tmp/all_sce_bundle/<scene>.sce/scene.json`
  - Monster spawn records are exported as `records[].unknown_blob`.
  - The combat spawn token shape is:

```text
u16 kind = 3
u16 x
u16 y
meta token type 5 or 6: field 14 = actor id
string field 15 = display name
scalar field 16 = visual/class hint
string field 17 = actor resource
```

IDA parser boundary:

- `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` consumes `side` and
  `battleinfo`.
- `mmBattleMstarWqvga.cbm:HandleBattleActionMsg(0x6EB0)` consumes
  `actionnum/actioninfo`.
- `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` consumes settlement
  fields including `exp`, `gold`, `hp`, and `mp`.

Runtime/server boundary:

- Scene-monster battle start subtype 5 copies monster HP/MP from the scene node
  seeded by `1/2/2 { moveinfo }`, specifically raw blob offsets `+44/+48/+52/+56`.
- Battle action damage is expressed through subtype `1/4/6` action child value A
  as a negative HP delta.
- Battle reward persistence and display are expressed through subtype `1/4/7`.

## Implementation

`src/mock-server.c` now has:

- `g_vm_net_mock_monster_entries[]`: all 73 local automatic monster ids with
  server level and monster family.
- `vm_net_mock_monster_stats_for_enemy()`: derives HP, MP, attack, defense, EXP,
  copper, and optional drop info from the monster entry.
- `vm_net_mock_battle_player_damage_to_enemy()`: player attack minus monster
  defense, with role strength/level as the default player attack source.
- `vm_net_mock_battle_enemy_damage_to_role()`: monster attack minus role defense,
  with role endurance/level as the default role defense source.

The existing environment overrides remain useful for focused experiments:

```text
CBE_BATTLE_ENEMY_HP
CBE_BATTLE_ENEMY_MAX_HP
CBE_BATTLE_ENEMY_MP
CBE_BATTLE_ENEMY_MAX_MP
CBE_BATTLE_PLAYER_ATTACK
CBE_BATTLE_ENEMY_ATTACK
CBE_BATTLE_ENEMY_DEFENSE
CBE_BATTLE_ROLE_DEFENSE
CBE_BATTLE_REWARD_EXP
CBE_BATTLE_REWARD_GOLD
CBE_BATTLE_DROP_ITEM_ID
CBE_BATTLE_DROP_RATE
```

Poison slime compatibility remains explicit:

```text
enemy_id = 105
name = 毒泥怪
hp/mp = 20/20
attack/defense = 8/2
reward = 5 EXP, 5 copper
drop = 小长命散 item 304, 10%
```

## Stat Rules

Monster levels are based on first appearance in `automonster.dsh` row order,
with a +1 offset when a monster first appears in the second/third monster slot.
Families then derive stats by formula:

```text
slime:     hp=16+L*4,  mp=18+L*2, attack=6+L*2,  defense=2+L/4, exp=3+L*2, gold=3+L*2
beast:     hp=26+L*7,  mp=10+L,   attack=7+L*2,  defense=2+L/3, exp=4+L*3, gold=3+L*2
flying:    hp=18+L*5,  mp=12+L,   attack=8+L*2,  defense=1+L/4, exp=4+L*3, gold=3+L*2
insect:    hp=16+L*5,  mp=12+L,   attack=8+L*2,  defense=1+L/5, exp=4+L*3, gold=3+L*2
reptile:   hp=24+L*6,  mp=14+L,   attack=8+L*2,  defense=2+L/3, exp=5+L*3, gold=4+L*2
undead:    hp=34+L*8,  mp=10+L,   attack=8+L*2,  defense=4+L/3, exp=6+L*3, gold=4+L*2
spirit:    hp=22+L*6,  mp=16+L*3, attack=10+L*2, defense=2+L/3, exp=6+L*3, gold=5+L*2
elemental: hp=30+L*7,  mp=20+L*4, attack=11+L*2, defense=3+L/3, exp=7+L*3, gold=5+L*2
stone:     hp=42+L*9,  mp=12+L*2, attack=8+L*2,  defense=6+L/2, exp=8+L*3, gold=5+L*2
humanoid:  hp=30+L*7,  mp=12+L*2, attack=9+L*2,  defense=3+L/3, exp=6+L*3, gold=6+L*2
soldier:   hp=34+L*8,  mp=10+L,   attack=10+L*2, defense=4+L/3, exp=7+L*3, gold=7+L*2
boss:      hp=80+L*12, mp=24+L*4, attack=14+L*3, defense=8+L/2, exp=20+L*5, gold=25+L*4
```

## Monster Table

| ID | 名称 | 首次场景 | 等级 | 类型 | HP | MP | 攻击 | 防御 | 经验 | 铜钱 | 掉落 |
| - | - | - | -: | - | -: | -: | -: | -: | -: | -: | - |
| 1 | 野猪 | 06野猪林_01.sce | 6 | beast | 68 | 16 | 19 | 4 | 22 | 15 | - |
| 3 | 黑蝙蝠 | 01桃花岛_02.sce | 1 | flying | 23 | 13 | 10 | 1 | 7 | 5 | - |
| 4 | 黄色蜜蜂 | 01桃花岛_03.sce | 2 | insect | 26 | 14 | 12 | 1 | 10 | 7 | - |
| 6 | 老野猪 | 06野猪林_03.sce | 7 | beast | 75 | 17 | 21 | 4 | 25 | 17 | - |
| 9 | 掘地鼠 | 03丹霞山_02.sce | 3 | beast | 47 | 13 | 13 | 3 | 13 | 9 | - |
| 13 | 白灵蟒 | 09华山_03.sce | 12 | boss | 224 | 72 | 50 | 14 | 80 | 73 | - |
| 18 | 岩浆石火 | 22火焰山_04.sce | 38 | elemental | 296 | 172 | 87 | 15 | 121 | 81 | - |
| 19 | 岳老三 | 17无情谷_03.sce | 28 | boss | 416 | 136 | 98 | 22 | 160 | 137 | - |
| 22 | 碧纱幽火 | 10封神坛_01.sce | 12 | spirit | 94 | 52 | 34 | 6 | 42 | 29 | - |
| 25 | 恶臭僵尸 | 05上古皇陵_02.sce | 4 | undead | 66 | 14 | 16 | 5 | 18 | 12 | - |
| 28 | 灰蝙蝠 | 06野猪林_02.sce | 7 | flying | 53 | 19 | 22 | 2 | 25 | 17 | - |
| 29 | 鬼蝙蝠 | 06野猪林_04.sce | 8 | flying | 58 | 20 | 24 | 3 | 28 | 19 | - |
| 30 | 雪岩怪 | 07泰山_01.sce | 8 | stone | 114 | 28 | 24 | 10 | 32 | 21 | - |
| 31 | 苗人教徒 | 07泰山_02.sce | 9 | humanoid | 93 | 30 | 27 | 6 | 33 | 24 | - |
| 32 | 灰疫鼠 | 09华山_01.sce | 10 | beast | 96 | 20 | 27 | 5 | 34 | 23 | - |
| 34 | 碎骨兵 | 09华山_02.sce | 11 | undead | 122 | 21 | 30 | 7 | 39 | 26 | - |
| 36 | 青石怪 | 10封神坛_05.sce | 14 | stone | 168 | 40 | 36 | 13 | 50 | 33 | - |
| 40 | 碧磷鬼火 | 13剑阁_02.sce | 20 | spirit | 142 | 76 | 50 | 8 | 66 | 45 | - |
| 41 | 恶臭胶质 | 13剑阁_01.sce | 20 | slime | 96 | 58 | 46 | 7 | 43 | 43 | - |
| 42 | 白焰鬼 | 15剑冢_01.sce | 22 | elemental | 184 | 108 | 55 | 10 | 73 | 49 | - |
| 45 | 红鬼火 | 15剑冢_02.sce | 22 | spirit | 154 | 82 | 54 | 9 | 72 | 49 | - |
| 47 | 持刀日月教徒 | 17无情谷_02.sce | 27 | humanoid | 219 | 66 | 63 | 12 | 87 | 60 | - |
| 48 | 长矛兵 | 17无情谷_04.sce | 28 | soldier | 258 | 38 | 66 | 13 | 91 | 63 | - |
| 49 | 杂斑野猪 | 20黑龙潭_03.sce | 31 | beast | 243 | 41 | 69 | 12 | 97 | 65 | - |
| 50 | 金蜜蜂 | 20黑龙潭_01.sce | 30 | insect | 166 | 42 | 68 | 7 | 94 | 63 | - |
| 51 | 蓝灯鬼 | 20黑龙潭_02.sce | 31 | spirit | 208 | 109 | 72 | 12 | 99 | 67 | - |
| 52 | 赤炼蛇 | 19金蛇谷_01.sce | 28 | reptile | 192 | 42 | 64 | 11 | 89 | 60 | - |
| 53 | 白鳞蛇 | 19金蛇谷_03.sce | 29 | reptile | 198 | 43 | 66 | 11 | 92 | 62 | - |
| 54 | 碧竹蛇 | 19金蛇谷_02.sce | 29 | reptile | 198 | 43 | 66 | 11 | 92 | 62 | - |
| 55 | 死刀兵 | 21幽冥鬼府_04.sce | 34 | undead | 306 | 44 | 76 | 15 | 108 | 72 | - |
| 56 | 巨锤骷髅 | 21幽冥鬼府_05.sce | 34 | undead | 306 | 44 | 76 | 15 | 108 | 72 | - |
| 57 | 长枪鬼卒 | 21幽冥鬼府_01.sce | 32 | undead | 290 | 42 | 72 | 14 | 102 | 68 | - |
| 60 | 碎颅骷髅 | 22火焰山_02.sce | 36 | undead | 322 | 46 | 80 | 16 | 114 | 76 | - |
| 64 | 日月教徒 | 22火焰山_03.sce | 37 | humanoid | 289 | 86 | 83 | 15 | 117 | 80 | - |
| 67 | 拿枪士兵 | 17无情谷_03.sce | 27 | soldier | 250 | 37 | 64 | 13 | 88 | 61 | - |
| 69 | 腐血骸骨 | 16锁妖塔_05.sce | 24 | undead | 226 | 34 | 56 | 12 | 78 | 52 | - |
| 70 | 阴风鬼灯 | 16锁妖塔_03.sce | 24 | spirit | 166 | 88 | 58 | 10 | 78 | 53 | - |
| 71 | 鬼岩石 | 16锁妖塔_02.sce | 23 | stone | 249 | 58 | 54 | 17 | 77 | 51 | - |
| 73 | 上古遗骸 | 05上古皇陵_03.sce | 5 | undead | 74 | 15 | 18 | 5 | 21 | 14 | - |
| 74 | 箭簇骷髅 | 05上古皇陵_04.sce | 5 | undead | 74 | 15 | 18 | 5 | 21 | 14 | - |
| 75 | 骸骨禁卫 | 05上古皇陵_05.sce | 6 | undead | 82 | 16 | 20 | 6 | 24 | 16 | - |
| 76 | 饿虎 | 03丹霞山_01.sce | 3 | beast | 47 | 13 | 13 | 3 | 13 | 9 | - |
| 77 | 染血骷髅 | 03丹霞山_03.sce | 4 | undead | 66 | 14 | 16 | 5 | 18 | 12 | - |
| 78 | 死骨锤兵 | 07泰山_03.sce | 9 | undead | 106 | 19 | 26 | 7 | 33 | 22 | - |
| 79 | 青苔岩精 | 09华山_03.sce | 11 | stone | 141 | 34 | 30 | 11 | 41 | 27 | - |
| 81 | 灯焰鬼 | 10封神坛_03.sce | 13 | spirit | 100 | 55 | 36 | 6 | 45 | 31 | - |
| 82 | 黑斑虎 | 11终南山_01.sce | 15 | beast | 131 | 25 | 37 | 7 | 49 | 33 | - |
| 83 | 白光虎 | 11终南山_02.sce | 15 | beast | 131 | 25 | 37 | 7 | 49 | 33 | - |
| 84 | 幽蓝鬼火 | 11终南山_03.sce | 17 | spirit | 124 | 67 | 44 | 7 | 57 | 39 | - |
| 86 | 墓地士兵 | 12活死人墓_01.sce | 17 | soldier | 170 | 27 | 44 | 9 | 58 | 41 | - |
| 87 | 流窜苗人 | 12活死人墓_02.sce | 17 | humanoid | 149 | 46 | 43 | 8 | 57 | 40 | - |
| 89 | 近卫士兵 | 12活死人墓_05.sce | 18 | soldier | 178 | 28 | 46 | 10 | 61 | 43 | - |
| 91 | 蜜蜂群 | 13剑阁_03.sce | 21 | insect | 121 | 33 | 50 | 5 | 67 | 45 | - |
| 92 | 暗紫尸骸 | 15剑冢_03.sce | 23 | slime | 108 | 64 | 52 | 7 | 49 | 49 | - |
| 94 | 黄色蜜蜂群 | 17无情谷_01.sce | 26 | insect | 146 | 38 | 60 | 6 | 82 | 55 | - |
| 97 | 赤焰怪 | 22火焰山_01.sce | 36 | elemental | 282 | 164 | 83 | 15 | 115 | 77 | - |
| 98 | 金毛太岁 | 23蟠龙寨_01.sce | 38 | slime | 168 | 94 | 82 | 11 | 79 | 79 | - |
| 99 | 白光虎 | 23蟠龙寨_04.sce | 39 | beast | 299 | 49 | 85 | 15 | 121 | 81 | - |
| 101 | 日月教徒 | 23蟠龙寨_05.sce | 39 | humanoid | 303 | 90 | 87 | 16 | 123 | 84 | - |
| 103 | 拿枪教徒 | 23蟠龙寨_07.sce | 40 | humanoid | 310 | 92 | 89 | 16 | 126 | 86 | - |
| 104 | 近卫教徒 | 23蟠龙寨_08.sce | 41 | humanoid | 317 | 94 | 91 | 16 | 129 | 88 | - |
| 105 | 毒泥怪 | 01桃花岛_01.sce | 1 | slime | 20 | 20 | 8 | 2 | 5 | 5 | 小长命散 10% |
| 106 | 瘴气蝙蝠 | 01桃花岛_04.sce | 2 | flying | 28 | 14 | 12 | 1 | 10 | 7 | - |
| 107 | 蓝鬼灯 | 05上古皇陵_03.sce | 6 | spirit | 58 | 34 | 22 | 4 | 24 | 17 | - |
| 110 | 紫云鬼 | 27深渊沼泽_01.sce | 46 | stone | 456 | 104 | 100 | 29 | 146 | 97 | - |
| 111 | 蓝云鬼 | 27深渊沼泽_03.sce | 48 | stone | 474 | 108 | 104 | 30 | 152 | 101 | - |
| 112 | 绿云鬼 | 27深渊沼泽_06.sce | 50 | stone | 492 | 112 | 108 | 31 | 158 | 105 | - |
| 120 | 地府魔蝎 | 28地府门道_01.sce | 50 | reptile | 324 | 64 | 108 | 18 | 155 | 104 | - |
| 121 | 怒火魔头 | 28地府门道_02.sce | 52 | elemental | 394 | 228 | 115 | 20 | 163 | 109 | - |
| 122 | 封印僵尸 | 28地府门道_04.sce | 53 | undead | 458 | 63 | 114 | 21 | 165 | 110 | - |
| 200 | 炼狱鬼火 | 25华山洞窟_01.sce | 43 | spirit | 280 | 145 | 96 | 16 | 135 | 91 | - |
| 201 | 炼狱骷髅 | 25华山洞窟_03.sce | 44 | undead | 386 | 54 | 96 | 18 | 138 | 92 | - |
| 202 | 炼狱鬼兵 | 25华山洞窟_04.sce | 45 | undead | 394 | 55 | 98 | 19 | 141 | 94 | - |

## Validation

Build:

```text
make
result: passed
```

## Unknowns

- There is still no recovered live drop table. Only the user-specified poison
  slime drop is active. The stat layer supports `dropItemId/dropRatePercent`,
  so later recovered drops can be added without changing battle flow.
- Player skill formulas are not yet recovered. The current normal attack uses a
  conservative server-side default based on role strength and level.
