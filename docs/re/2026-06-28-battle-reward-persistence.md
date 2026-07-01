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
  two small value fields. `reward type == 1` enters an equipment/detail
  registration helper at `0x76C6`; runtime with a short ordinary consumable row
  crashed there. `reward type == 3` adds a money value. Runtime with
  `reward type == 2` enlarged the panel but left the item row blank.

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
drop_rate = 100% (temporary test setting; was 10%)
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
  late for the result panel. Terminal operate responses therefore keep `4/7`
  inline with the final `4/6` action. Current evidence uses action-first object
  order and disables the extra type-3 terminal action by default, because that
  extra visual record can disturb multi-monster target state.
  `HandleBattleSettleMsg(0x743C)` treats the `exp` field as total EXP and the
  `gold` field as total money, then computes the visible gained values by
  subtracting the old client-side actor fields. Therefore `4/7.exp` must be the
  role's post-reward total EXP, not the per-kill delta.
- The `hp` and `mp` fields in `4/7` are settlement-panel recovery amounts, not
  the role's persisted current HP/MP. Current battle HP/MP remains stored from
  the server-side battle state; packet `hp/mp` now default to `0/0` so the panel
  shows no automatic recovery unless explicit recovery mechanics are added.
- Battle-result drop persistence still grants the actual item into the active
  role backpack when the reward roll succeeds. The visible popup text uses the
  `4/7.fdata` settlement text field, which `HandleBattleSettleMsg` reads at
  `0x7B08` and the result panel renders at `0x4462`.

The unsafe `iteminfo` ordinary row shape is retained here only as parser
evidence:

```text
u32 owner_role_id
u8  display_flag = 1
u32 item_id
string item_name
u8  reward_type = 2
i16 count_or_seq = 1
u32 value = 1
```

IDA correction: `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)`
compares the row `owner_role_id` with the current battle actor id at
`*(battleR9+0x286C)+0x64` before copying the item name. If this id does not
match, the parser skips the row while `itemnum` has already enlarged the result
panel, producing a blank item line. The mock therefore uses the role id sent in
the battle-start `battleinfo` for settlement item rows, not merely the active
local role-db id.

Second correction: owner match alone is not sufficient. `reward_type=2` follows
the non-money parse-only branch at `0x77FC`, so the result panel reserves a row
but does not reliably display text. `reward_type=1` is not an ordinary item
solution: runtime after switching to type `1` crashed at the helper reached
from `0x76C6` (`pc=0x51896CA`, invalid access near `0x64000098`). Keep
`itemnum=0/iteminfo=""` for ordinary consumable drops and put the visible line
in `fdata` instead.

The `fdata` line is display-only for the result panel; the actual durable item
has already been inserted through the active role backpack state.

Backpack refresh after a battle drop is a separate client contract. The result
panel `4/7` object can show text, but it does not update the live item manager.
`JianghuOL.CBE:net_handle_misc_player_fields(0x01011D16)` dispatches kind
`7`, subtype `37` to `HandleItemAcquire(0x0101191A)`. That handler reads:

```text
msg      string, rendered as the acquire message
result   u8, success when 0
itemid   u32
seq      u16
itemname string
```

It can insert the item on `result == 0`, but it always renders `msg` before the
insert. Runtime after adding it to the same packet as the terminal battle
action showed an early "drop acquired" popup before the kill/settlement flow was
visibly finished. Do not use `7/37` for battle drops unless a later no-popup
variant is found.

The no-popup refresh path is `mmGameMstarWqvga.cbm:sub_D04(0x0D04)`, reached
from the scene callback for kind `7`, subtype `7`. A `type=1` row is an
add/update operation and its `iteminfo` stream is:

```text
u8  row_count
repeat row_count:
  i16 seq
  u32 item_id
  u32 count_delta
  common item-extra block
```

The mock therefore appends a one-shot `1/7/7 { type=1, iteminfo }` after the
post-battle scene follow-up, not inside the battle operate packet. Runtime
showed two possible follow-up shapes: the usual short `25/5` scene-default event
and an occasional `split-safe-combo`. The refresh helper is attached to both,
with battle-session serial de-duplication, so the first actual client request
wins. The row uses the role DB `seq` returned by the server-side drop grant and
a `count_delta` equal to the number of items dropped in that battle. Runtime
after sending the full server-side stack count (`6`) showed the client adding
6 more items, so this branch is additive rather than a set-count refresh. Do
not use `7/1` for this path:
`HandleItemOperationResponse(0x01033544)` requires the client-side pending
item-use pointer at `r9+38036`, which is absent for battle rewards. `17/1`
remains the explicit full-list response for opening the backpack UI, not the
battle-drop acquire event.

Role normalization was tightened to clamp HP/MP only when they exceed max
values. It no longer treats `hp == 0` or `mp == 0` as "missing data", because
that destroyed persisted current battle state whenever position/backpack saves
normalized the role.

## Remaining Unknown

The ordinary item drop display now uses `fdata`. Equipment-specific reward
rows still need a dedicated sample before adding equipment attribute payload
fields or using `reward_type=1`.
