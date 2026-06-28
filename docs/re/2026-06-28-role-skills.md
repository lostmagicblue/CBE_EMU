# 2026-06-28 Role Skills

## Data Source

Local skill table:

```text
bin/JHOnlineData/skill.dsh
```

Header values:

```text
columns=32
rows=74
```

Relevant columns:

```text
ID, 名称, 技能图片, 说明, 等级, 法术标示, 职业
```

The `职业` column is the real job discriminator:

```text
0 = 天机
1 = 幻剑
2 = 鬼道
```

The later columns named `天机 / 幻剑 / 鬼道` are coefficient columns in this
table. They are all `100` in the current file and must not be used as class
membership flags.

## Client Parser Evidence

`JianghuOL.CBE:net_handle_business_followup_events(0x01010594)` handles WT
object `1/12/1`:

1. Calls `LoadSkillDataSheet(0x0103550E)` to load `skill.dsh`.
2. Reads `learnednum`.
3. Opens the `learnedskill` blob as a tagged stream.
4. Reads one skill ID per learned row.
5. Calls `MarkListItemActive()` for each ID.

Therefore the server only needs to send learned skill IDs. Names, descriptions,
effects, and icons are supplied by the local `skill.dsh`.

## Unlock Rule

The mock server now computes learned skills from the active role:

```text
learned_count = 1 + floor(level / 5)
```

So level 1 starts with one skill, then new skills unlock at level 5, 10, 15,
20, and so on. The list is capped by the number of skills available for the
role's profession.

Response source remains the existing skill-tail object family:

```text
1/12/1 { learnednum, learnedskill }
1/7/42 { booknum, booksinfo }
1/17/1 { iteminfo }
```

## 天机 Skills

| Order | DSH Level | ID | Name |
| --- | ---: | ---: | --- |
| 1 | 1 | 1 | 万剑诛仙1 |
| 2 | 3 | 11 | 灵岩护身1 |
| 3 | 5 | 21 | 雷震八方1 |
| 4 | 7 | 31 | 神臂担山1 |
| 5 | 9 | 41 | 破甲烈刃1 |
| 6 | 14 | 2 | 万剑诛仙2 |
| 7 | 15 | 12 | 灵岩护身2 |
| 8 | 20 | 51 | 神堂静默 |
| 9 | 24 | 32 | 神臂担山2 |
| 10 | 26 | 42 | 破甲烈刃2 |
| 11 | 27 | 3 | 万剑诛仙3 |
| 12 | 27 | 13 | 灵岩护身3 |
| 13 | 30 | 22 | 雷震八方2 |
| 14 | 39 | 14 | 灵岩护身4 |
| 15 | 40 | 4 | 万剑诛仙4 |
| 16 | 41 | 33 | 神臂担山3 |
| 17 | 43 | 43 | 破甲烈刃3 |
| 18 | 51 | 15 | 灵岩护身5 |
| 19 | 53 | 5 | 万剑诛仙5 |
| 20 | 55 | 23 | 雷震八方3 |
| 21 | 58 | 34 | 神臂担山4 |
| 22 | 60 | 44 | 破甲烈刃4 |

## 幻剑 Skills

| Order | DSH Level | ID | Name |
| --- | ---: | ---: | --- |
| 1 | 1 | 101 | 风舞刃行1 |
| 2 | 3 | 111 | 混沌鬼气1 |
| 3 | 5 | 121 | 荒魂劫火1 |
| 4 | 7 | 131 | 形神离散 |
| 5 | 9 | 141 | 毒入膏肓1 |
| 6 | 11 | 151 | 尸鬼召还 |
| 7 | 13 | 161 | 电闪神兵1 |
| 8 | 14 | 102 | 风舞刃行2 |
| 9 | 15 | 112 | 混沌鬼气2 |
| 10 | 26 | 142 | 毒入膏肓2 |
| 11 | 27 | 103 | 风舞刃行3 |
| 12 | 27 | 113 | 混沌鬼气3 |
| 13 | 30 | 122 | 荒魂劫火2 |
| 14 | 35 | 162 | 电闪神兵2 |
| 15 | 39 | 114 | 混沌鬼气4 |
| 16 | 40 | 104 | 风舞刃行4 |
| 17 | 43 | 143 | 毒入膏肓3 |
| 18 | 51 | 115 | 混沌鬼气5 |
| 19 | 53 | 105 | 风舞刃行5 |
| 20 | 55 | 123 | 荒魂劫火3 |
| 21 | 57 | 163 | 电闪神兵3 |
| 22 | 60 | 144 | 毒入膏肓4 |

## 鬼道 Skills

| Order | DSH Level | ID | Name |
| --- | ---: | ---: | --- |
| 1 | 1 | 201 | 绯炎幻法1 |
| 2 | 3 | 211 | 清风拂体1 |
| 3 | 5 | 221 | 道统天地1 |
| 4 | 7 | 231 | 天火熔身1 |
| 5 | 9 | 241 | 妙慧通灵1 |
| 6 | 11 | 251 | 五雷天殛1 |
| 7 | 13 | 261 | 三花聚顶1 |
| 8 | 15 | 202 | 绯炎幻法2 |
| 9 | 15 | 212 | 清风拂体2 |
| 10 | 16 | 222 | 道统天地2 |
| 11 | 23 | 232 | 天火熔身2 |
| 12 | 26 | 242 | 妙慧通灵2 |
| 13 | 27 | 213 | 清风拂体3 |
| 14 | 27 | 223 | 道统天地3 |
| 15 | 27 | 252 | 五雷天殛2 |
| 16 | 29 | 203 | 绯炎幻法3 |
| 17 | 33 | 262 | 三花聚顶2 |
| 18 | 38 | 224 | 道统天地4 |
| 19 | 39 | 214 | 清风拂体4 |
| 20 | 39 | 233 | 天火熔身3 |
| 21 | 43 | 204 | 绯炎幻法4 |
| 22 | 43 | 243 | 妙慧通灵3 |
| 23 | 43 | 253 | 五雷天殛3 |
| 24 | 49 | 225 | 道统天地5 |
| 25 | 51 | 215 | 清风拂体5 |
| 26 | 53 | 263 | 三花聚顶3 |
| 27 | 55 | 234 | 天火熔身4 |
| 28 | 57 | 205 | 绯炎幻法5 |
| 29 | 59 | 254 | 五雷天殛4 |
| 30 | 60 | 244 | 妙慧通灵4 |

## Runtime Trace

The skill-tail response logs:

```text
mock_role_skills role=... job=... level=... learned=... ids=...
```
