# Battle Mainline

This is the stable working model for Jianghu OL battle server recovery. Use it before changing
`4/1`, `4/2`, `4/6`, `4/7`, `4/8`, `4/9`, `4/10`, or `4/11` mock behavior.

## Rule

Do not start a battle task by guessing packet fields. First map the packet to this model:

1. request trigger
2. Battle.cbm dispatch branch
3. field read order
4. materialized local state
5. required side effect
6. visible/runtime gate

Only add a packet experiment when it targets one named unknown in this file. If a result rejects a
hypothesis, record it here so the same shape is not retried.

## Static Evidence Baseline

IDA instance used for this pass: `yyej` (`mmBattleMstarWqvga.cbm`).

Full pseudocode exports for this pass were written to `tmp/ida_battle_static/`:

- `0x7BD0`: `HandleServerBattleCmd`
- `0x6EB0`: `HandleBattleActionMsg`
- `0x4B38`: local state-4 HP merge helper
- `0x31FA`: `DrawBattleAnimEffect`
- `0x743C`: `HandleBattleSettleMsg`

Runtime address notes in older docs often use the loaded `0x0518xxxx` range. The stable IDA
offsets above are the preferred references in this file.

## Server Command Dispatcher

Function: `HandleServerBattleCmd()` at Battle.cbm offset `0x7BD0`.

Confirmed dispatch table:

| subtype | Meaning | Static behavior |
| --- | --- | --- |
| `4/1`, `4/9` | result branch | reads `result`; `result=1` uses gates at main object `+0x470/+0x474`; other results display messages/cleanup |
| `4/4` | escape/leave branch | only active when mode from `screenObj+72` is `2`; `result=1` clears death markers and updates attrs, but user-visible semantic is escape |
| `4/5`, `4/10` | battle start | clears battle wait flags and calls `HandleBattleStartMsg()` |
| `4/6` | action response | only active when mode is `2`; clears `**(R9+0x2868)`, calls `HandleBattleActionMsg(a1+12, packet, reader, slot)`, clears `a1+2488`, then `SetBattlePhase(5)` |
| `4/7` | settle/status | only active when mode is `2`; clears `**(R9+0x2868)` and calls `HandleBattleSettleMsg()` |
| `4/8` | apply/autorevive | reads `autorevive` and `info`; `autorevive=1` copies `info` into `R9+0x3480`-family buffer, resets flags, sets state bytes, and calls `BattleSettle_UpdateCharAttrs(0)` |
| `4/11` | terminal gate setter | reads `result` and `type`; `result=1,type=1` sets the gate that lets later `4/9 result=1` reach victory branch |

Confirmed implication: terminal objects are not a substitute for a correct `4/6` action response.
They can pass gates, but if the local fighter table never saw HP/death state, victory settlement is
semantically premature.

## Battle Start Contract

Live request:

- `4/1` challenge/interact request with fields such as `id`, `index`, `posx`, `posy`.

Current confirmed response shape:

- optional `1/5/5 { groupinfo=<subtype-5 template> }`
- then `1/4/10 { side=0, battleinfo=<raw typed stream> }`

Confirmed `4/10.battleinfo` stream shape in current mock:

```text
ownCount
  own: id, hp, maxHp, mp, maxMp, name, visualByteA, visualByteB
enemyCount
  enemy: id, hp, maxHp, mp, maxMp
```

Confirmed enemy-template table issue:

- Battle.cbm start parser resolves enemies through the compact enemy-template table at
  `battleState+0x50`.
- If `battleinfo.enemyId` is not already present there, `sub_66A4()` takes the lookup-failed path
  and later render/stage callbacks can be null.
- Same-packet `1/5/5` before `1/4/10` can populate the compact table early enough.
- Confirmed subtype-5 template grammar is:

```text
raw id
seq string name
seq u8 byte34
seq u8 byte35
seq u32 stat0
seq u32 stat1
seq u32 stat2
seq u32 stat3
```

Clarification: `groupinfo` itself is carried as a blob field, so the field value starts with the
blob's inner `len16`. The subtype-5 first scalar reader consumes that `len16` plus the following
four id bytes. The builder must not add a second per-id sequence prefix before `id`.

Current stable goal: keep battle start focused on a real enemy template plus `4/10`; do not use
fallback player/self templates as a semantic fix.

