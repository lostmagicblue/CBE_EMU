# 2026-06-28 Battle Reward And Role Persistence

## Phase

Implement the first persisted monster reward loop for the local Jianghu OL mock
server:

- killing the default scene monster `actor_id=105` grants 5 EXP;
- killing that monster also grants 5 copper;
- the same kill rolls a 10% drop into the active role backpack;
- current HP/MP, total EXP, derived level, money, position, and backpack state
  remain attached to the selected role row in `nvram/jhol_mock_roles.bin`.

## Evidence

Runtime/battle evidence from `2026-06-25-battle-server-flow.md` already ties the
current scene-monster path to `actor_id=105`. This is the duplicated local SCE
row used for the touched poison slime battle.

IDA evidence:

- `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` reads the settlement
  object fields in this order: `exp`, `lastexp`, `curexp`, `persentexp`,
  `energy`, `energymax`, `gold`, `level`, `result`, `bagstatus`, `hp`, `mp`,
  `itemnum`, `iteminfo`.
- `iteminfo` is handed to the battle module's stream reader after the raw field
  lookup. The tail block reached from `0x7612 -> 0x78C6` reads one row per
  `itemnum`: owner role id, display flag, item id, item name, reward type, and
  two small value fields. `reward type == 1` enters equipment attribute parsing;
  `reward type == 3` adds a money value; ordinary dropped items use type `2`.

Local resource evidence:

- `bin/JHOnlineData/item.dsh` has no bare `长命散` row.
- The closest low-level HP medicine is row `304`, `小长命散`, description
  `加850生命`, stack `20`.
- The mock therefore maps the requested "长命散" poison-slime drop to item ID
  `304` until a more exact live-server drop table is recovered.

## Implementation

`src/mock-server.c` now records the current battle enemy ID when handling the
scene challenge request. The default poison slime constants are:

```text
enemy_id = 105
reward_exp = 5
reward_gold = 5
drop_item_id = 304
drop_rate = 10%
```

Reward grant is guarded by `g_mockBattleOperateSessionSerial`. Both the normal
operate path and the relaxed fallback path call a terminal-save helper when a
round ends. That helper persists the active role's current HP/MP and grants the
reward only once per battle session. If the later `1/4/7` settlement object path
also runs, it can display the same reward cache without applying EXP a second
time.

Runtime correction:

- The first reward implementation saved EXP/HP/MP into the role DB when the
  terminal action ended, but the later `g_mockBattleAwaitingSettlement` branch
  still answered the next battle request with only `4/11 {result=1,type=0}`.
  The client result panel gets "获得经验" and "金钱" from
  `HandleBattleSettleMsg(0x743C)`, which only runs for server command case
  `4/7`.
- The waiting-settlement branch now sends a one-shot packet containing
  `4/7 + 4/11 + 4/9`. `4/7` carries the cached reward fields and total money,
  while later duplicate waiting-settlement requests fall back to the old `4/11`
  auto-battle-off response.
- A later runtime check showed that waiting for the next request was still too
  late for the result panel. The terminal action in `4/6` can bring up the
  panel immediately, so terminal operate responses place `4/7` before `4/6` by
  default. `HandleBattleSettleMsg(0x743C)` treats the `exp` field as total EXP
  and the `gold` field as total money, then computes the visible gained values
  by subtracting the old client-side actor fields. Therefore `4/7.exp` must be
  the role's post-reward total EXP, not the per-kill delta.
- The `hp` and `mp` fields in `4/7` are settlement-panel recovery amounts, not
  the role's persisted current HP/MP. Current battle HP/MP remains stored from
  the server-side battle state; packet `hp/mp` now default to `0/0` so the panel
  shows no automatic recovery unless explicit recovery mechanics are added.
- Battle-result drop display now sends `itemnum=1` and one `iteminfo` row when
  the reward roll inserted a backpack item. The row is:

```text
u32 owner_role_id
u8  display_flag = 1
u32 item_id
string item_name
u8  reward_type = 2
i16 count_or_seq = 1
u32 value = 1
```

The row is display-only for the result panel; the actual durable item has
already been inserted through the active role backpack state.

Role normalization was tightened to clamp HP/MP only when they exceed max
values. It no longer treats `hp == 0` or `mp == 0` as "missing data", because
that destroyed persisted current battle state whenever position/backpack saves
normalized the role.

## Remaining Unknown

The ordinary dropped-item row is recovered enough to show item drops on the
result panel. Equipment reward rows still need a dedicated sample before using
`reward_type=1`, because that branch enters the larger equipment attribute
parser.