## Action Response Contract

Function: `HandleBattleActionMsg()` at Battle.cbm offset `0x6EB0`.

Static field order:

1. `sub_6D12(slot)` before named action fields.
2. `sub_6DBC(slot)` before named action fields.
3. lookup wrapper for field `actioninfo`.
4. import the `actioninfo` wrapper through reader owner `R9+0x204C` slot `+0x0C`.
5. read field `actionnum`.
6. loop `actionnum` times, materializing one 100-byte local action record per iteration.

Important correction:

- `sub_6D12()` and `sub_6DBC()` are real prelude consumers, but the current empty `iteminfo` and
  `teaminfo` field experiments are rejected. Empty/raw/tagged-empty variants caused wrapper
  misreads into neighboring packet metadata. Current mock omits those fields until their real
  record grammar is recovered.

Materialized action record layout from `0x6EB0`:

| local offset | Source | Meaning |
| --- | --- | --- |
| `+0` | set by parser | record occupied |
| `+1` | reader `u8` | action type |
| `+2` | reader `u8` through `CalcTargetSideIndex()` | actor/source side index |
| `+3` | reader `u8` | child/effect count, must be `1..6` for normal path |
| `+20 + 12*n` | set `0` | child local flag/status |
| `+21 + 12*n` | reader `u8` through `CalcTargetSideIndex()` | child target side index |
| `+22 + 12*n` | reader `u8` | child flag |
| `+24 + 12*n` | reader `u32` | value A, currently visible damage text for type-1 |
| `+28 + 12*n` | reader `u32` | value B, remaining-HP trial was parser-safe but HP-inert |
| `+92` | reader `u32` | effect/template index |
| `+4..+15` | reader blob/string copy for type `1`/`2` | payload text/resource scratch |
| `+96..+98` | three reader `u8` values | tail/effect bytes |

Type-specific notes:

- Type `1` records materialize `valueA/valueB` and visible damage text, but current evidence shows
  they do not by themselves commit HP.
- Type `2` records select/copy an effect template and run the same callback family, but prior safe
  type-2/effect experiments did not produce a confirmed local KO.
- `actionnum=0` is parser-valid but only exercises an early-return path.

Current confirmed negative:

- `valueB=remainingHp` is rejected as sufficient HP contract. Runtime materialized
  `valueA/valueB=12/8` and `8/0`, but canonical enemy HP stayed `20/20` and pending deltas stayed
  `0,0`.

## HP Commit Chain

Function: `sub_4B38()` at Battle.cbm offset `0x4B38`.

Static behavior:

- reads local action byte `record+0x0D`.
- if byte is `3`, calls callback at `record+0xA14`.
- if byte is `4`, commits pending deltas:

```text
fighter[activeSide].hp_like_a += signed16(R9+0x34B4)
fighter[activeSide].hp_like_b += signed16(R9+0x34B6)
R9+0x34B4 = 0
R9+0x34B6 = 0
```

Then it toggles screen/runtime flags, calls `sub_2C50()`, and refreshes UI through platform
callbacks.

Confirmed implication:

- The missing `4/6` contract must make the action/effect pipeline produce either
  `record+0x0D == 4` with nonzero `R9+0x34B4/+0x34B6`, or some earlier equivalent that changes
  the fighter table before terminal settlement.
- Terminal `4/7/8/11/9` cannot repair missing local HP deltas after the fact.

## Settle And Terminal Objects

Function: `HandleBattleSettleMsg()` at Battle.cbm offset `0x743C`.

Confirmed field family for subtype `4/7`:

```text
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
combatinfo
autorevive
info
fbs
fdata
```

Current use:

- Treat `4/7` as settle/reward/status, not as the source of monster death animation.
- `combatinfo` may matter for a nonblank result prompt, but it is not proven to commit fighter HP.

Confirmed terminal gates:

- `4/8 { result=1, autorevive=1, info=... }` reaches apply windows and `sub_4B70`/`sub_4B38`
  neighborhood when mode is still `2`.
- `4/11 { result=1, type=1 }` sets the gate byte consumed by `4/9`.
- `4/9 { result=1 }` reaches the victory branch only after the `4/11` gate.
- `4/4 { result=1 }` is parser-valid in the right mode but means escape/leave, not victory.

## Progress Gates

Use these gates to report every battle experiment:

| Gate | Required evidence |
| --- | --- |
| `G1 request` | client sends expected request, usually `4/1` or `4/2` in `bin/logs/net_packets.log` |
| `G2 dispatch` | Battle.cbm dispatches expected subtype, e.g. `trace_battle_kind4_subtype6_flow_start` |
| `G3 fields` | named fields are found in the expected order |
| `G4 materialize` | local action record bytes match intended type/actor/target/value fields |
| `G5 effect` | `DrawBattleAnimEffect` or equivalent callback path is reached |
| `G6 delta` | `R9+0x34B4/+0x34B6` becomes nonzero or `record+0x0D == 4` appears |
| `G7 commit` | fighter table HP changes at `sub_4B38` or an equivalent confirmed write |
| `G8 terminal` | `4/8`, `4/11`, and `4/9` gates are consumed in valid order |
| `G9 exit` | Battle screen exits or scene resumes without fallback prompt/loop |

Current blocker position:

- `G1` through `G4` are confirmed for the current type-1 `4/6` shape.
- `G5` is confirmed enough for the current blocker: the visible damage-number/effect cycle fires,
  and `trace_battle_state4_detail` reaches the state-machine store-target branch.
- `G6` and `G7` are not confirmed. This is the active blocker.
- Latest evidence from the refined trace pass: `state4_store_target` / `state4_target_copy_store`
  repeat with `record+0x0D == 1`, `state4cc=0`, `callback52c=2`, pending deltas `0,0`, and canonical
  enemy HP still `20/20`. No `record+0x0D == 4` HP-merge window was observed.

## Active Unknowns

Prioritized unknowns:

1. What exact action record/effect-template fields make `DrawBattleAnimEffect()` generate pending
   deltas?
2. Does a real non-empty `teaminfo` or `iteminfo` stream initialize owner/state needed by the
   action callback before `actioninfo` runs?
3. Does `4/8.info` contain a structured action/importer stream for post-action apply, or is it only
   terminal state once `4/6` has already committed local HP?
4. What is the real source of `record+0x0D == 4` in the action state machine?

Do not prioritize:

- More terminal ordering trials before `G6/G7` are solved.
- Repeating `valueB=remainingHp` as the only change.
- Reintroducing empty `iteminfo/teaminfo` variants already rejected by wrapper misreads.
- Falling back to player/self template ids as a semantic battle fix.

## Next Static/Runtime Pass

Current single packet experiment:

- hypothesis: for the parser-safe type-1 `4/6.actioninfo` record, the still-hardcoded
  `record+0x5C` effect/template index or tail bytes `+0x60..+0x62` select the callback/state path
  that can advance `record+0x0D` from `1` to `4` or produce `R9+0x34B4/+0x34B6` deltas.
- implementation: `src/mock-server.c` now exposes `CBE_BATTLE_TYPE1_EFFECT_INDEX` and
  `CBE_BATTLE_TYPE1_TAIL0/1/2`; defaults are all zero, preserving the current packet.
- expected gate: advance or reject `G6`; success is `record+0x0D == 4`, nonzero pending delta, or a
  canonical fighter HP change during/after `4/6`, before terminal `4/7/8/11/9`.
- observe: `mock_battle_operate_response`, `trace_battle_actioninfo_materialize_detail`,
  `trace_battle_state4_detail`, `trace_battle_anim_effect_delta_detail`, `trace_battle_apply_detail`,
  and canonical enemy HP in `trace_battle_module_state`.
- rejected if: changed effect/tail fields still materialize correctly but remain stuck at
  `record+0x0D == 1`, pending deltas `0,0`, and enemy HP `20/20`; then this field family is not
  sufficient and the next static target is the writer/state transition source for `record+0x0D`.

Before any broader packet shape:

1. Rerun the current build only to collect the refined `trace_battle_anim_effect_delta_detail`.
2. Count labels for `anim_*`, `state4_*`, and `damage_number_effect_entry`.
3. If `anim_*` appears but deltas stay zero, inspect the exact action record fields read by that
   branch.
4. If `anim_*` never appears, move static focus to the callback chosen at `record+92` / copied
   effect template, not to terminal packets.
5. If `state4_*` appears with zero deltas, recover the writer of `R9+0x34B4/+0x34B6`.

Only after this pass should the next packet matrix be designed.
