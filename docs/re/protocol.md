# Protocol Notes

This file is the canonical packet/protocol summary for the project.

Status tags used in this file:

- `confirmed`: established by code inspection, xrefs, logs, memory observation, or repeatable runtime behavior
- `hypothesis`: plausible inference that still needs direct confirmation

When adding new findings here:

- separate transport framing from message semantics
- record request triggers and response side effects
- mark each important claim as `confirmed` or `hypothesis`
- include enough detail for future server implementation

## WT Packet Framing

Current mock/server-side packet construction uses the `WT` container.

Status: `confirmed`

Framing:

- `out[0..1] = "WT"`
- `out[2..3] = packet_len` in big-endian order
- `out[4] = object_count`
- each object header is 6 bytes:
  - `major`
  - `kind`
  - `subtype`
  - `0`
  - `object_len_be16`

Field encoding inside an object:

- field name is encoded as `name_len, name`
- field value is encoded as `value_len_be16, value`
- integer field values are internally stored as `0, byte_len, big_endian_value`
- string/blob field values are internally stored as `data_len_be16, data`

Implementation helpers already exist in `src/mock-server.c` and should be reused instead of hand-building packet headers:

- `vm_net_mock_begin_wt_object`
- `vm_net_mock_finish_wt_object`
- `vm_net_mock_finish_wt_packet`
- `vm_net_mock_put_object_u8`
- `vm_net_mock_put_object_u32`
- `vm_net_mock_put_object_blob`
- `vm_net_mock_put_object_string`

## Battle Operate Request `4/2`

Status: `confirmed`

### 2026-06-13 current battle mainline contract

Current confirmed entry/start contract:

- `confirmed`: when the touched monster id is absent from Battle.cbm's compact enemy table, the mock can prepend `1/5/5 { groupinfo=<subtype-5 template> }` before `1/4/10 { battleinfo=... }`.
  - static evidence: main CBE `net_handle_group_info()` subtype `5` at `0x0101208C` reads `groupinfo` through reader slots `+0x20,+0x2C,+0x1C,+0x28,+0x28,+0x20*4`, then calls `AddRoleToList()` at `0x01012116`.
  - runtime evidence: latest `trace_groupinfo_case5_reader` reads id `3`, name `Monster`, bytes `1/0`, and stats `20/20/20/20`.
  - runtime evidence: Battle.cbm `sub_66CC_before_return` reports `enemyIds=10001,3,0,0` and `enemyHead id=3 hp/max=20/20`.
- `confirmed`: the live attack request remains `1/4/2 { index=0, Operate=0 }`.
  - packet evidence: latest `bin/logs/net_packets.log` ticks `317`, `345`, and `371`.
- `confirmed partial`: the stable visible attack reply remains `1/4/6 { actionnum=1, actioninfo=<type-1 tagged stream> }`.
  - runtime evidence: tick `317` first hit sends damage `12`, mock enemy HP becomes `8/20`.
  - runtime evidence: tick `345` lethal mock-side hit sends damage `8`, mock enemy HP becomes `0/20`.
- `confirmed negative`: the current action records do not decrement Battle.cbm's local enemy fighter HP.
  - runtime evidence: after the lethal hit, `trace_battle_local_action_state` still reports `valueAB=2,120 targetId=1`, and Battle module dumps keep `enemyHead ... 20/20`.
  - runtime evidence: terminal subtype-8 apply at `sub_4B70_battle_apply_entry` still sees `enemyIds=10001,3,0,0` with `enemyHead hp/max=20/20`.
- `confirmed negative`: the first broad local write watcher did not cover the useful action window.
  - runtime evidence: `trace_battle_local_state_write` began firing at pre-battle tick `70` with invalid local state (`state=209,209,...`) and nonsensical HP reads (`enemyHp=0/83954816`).
  - runtime evidence: its 512-entry budget was exhausted before the latest battle action ticks `490/515`, so it could not prove whether `4/6.actioninfo` wrote HP deltas.
- `confirmed negative`: the current terminal follow-up `1/4/7 + 1/4/8 + 1/4/11 + 1/4/9` breaks the extra operate loop but is semantically premature if local KO did not happen.
  - packet evidence: latest third `4/2` tick `371` receives the terminal composite; tick `372` then sends `25/5 + 2/1`.
  - runtime evidence: `mock_battle_operate_response ... terminalFollowup=1 ... enemyHp=0/20` is paired with client-local `enemyHead ... 20/20`.
- `confirmed negative`: the 2026-06-13 rerun still sends a third operate and reaches terminal with no local HP delta.
  - packet evidence: latest session ticks `217`, `243`, and `268` are the first hit, mock-lethal hit, and post-lethal terminal `4/2`.
  - runtime evidence: `sub_4B70_battle_apply_entry` at tick `269` reports `pendingDelta=0,0` and `enemyHead ... 20/20`.
- `confirmed negative`: the current `4/7` object does not yet provide a useful `combatinfo` payload.
  - runtime evidence: `trace_battle_status7_combatinfo_detail` fires only in the later `autorevive/fdata` window with `blobPtr=0, blobLen=0`; no `combatinfo_word*` checkpoints fire.

Current minimal server-side contract target:

- Keep `1/5/5 + 1/4/10` for real enemy template population.
- Keep non-terminal `1/4/6` parser shape while recovering the missing field/record semantics that make Battle.cbm commit enemy HP/KO before terminal.
- Use the trace-only local write watcher (`trace_battle_local_state_write`) to recover the missing actioninfo side effect:
  - it is now gated by Battle.cbm `R9+0x3450` parser state (`parseOk`, own/enemy counts, subtype) to avoid pre-battle noise.
  - it watches Battle.cbm `R9+0x2918` local battle/action state, `R9+0x374C` fighter tables, and `R9+0x34B4/0x34B6` pending deltas.
  - it reports canonical fighter HP from `R9+0x374C` and `R9+0x374C+0xC4`, matching existing `trace_battle_module_state` evidence.
  - evidence target: distinguish "no delta written" from "wrong slot" from "delta written then cleared before `sub_4B70`".
- Treat `4/7.combatinfo` as a separate settle/reward/prompt candidate, not as proven KO state:
  - static evidence: `HandleBattleSettleMsg()` (`0x0518935C`, off `0x743C`) reads `lastexp/curexp/persentexp/energy/energymax/gold/level/result/bagstatus/hp/mp/itemnum/iteminfo`, then a `combatinfo` stream at `0x051897D0..0x051899BA`, followed by `autorevive/info/fbs/fdata`.
  - trace-only instrumentation now records `combatinfo` stream cursor/blob/next bytes and read-return checkpoints at `0x05189910..0x051899CE`.
  - hypothesis: a non-empty `combatinfo` may be required for the nonblank reward/result prompt, but the monster death animation still requires the preceding `4/6.actioninfo` path to drive local fighter HP/death state.

### 2026-06-13 terminal-followup correction

Current live Battle.cbm flow differs from the older `docs/re/battle-flow.md` high-level end
summary: the client does not currently reach a main-scene `10/17`-style result object after
lethal damage. The active low-level window is still Battle.cbm kind `4`:

- `confirmed`: touch monster / challenge enters battle through `4/1 -> 1/4/10`.
  - latest packet evidence: `bin/logs/net_packets.log` tick `167`, request `4/1 id=105,index=3,pos=249,239`, response `1/4/10`, responseLen `117`.
- `confirmed`: player action remains `4/2 index=0 Operate=0`.
  - latest packet evidence: ticks `173`, `199`, `217`.
- `confirmed`: first action reply and lethal reply are parser-safe subtype `1/4/6`.
  - tick `173`: `1/4/6 { actionnum=2, actioninfoLen=90 }`, mock HP becomes player `110/120`, enemy `8/20`.
  - tick `199`: `1/4/6 { actionnum=1, actioninfoLen=45 }`, mock enemy HP becomes `0/20`.
- `confirmed negative`: terminal-only `1/4/7` is parser-safe but not sufficient to exit battle.
  - tick `217`: post-lethal `4/2` received a single `1/4/7` response, responseLen `244`.
  - runtime trace tick `218`: `case7_settle_result_check`, `before_status7_parser`, `sub_743C_status7_entry`, item/combat windows, and `after_status7_parser` all fire without crash.
  - same `after_status7_parser` trace still reports active Battle screen `01053f78`, `mainBattle=2,0,0,0`, and later render traces continue in Battle.cbm.
- `confirmed static`: terminal follow-up search must include subtype `4/8`.
  - Battle.cbm `HandleServerBattleCmd()` (`0x05189AF0`, off `0x7BD0`) subtype-8 branch `0x05189D16..0x05189E24` reads `result`, `autorevive`, and raw `info` pointer/length, then the success path calls the reset/apply sequence (`ResetBattleStateFlags` / `BattleSettle_UpdateCharAttrs` neighborhood).
  - prior runtime evidence for minimal subtype-8 gate: `1/4/8 { result=1, autorevive=1, info=<12 raw bytes, byte11=1> }` reaches `case8_apply_gate_check pc=05189D82` and `case8_apply_call pc=05189DB4`.

Updated terminal contract after the `4/11`, `4/8+4/4`, `4/7+4/4`, and `4/7+4/9` cleanup reruns:

- normal non-terminal turns stay on `1/4/6` tagged `actioninfo`.
- post-lethal follow-up now returns one WT packet with three objects:
  - `1/4/7` settle/status object with current mock role HP.
  - `1/4/11 { result=1, type=1 }`.
  - `1/4/9 { result=1 }`.
- `confirmed partial`: terminal `4/7 + 4/8` reaches `sub_4B70_battle_apply_entry` and `sub_4B70_send25_call`; the client then emits scene-side `25/5` and `2/1 moveinfo`, so the battle operate loop is broken.
  - latest evidence: `bin/logs/net_trace.log` tick `209`, `case8_apply_call pc=05189DB4`, `sub_4B70_battle_apply_entry ... lr=05189DB9`, `sub_4B70_send25_call`; `bin/logs/net_packets.log` ticks `209..210` show `25/5` then `2/1`.
- `confirmed negative`: the same partial-exit run leaves `mainBattle=2,0,0,0` at `sub_4B70_battle_apply_entry`, and later scene ticks still run under the same Battle screen context `01053f78/01053f60`.
- `confirmed negative`: appending subtype `4/11 { result=1,type=1 }` is consumed but still does not cleanly exit the Battle screen context.
  - packet evidence: `bin/logs/net_packets.log` tick `472` post-lethal `4/2` receives a three-object `1/4/7 + 1/4/8 + 1/4/11` response, responseLen `325`.
  - runtime evidence: `bin/logs/net_trace.log` tick `473` reaches `case11_result_read`, `case11_result_after_read`, and `case11_type_after_read`; the branch writes `mainObj+0x474=1` (`trace_battle_main_gate_write ... pc=05189B66`).
  - runtime negative evidence: after `4/11`, the client still emits `25/5` and `2/1 moveinfo`, but post-data-event and later scene ticks continue under active screen/current-this `01053f78/01053f60`.
- `confirmed negative`: appending subtype `4/4 { result=1 }` after subtype `4/8` is too late; the object is dispatched, but the case-4 mode gate fails before `result` is read.
  - packet evidence: `bin/logs/net_packets.log` tick `510` post-lethal `4/2` receives a three-object `1/4/7 + 1/4/8 + 1/4/4` response, responseLen `315`.
  - static correction: Battle.cbm `HandleServerBattleCmd()` off `0x7F06` is a `cmp r0,#2` mode gate, not the `result` read itself. The result helper call is at `0x7F12`, the result check is at `0x7F14`, and the success cleanup begins at `0x7F18`.
  - runtime evidence: `bin/logs/net_trace.log` tick `511` reaches `sub_7BD0_case4_result_read` under the old trace label, but the register snapshot has `r0=0` at off `0x7F06`; no `case4_result_after_read` or `case4_success_cleanup` trace follows.
  - runtime negative evidence: post-`25/5` and `2/1` processing still reports active screen/current-this `01053f78/01053f60`.
- `hypothesis pending rerun`: terminal subtype `4/4 { result=1 }` must run before subtype `4/8` changes the mode gate. The current minimal trial is therefore `1/4/7 + 1/4/4`; if case4 success reaches `sub_4B70`, subtype `4/8` may be redundant.
- `confirmed negative`: terminal `1/4/7 + 1/4/4 { result=1 }` reaches cleanup, but the user-visible semantic is escape/flee, not victory.
  - packet evidence: `bin/logs/net_packets.log` tick `282` post-lethal `4/2` receives a two-object `1/4/7 + 1/4/4` response, responseLen `262`.
  - runtime evidence: `bin/logs/net_trace.log` tick `283` reaches `case4_mode2_gate` with `r0=2`, then `case4_result_after_read` with `r0=1`, then `case4_success_cleanup`, `sub_4B70_battle_apply_entry`, and `sub_4B70_send25_call`.
  - user-visible evidence: the client reports escape success after one battle.
  - static/doc evidence: `docs/re/battle-flow.md` records high-level result `1` as victory and result `4` as escape, while Battle.cbm subtype `4/4` itself is the escape/leave branch, so it is the wrong terminal object for monster death.
- `confirmed negative`: terminal `1/4/7 + 1/4/9 { result=1 }` is parser-valid and reaches the victory-result branch, but it does not pass the branch gate by itself.
  - packet evidence: `bin/logs/net_packets.log` tick `225` post-lethal `4/2` receives `responseLen=262`, the two-object `1/4/7 + 1/4/9` packet.
  - runtime evidence: `bin/logs/net_trace.log` tick `226` dispatches subtype `4/7`, then subtype `4/9`; `trace_battle_kind4_subtype9_flow` reaches `0x05189BF0` and returns through `0x05189C02 -> 0x05189B18`.
  - gate evidence: at tick `226`, the subtype-9 trace reads `mainObj=0500b210`, `gateBase=0500b680`, `gateBytes=2,0,0,0,0`; neither `mainObj+0x470` nor `mainObj+0x474` is `1`.
- `hypothesis pending rerun`: terminal victory needs subtype `4/11 { result=1,type=1 }` before subtype `4/9 { result=1 }`.
  - runtime evidence from the prior `4/11` run: `bin/logs/net_trace.log` tick `473` reaches `case11_result_read`, `case11_result_after_read`, and `case11_type_after_read`, then writes `mainObj+0x474=1` (`trace_battle_main_gate_write ... pc=05189B66`).
  - static evidence: Battle.cbm `sub_7BD0` subtype-9 result branch checks `mainObj+0x474` or `mainObj+0x470` before the success branch at `0x05189C04`.
- `confirmed`: `1/4/7 + 1/4/11 { result=1,type=1 } + 1/4/9 { result=1 }` passes the subtype-9 victory gate.
  - packet evidence: `bin/logs/net_packets.log` tick `226` post-lethal `4/2` receives `responseLen=290`, the three-object terminal packet.
  - runtime evidence: `bin/logs/net_trace.log` tick `227` consumes subtype `4/11`, reaches `case11_*` probes, and writes `mainObj+0x474=1` at `pc=05189B66`.
  - runtime evidence: the following subtype `4/9` reaches `0x05189BF0` with `gateBytes=2,0,0,0,1`, then reaches `0x05189C04` and `0x05189EA4`.
- `confirmed next request`: after the subtype-9 gate succeeds, Battle.cbm queues a new short empty `4/12` request.
  - packet evidence: `bin/logs/net_packets.log` immediately after the terminal packet shows `send_payload len=9`, `hdr=4/12`, then the previous mock asserted on `unhandled_packet`.
  - runtime evidence: `bin/logs/net_trace.log` tick `237` logs `trace_battle_outgoing_request_source label=battle_send_operate_4_2_entry pc=05184A70 ... regs=...,00000004,0000000c`, followed by `unhandled_packet WT len=9 hdr=4/12`.
  - UI evidence: the same tick displays `按1键开启/关闭自动战斗`, explaining the visible "auto battle" text; this is a Battle.cbm state reached after victory-gate success, not input automation.
- current trial:
  - the mock now answers `4/12` with one empty same-family object `1/4/12`.
  - static evidence: Battle.cbm `sub_7BD0` dispatch compares subtype with `0x0c` at `0x05189B06..0x05189B08`; subtype `12` takes the `bhs` return path, so a `4/12` response is expected to be parser-safe no-op acknowledgement.
- `confirmed sequencing gap`: sending terminal `4/7 + 4/11 + 4/9` only on the post-lethal follow-up is too late.
  - packet evidence: latest run has three battle operate requests: tick `239` first `4/2` -> `4/6 actionnum=2`, tick `262` second `4/2` -> `4/6 actionnum=1`, tick `278` third `4/2` -> `4/7 + 4/11 + 4/9`.
  - runtime evidence: `bin/logs/net_trace.log` tick `4965` logs the second response as `battleEnds=1`, `enemyHp=0/20`, but the response contains only `4/6`; the terminal objects are delayed until tick `5816`.
  - conclusion: the battle-end contract must be attached to the lethal action response itself, not deferred until the client emits another operate request.
- current trial:
  - confirmed: lethal `4/6 + 4/7 + 4/11 + 4/9` in the same WT packet fixes the extra third-attack request, but it still leaves Battle.cbm on the auto-battle prompt.
    - packet evidence: latest session tick `193` first `4/2` -> `1/4/6`; tick `219` second/lethal `4/2` -> composite terminal response; no later third `4/2`.
    - runtime evidence: `mock_battle_operate_response response=4/6+4/7+4/11+4/9 ... enemyHp=0/20 ... battleEnds=1`, followed by subtype-9 reaching `0x05189C04` and `0x05189EA4`.
    - negative UI/runtime evidence: repeated `lcd_text ... 按1键开启/关闭自动战斗` while active screen/current-this remain `01053f78/01053f60`.
  - corrected terminal trial:
    - lethal action response is now `1/4/6 + 1/4/7 + 1/4/8 + 1/4/11 + 1/4/9`.
    - stray post-terminal `4/2` safety response is now `1/4/7 + 1/4/8 + 1/4/11 + 1/4/9`.
    - ordering rationale: subtype `4/8` has confirmed apply/cleanup evidence (`0x05189D82 -> 0x05189DB4 -> sub_4B70`) and must run while mode is still `2`; subtype `4/11` then sets `mainObj+0x474=1`, and subtype `4/9` consumes the victory result gate.
  - follow-up confirmation:
    - latest runtime evidence confirms this sequence reaches the intended apply path before victory gate:
      - `bin/logs/net_trace.log` line `2557`: `response=4/6+4/7+4/8+4/11+4/9`.
      - line `3306`: `case8_apply_call pc=05189DB4`.
      - line `3316`: `sub_4B70_battle_apply_entry ... lr=05189DB9`.
      - line `3339`: `sub_4B70_send25_call`.
      - lines `3454..3456`: subtype-9 reaches `0x05189C04` and `0x05189EA4`.
    - user-visible follow-up: the auto-battle prompt is gone, but a blank post-battle prompt appears.
    - confirmed negative: changing the first short `25/5` after `g_mockBattleOperateSessionFinished` to same-subtype `1/25/5 { result=1 }` still leaves a blank prompt. Runtime shows it still dispatches as `kind=25 subtype=5` and enters Battle.cbm `sub_17AC`'s generic branch.
      - evidence: `bin/logs/net_trace.log` line `9305` (`response=25/5 result=1 battleTerminalAck=1`), followed by `trace_battle_sub17ac_flow` at tick `5410`.
    - current correction: the first short `25/5` after `g_mockBattleOperateSessionFinished` now returns the documented high-level battle-end object `1/10/17 { result=1, lastexp=1, curexp=1 }` instead of another `kind=25` object.
    - evidence: `docs/re/battle-flow.md` records battle-end response `cmd=10, subcmd=17` with `result=1` as victory and `result=4` as escape; prior `4/7+4/4` runtime/user-visible evidence proved escape semantics are the wrong terminal for monster death.
    - confirmed negative after rerun: `1/10/17 { result=1, lastexp=1, curexp=1 }` reaches the main dispatcher but still produces a blank prompt, because it is being sent before the client has materialized the monster KO state.
      - packet evidence: `bin/logs/net_packets.log` ticks `3865..3866`; lethal `4/2` receives `1/4/6+1/4/7+1/4/8+1/4/11+1/4/9`, then the Battle-emitted short `25/5` receives `1/10/17`.
      - runtime evidence: `bin/logs/net_trace.log` tick `3867` dispatches `kind=10 subtype=17` through `trace_business_dispatch_item` / `trace_business_handler`, but `battle_event7_dispatch_entry_sub_17AC` still fires under the Battle screen context.
      - critical state evidence: immediately around subtype-8/subtype-9 apply, Battle.cbm slot dumps still report `enemy0=...,00000050,00000050` while the mock-side terminal trace reports `enemyHp=0/20`. The client-local enemy therefore started at/stayed at `80/80`, so the `20` total mock damage could not produce a local death animation.
    - corrected contract for the next run:
      - `1/4/10 battleinfo` enemy HP default is aligned with the battle-operate mock default (`20/20` instead of `80/80`).
      - lethal action replies are no longer forced to include `4/7+4/8+4/11+4/9` in the same packet; the lethal packet is now only `1/4/6` and is logged as `4/6(lethal-local-ko-pending)`.
      - terminal follow-up objects are kept only for later client follow-up windows, so the client has a chance to play the final hit / KO before victory settlement.
    - confirmed negative after the `20/20` rerun:
      - packet evidence: `bin/logs/net_packets.log` tick `163` battleinfo now encodes enemy HP/max HP as `0x14/0x14`; tick `176` first `4/2` receives `1/4/6 actionnum=2`; tick `208` lethal mock-side `4/2` receives only `1/4/6 actionnum=1`; tick `226` the client still sends a third `4/2`.
      - runtime evidence: `bin/logs/net_trace.log` around ticks `209..227` repeatedly reports `enemy0=...,00000014,00000014`; the client-local fighter table still does not decrement after the `type=2` action record.
      - importer evidence: the lethal record materializes as `record=2,2,1 valueA=8 valueB=0` and selects the blood effect template, but local state remains `valueAB=0,20`, so current `type=2` is effect-only for HP purposes.
    - next actioninfo experiment:
      - damage action records first tried `type=1` while preserving the stable slot/subcount shape (`actor=2,target=0,subCount=1`) and including the damage number as the type-1 blob/string.
      - `CBE_BATTLE_ACTION_TYPE=2` can restore the previous parser-safe type-2 behavior if the type-1 rerun regresses.
      - confirmed negative after rerun:
        - `record=1,2,1` materializes successfully, so the richer type-1 parser branch is reachable.
        - runtime then enters Battle.cbm `sub_6B08` local action type `1`, calls `sub_4404(activeSlot,3,6,1,7)` at `0x05186C74`, and reaches the type-6 stage callback call at `0x05186C98`.
        - `bin/logs/stdout_trace.log` ends with `pc=0`, `lr=5186c9b`, `lastPc=5186c98`; the selected stage block callback was zero.
        - critical runtime state: `trace_battle_local_action_state label=state4_store_target ... localState=255,0,1,0,0,0,2 ... valueAB=0,20`; the local action record's slot byte is still `2` in a `counts=1,1` battle.
      - corrected current experiment:
        - `type=1` first-record slot byte now defaults to `1` via `CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT=1`.
        - paired counter records are disabled for type-1 by default (`CBE_BATTLE_TYPE1_BUNDLE_COUNTER=0`) so the next rerun isolates the first hit before reintroducing monster counterattack.
        - `CBE_BATTLE_FIRST_ACTION_TYPE` / `CBE_BATTLE_COUNTER_ACTION_TYPE` can split later mixed record experiments without changing code.
      - success evidence to look for: `record=1,1,1` or another in-range slot shape, no `0x05186C98 -> pc=0`, a nonzero local damage/popup path, fighter HP movement away from `enemy0 ... 0x14/0x14`, and later a visible monster KO before settlement.
      - confirmed negative after the safe-slot/valueB rerun:
        - `bin/logs/net_trace.log` line `13820` confirms the first type-1 payload carries `valueA=12,valueB=8`; line `14088` confirms Battle.cbm reads that `valueB=8`; lines `14146/14160` materialize `record=1,1,1 valueA=12 valueB=8`.
        - `bin/logs/net_trace.log` line `14938` confirms the lethal payload carries `valueA=8,valueB=0`; lines `15264/15278` materialize `record=1,1,1 valueA=8 valueB=0`.
        - local state still remains HP-inert: lines `15879..15885` keep `valueAB=0,20`, line `16166` keeps `enemy0=...,00000014,00000014`, and `bin/logs/net_packets.log` line `239` shows a third post-lethal `4/2`.
      - conclusion:
        - `valueB` is parsed correctly but is not sufficient to decrement the enemy fighter HP or trigger KO.
        - the safe actor wire slot `1` avoids the previous `record=1,2,1` crash, but the record still targets the wrong local action/fighter path.
      - current target-mapping trial:
        - keep `CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT=1`.
        - add `CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT`, default `1`, so the type-1 child target can be tested independently from the actor slot.
        - battle operate trace now logs the actual post-override `wireMapped=<actor>_to_<target>` values.
      - confirmed negative after the `1_to_1` target rerun:
        - `bin/logs/net_trace.log` line `20996`: first `4/6` logs `wireMapped=1_to_1`, `valueA=12,valueB=8`.
        - line `21236`: Battle.cbm reads the type-1 child target byte as `r0=00000001`.
        - line `21323`: materialized record dump confirms child target `1`, child flag `1`, `valueA=12`, `valueB=8`.
        - line `22235`: lethal `4/6` also logs `wireMapped=1_to_1`, `valueA=8,valueB=0`.
        - line `22562`: lethal record dump confirms target `1`, flag `1`, `valueA=8`, `valueB=0`.
        - local HP remains unchanged: lines `21339`, `21395`, `22578`, and `22626` keep `valueAB=0,20 targetId=1`; `bin/logs/net_packets.log` line `351` still shows the third post-lethal `4/2`.
      - confirmed negative after the `1_to_2` target rerun:
        - packet evidence: `bin/logs/net_packets.log` latest session tick `182` enters battle via `4/1 id=105`, tick `194` sends the first `4/2`, and no later battle packet is handled before crash.
        - runtime evidence: `bin/logs/net_trace.log` line `27337` logs `wireMapped=1_to_2`; line `27577` confirms Battle.cbm reads the type-1 child target as `r0=00000002`.
        - crash evidence: `bin/logs/stdout_trace.log` ends with `pc=0`, `lr=518789f`, `lastPc=518789c`.
        - static evidence: Battle.cbm `0x05187896..0x0518789C` loads `[slot+0x2c]` into `r1` and calls `blx r1`; the latest run reached this with a zero callback.
        - conclusion: `1_to_2` is parser-valid but stage-unsafe in the current slot/template state.
      - current correction:
        - `CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT` was first restored to `1` after the `1_to_2` crash, then changed to default `0` after the next rerun showed visible self-damage with `wireMapped=1_to_1`.
        - evidence for the self-damage correction: latest `bin/logs/net_trace.log` shows no `groupinfo` seed, compact table still `enemyIds=10001,0,0,0`, `mock_battle_operate_response ... wireMapped=1_to_1`, and Battle-local action state keeping `targetId=1`.
        - this is only a mock-side target-selection mitigation while the enemy template table source is still missing; `CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT=1` remains available to reproduce the self-hit baseline, and `=2` remains the confirmed stage-unsafe crash reproducer.
        - `1/4/10 battleinfo` defaults back to the current Battle.cbm enemy-template table id when the live request id is absent from that table.
        - forcing the request id is available only through `CBE_BATTLE_FORCE_REQUEST_ENEMY_ID=1`.
      - confirmed negative after forcing `enemyWireId=105`:
        - runtime evidence: `mock_challenge_interaction_response ... requestEnemyId=105 enemyWireId=105`, followed by `sub_66CC_enemy_lookup_callsite` with `r0=00000069`.
        - lookup failure evidence: `sub_66CC_enemy_lookup_failed_path pc=05188A46`, with table `0500c718` still containing `enemyIds=10001,0,0,0`.
        - crash evidence: `stdout_trace.log` ends with `pc=0`, `lr=51876d5`, `lastPc=51876d2`.
        - static evidence: Battle.cbm `0x051876CE..0x051876D2` loads a fighter render callback from `[slot+0x584+4]` and calls `blx r3`; the lookup-failed enemy slot leaves that callback zero.
        - conclusion: the real touched monster cannot be introduced by changing only `battleinfo.enemyId`; the upstream template table population contract is still missing.
  - non-lethal turns stay on the existing `1/4/6` action packet.

Latest live request shape after entering `Battle.cbm`:

- request object: `1/4/2`
- fields:
  - `index=0`
  - `Operate=0`

Evidence:

- runtime packet log: `bin/logs/net_packets.log` latest battle ticks `190`, `226`, `252`, `271`
- runtime trace: `trace_outgoing_wt_send_context first=4/2 ... index=0 operate=0`

Latest response sequencing correction:

- previous mock gating bundled `1/4/6 { actionnum=2 }` only on the first armed request of the battle session
- latest packet evidence proves later player attacks are still the same live `4/2 index=0 operate=0` request family
- when those later requests regressed to `actionnum=1`, user-visible behavior regressed to “first attack has counterattack, second attack has no counterattack”

Therefore current mock policy is:

- while battle-operate session is armed and incoming `4/2` has `Operate=0`, return parser-safe subtype `1/4/6`
- set `actionnum=2`
- encode two tagged-seq `actioninfo` records
- record 0 stays on the currently confirmed no-crash player-hit baseline
- record 1 reuses the currently confirmed no-crash bundled follow-up shape

Current session-state extension:

- mock now keeps per-battle HP state for both sides
- initial values are seeded on `1/4/10` battle start:
  - player HP defaults to `120`
  - enemy HP defaults to `20`
- on each `4/2 Operate=0`:
  - record 0 damage is `min(12, enemyHpCurrent)`
  - enemy HP is decremented immediately on the mock side
  - record 1 is appended only if the enemy survives and player HP is still positive
  - counter damage is `min(10, roleHpCurrent)`
  - when either side reaches `0`, the mock disarms the battle-operate session after that reply
  - later stray `4/2` requests after lethal no longer re-seed HP from defaults; they stay in a terminal-followup path with `actionnum=0`
  - latest terminal `type=1` experiment is now rejected:
    - static `Battle.cbm` `0x05188ED4..0x05188F4C` does show a richer `type=1` branch than `0x05188F4E..0x05189010` `type=2`
    - however the minimal terminal `type=1` packet trial was not parser-safe on the live client path
    - runtime evidence:
      - `bin/logs/net_packets.log` lethal reply at tick `775` used `responseLen=84`
      - `bin/logs/net_trace.log` still reached `sub_actioninfo_parser_type2_branch`, but materialized local record bytes changed to `record=1,1,2,0`
      - the battle state then fell through `0x05186C74 -> 0x05186C98`
      - `bin/logs/stdout_trace.log` crash: `lastPc=5186c98`, `lr=5186c9b`, `pc=0`
    - conclusion:
      - `confirmed negative`: the current minimal terminal `type=1` encoding is wrong and must not be used as the default lethal packet shape
      - current default has been reverted to the prior stable all-`type=2` baseline

Status:

- `confirmed`: current request family and two-record parser-safe reply shape
- `hypothesis`: client-local KO / battle-end transition should now be recoverable from lethal `4/6 actioninfo` rounds without patching client globals, but latest manual state still reports that battle does not exit automatically after lethal rounds

Evidence:

- Battle loop over `actionnum`: `Battle.cbm` `0x05189018..0x05189022`
- first-round two-record success: `bin/logs/net_packets.log` tick `190` responseLen `129`, `actionnum=2`
- regression cause: `bin/logs/net_packets.log` ticks `226/252/271` responseLen `84`, `actionnum=1`
- current HP-state implementation site: `src/mock-server.c` `vm_net_mock_build_challenge_interaction_response()` and `vm_net_mock_build_battle_operate_response()`
- latest lethal-round evidence:
  - `bin/logs/net_trace.log` `tick=334`: `mock_battle_operate_response ... damageValue=8 ... enemyHp=0/20 ... battleEnds=1`
  - older sessions still showed repeated post-lethal `4/2` afterwards (`bin/logs/net_packets.log` ticks `333`, `350`, `364`)
  - prior bug: those post-lethal requests re-seeded enemy HP because the mock only checked `enemyHpCurrent == 0`; this is now corrected with a dedicated finished-session flag
  - newest stable lethal session no longer shows post-lethal `4/2`; the stall is now inside the local Battle state after the lethal `4/6` is consumed
    - evidence:
      - `bin/logs/net_packets.log` newest battle session ticks `259`, `282`
      - first operate reply: `responseLen=129`
      - lethal reply: `responseLen=84`
      - no later `4/2` request appears in that newest session tail

Subtype-6 static prelude and terminal-followup narrowing:

- `confirmed`: Battle subtype `4/6` handler `HandleBattleActionMsg()` (`Battle.cbm` `0x05188DD0`, offset `0x6EB0`) always does three field reads in this order:
  - `InitActionSlot_A()` / `0x05188C32` reads field `iteminfo`
  - `InitActionSlot_B()` / `0x05188CDC` reads field `teaminfo`
  - only then does the main handler read `actioninfo` and `actionnum`
- `confirmed`: `teaminfo` is not a generic opaque blob.
  - static `InitActionSlot_B()` shows each record reads three scalar values and, when the first scalar matches a live fighter slot id at `R9+0x2918 + slot*0xC4 + 0x524`, stores the third scalar into `slot + 0x540`
  - this makes `teaminfo` a plausible slot-link / relation table, not damage text
- `confirmed`: `actionnum=0` in subtype `4/6` is a parser-valid early return only.
  - static `0x05188EFC` stores `actionnum` into the loop bound
  - static `0x051890FA..0x05189104` compares loop index against that bound and returns directly when the bound is zero
  - there is no extra terminal-side branch in this handler for the zero-action case itself
- current implication:
  - `hypothesis`: the post-lethal mock `4/6 { actionnum=0, actioninfo="" }` is too weak because it only exercises the parser-safe empty-return path
  - next evidence target is whether terminal follow-up also needs non-empty `teaminfo`, `iteminfo`, or a different business object after the lethal `4/6`

Implementation correction for the next run:

- `confirmed`: the previous mock response body omitted `iteminfo` and `teaminfo` entirely, even though the static subtype-6 prelude looks up both names before `actioninfo/actionnum`.
- changed:
  - `src/main.c` now emits explicit typed-empty fields `iteminfo=<tagged u8 0>` and `teaminfo=<tagged u8 0>` in every `1/4/6` battle-operate response before `actionnum/actioninfo`.
  - this applies to both the strict `4/2 index/Operate` builder and the relaxed fallback builder.
- status:
  - `confirmed`: this is only a server packet-shape correction; it does not patch Battle.cbm code, write battle globals, or change the current tagged `actioninfo` record grammar.
  - `hypothesis`: explicit empty companion fields may be sufficient to let the terminal follow-up take the same parser path as real packets; if not, the next run should use the existing `sub_teaminfo_wrapper_*` and `sub_actioninfo_parser_loop_*` probes to recover the first non-empty `teaminfo` / `iteminfo` record grammar.

Follow-up after the first companion-field rerun:

- `confirmed runtime evidence`:
  - the new packet shape was delivered and remained non-crashing:
    - `bin/logs/net_packets.log` tick `222`: `responseLen=151`, payload contains field names `iteminfo`, `teaminfo`, `actionnum`, `actioninfo`
    - tick `244`: `responseLen=106`
    - tick `262`: terminal follow-up `responseLen=61`
  - `sub_teaminfo_wrapper_*` probes now fire; previous matching count was zero.
  - terminal follow-up still exits through the zero-action loop:
    - `bin/logs/net_trace.log` tick `263`: `trace_battle_actioninfo_loop_detail ... actionCount=0`, then `return pc=05189024`
- `confirmed negative`:
  - zero-length raw `iteminfo/teaminfo` fields are not a real empty stream. The teaminfo parser sees `teamCount=1` and reads later packet/object bytes as record data, for example `member_id_read r0=74696f6e` (`"tion"`, from the trailing `actioninfo` name/string region).
- corrected mock:
  - `src/main.c` now encodes both companion fields as a tagged sequence containing one `u8` value `0`, matching the typed stream reader family used by `actioninfo`.
- status:
  - `hypothesis pending rerun`: tagged-empty `iteminfo/teaminfo` should make `sub_teaminfo_wrapper_count_ready/count_read` report a zero record count and avoid consuming later object metadata.

Follow-up after the tagged-empty companion-field rerun:

- `confirmed runtime evidence`:
  - the tagged-empty packet shape was delivered and remained non-crashing:
    - `bin/logs/net_packets.log` latest battle session tick `213`: `responseLen=157`, `iteminfo` and `teaminfo` each carry value bytes `00 01 00`, followed by `actionnum=2`
    - tick `238`: lethal response `responseLen=112`, `actionnum=1`
    - tick `267`: post-lethal follow-up `responseLen=67`, `actionnum=0`
  - `bin/logs/net_trace.log` latest run still shows `InitActionSlot_B()` entering one bogus team record:
    - tick `268`: `trace_battle_teaminfo_wrapper_detail ... teamCount=1`
    - tick `268`: `member_id_read r0=00016163`
    - tick `268`: `link_value_read r0=6e756d00`, which is the neighboring `actionnum` name bytes, not a legal slot-link value
  - terminal follow-up still exits through the zero-action loop:
    - tick `268`: `trace_battle_actioninfo_field_result ... actionnum_result r0=00000000`
    - tick `268`: `trace_battle_actioninfo_loop_detail ... actionCount=0`, then `return pc=05189024`
- `confirmed negative`:
  - raw tagged-empty `iteminfo/teaminfo` is also not a real empty value for the `InitActionSlot_A/B` wrappers.
  - `actioninfo` remains confirmed as a raw tagged stream, but the companion fields do not share that exact outer field encoding.
- corrected mock for the next run:
  - `src/main.c` now keeps `iteminfo` and `teaminfo` present, but encodes each empty companion value with the normal WT blob helper (`valueLen=2`, inner blob length `0`) instead of a raw tagged stream.
- status:
  - `hypothesis pending rerun`: empty WT blob fields may give `InitActionSlot_A/B` a true empty descriptor without leaking the following `actionnum/actioninfo` object metadata into the teaminfo reader.

Follow-up after the empty-blob companion-field rerun:

- `confirmed runtime evidence`:
  - the empty-blob packet shape was delivered:
    - `bin/logs/net_packets.log` latest session tick `215`: `responseLen=155`, `iteminfo/teaminfo` each encode `valueLen=2` with inner blob length `0`
    - tick `248`: lethal response `responseLen=110`
    - tick `265`: post-lethal follow-up `responseLen=65`
  - it still is not a legal empty value for the wrappers:
    - `bin/logs/net_trace.log` tick `266`: `trace_battle_teaminfo_wrapper_detail ... teamCount=1`
    - tick `266`: `member_id_read r0=03016163`
    - tick `266`: `link_value_read r0=6e756d00`, again consuming the neighboring `actionnum` field name
  - terminal follow-up still exits through the zero-action loop:
    - tick `266`: `actionnum_result r0=00000000`
    - tick `266`: `return pc=05189024`
- `confirmed static evidence`:
  - `InitActionSlot_B()` / off `0x6DBC` does not take its loop bound from the `teaminfo` field payload.
  - after `teaminfo` is found, it calls the Battle current-team/count provider via `R9+0x2050`, then reads that many records from the field with `stream_read_i32_be_tagged` (`0x01033A5D` / impl `0x01033A62`).
  - each record reads three scalar values; the first is compared with each live slot id at `R9+0x2918 + slot*0xC4 + 0x524`, and the third is stored to `slot+0x540` on match.
- corrected mock for the next run:
  - `src/main.c` now omits `iteminfo/teaminfo` again by default instead of sending any empty placeholder.
  - this restores the parser-safe action path while the real non-empty `teaminfo` record grammar is recovered.
  - trace-only instrumentation now also dumps the first four slot ids and slot links (`+0x524/+0x540`) in `sub_teaminfo_wrapper_*` logs.
- status:
  - `confirmed negative`: raw empty, raw tagged-empty, and empty-blob companion fields are all wrong empty encodings.
  - `hypothesis`: a present `teaminfo` field is only valid when it contains the current-team record count's worth of records; the next evidence target is the exact overlapping tagged-i32 record layout.

Broad trace pass before the next packet-shape change:

- `confirmed static evidence`:
  - `HandleServerBattleCmd()` / Battle.cbm off `0x7BD0` is the active kind-4 server dispatch for battle.
  - subtype cases now in scope:
    - subtype `5`/`10`: battle-start path, calls `HandleBattleStartMsg()` at off `0x66CC` from the branch ending around `0x7DF0`
    - subtype `6`: action path, calls `HandleBattleActionMsg()` at off `0x6EB0`, then sets battle phase at off `0x7F84`
    - subtype `7`: battle-settle path, calls `HandleBattleSettleMsg()` at off `0x743C`
    - subtype `8`: autorevive/info/fbs/fdata settle-adjacent path
    - subtype `1`/`9`, subtype `4`, and subtype `11`: result-checked state/cleanup branches that may be relevant after terminal rounds
- `confirmed static evidence` from main CBE WT parser:
  - `ParseDMenuResponse()` / `0x01033B9B` builds an object with field count at object `+0x10`, capacity at `+0x14`, and a field table pointer at `+0x18`
  - field table entries are 12 bytes: `namePtr`, `valuePtr`, and a third metadata word
  - scalar field helpers read from the same `valuePtr` convention:
    - `LookupItemIntField()` / `0x01033CF3` reads BE u32 after the two-byte field length
    - `LookupItemByteField()` / `0x01033C6D` reads one byte after the two-byte field length
    - `LookupItemIdField()` / `0x01034163` reads the first two bytes as an id-like value
- changed trace-only instrumentation:
  - `src/main.c` now emits `trace_wt_field_table` and `trace_wt_field_entry` snapshots for battle server packets before key parser calls.
  - `trace_battle_server_cmd` snapshots the kind-4 dispatch packet, current Battle state bytes, team counts, phase word, and mock-side HP state at `0x7BD0` and key subtype case offsets.
  - status-7/settle traces now also dump the parsed WT field table.
  - this is observation-only instrumentation: it does not alter guest registers, return values, packet parsing, Battle.cbm logic, or battle globals.
- next contract target:
  - before trying another terminal packet shape, use the next manual run to decide whether post-lethal needs a non-empty `teaminfo/iteminfo` record, a subtype `7` settle object, a subtype `8` info/autorevive object, or a result branch such as subtype `4`/`11`.

Follow-up after the broad trace rerun:

- `confirmed runtime evidence`:
  - the compiled default is back to the parser-safe subtype-6 action packet shape with no `iteminfo/teaminfo` fields:
    - `bin/logs/net_packets.log` latest session tick `167`: `responseLen=129`, `1/4/6` with only `actionnum=2` and `actioninfo`
    - tick `192`: lethal reply `responseLen=84`, `actionnum=1`
    - tick `206`: post-lethal follow-up `responseLen=39`, `actionnum=0`
  - broad dispatch traces show no hidden subtype-7/subtype-8/result branch after lethal. The only server response consumed after the final `4/2` is still subtype `6`:
    - `bin/logs/net_trace.log` tick `207`: `trace_battle_server_cmd ... kind=4 subtype=6 ... mockHp=110/120,0/20`
    - the same tick reaches `case6_action_entry`, `case6_before_action_parser`, and `case6_after_action_phase_set`
    - no `case7_settle_result_check`, `before_status7_parser`, or `case8_*` trace appears in that terminal window
  - the post-lethal subtype-6 parser does exactly the known empty-loop return:
    - tick `207`: `trace_battle_actioninfo_field_result ... actionnum_result r0=00000000`
    - tick `207`: `trace_battle_actioninfo_loop_detail ... actionCount=0`, then `return pc=05189024`
- `confirmed trace correction`:
  - `trace_wt_field_entry.taggedLen` is now decoded as BE16; the previous `taggedLen=256` display for `00 01` fields was a trace decoding bug, not a client/parser fact.
- changed mock contract for the next run:
  - only when `terminalFollowup=1`, the battle-operate response now sends a single `1/4/7` settle/status object instead of the semantically weak `1/4/6 actionnum=0`.
  - this is deliberately not the old unsafe same-window battle-start `4/7`, and not a normal mid-fight `4/7 + 4/8` composite.
  - rationale:
    - static `HandleServerBattleCmd()` subtype-7 branch enters `HandleBattleSettleMsg()` only when the current mode value is `2`
    - the latest post-lethal dispatch window still has that mode value while enemy HP is `0`
    - therefore post-lethal `4/2` is the first currently observed server boundary where a settle object can be tested without appending it to battle start or ordinary attack playback
- status:
  - `hypothesis pending rerun`: terminal-only `1/4/7` may be the missing battle-end contract. If it crashes or returns through the subtype-7 guard, use the new status7 field-table and item/combat probes to recover the exact settle object grammar.

Evidence:

- static IDA:
  - `HandleBattleActionMsg()` at `0x05188DD0` / `0x6EB0`
  - `InitActionSlot_A()` at `0x05188C32` / `0x6D12`
  - `InitActionSlot_B()` at `0x05188CDC` / `0x6DBC`
  - zero-action loop exit at `0x051890FA..0x05189104`
- runtime:
  - latest packet/log pair still shows lethal follow-up as `1/4/6 { actionnum=0, actioninfo="" }`
  - `bin/logs/net_packets.log` newest battle session ticks `258`, `293`, `314`
  - `bin/logs/net_trace.log` `mock_battle_operate_response ... terminalFollowup=1 ... actionnum=0 ... tick=314`

## Startup Update Protocol

### Version Check Response

Trigger request contains `version`.

Status: `confirmed`

Correct response shape contains two objects:

- subtype `5`
  - field: `result`
- subtype `9`
  - fields: `type`, `id`, `code`

Why this is required:

- `handle_version_update_response` consumes subtype `5` field `result`
- `startup_update_net_callback` consumes subtype `9` and enters `startup_handle_update_metadata`
- subtype `5` alone can drive `update_state=2`, but the startup screen does not remove itself and the UI stalls
- `type=0` is the confirmed "no update / local data already usable" path and the CBE removes the startup screen on its own

When local `JHOnlineData/MMORPGTempcbm` and `JHOnlineData/mmorpg_updateversioncbm` already exist, the built-in version response currently returns:

- `result = 0`
- an additional close event `9` after the data event

Do not change that close event to `8`; the current client net wrapper does not route event `8` into the active object callback.

### Update Chunk Response

Trigger request contains `start` and `id`.

Status: `confirmed`

Response fields:

- `totalsize`
- `crc`
- `type`
- `name`
- `data`

Rules:

- `start` must be echoed from the request field
- `type` should be echoed from the request field when one is present
- `name` should be echoed from the request field when one is present; hardcoding `MMORPGTempcbm` is only correct for the generic startup/update package path
- `crc` is the signed-byte running sum up to the end of the current chunk
- chunk size is currently limited to `0x1000`

Current payload source priority:

- exact local `JHOnlineData/<request.name>` when the request carries a non-empty resource name
- mock resource wrappers for the known synthetic names `c_mock_missing_motion.actor`, `mock_missing_motion.actor`, and `mock_actor_image.gif`
- `JHOnlineData/MMORPGTempcbm.mock`
- `JHOnlineData/mmBattleMstarWqvga.cbm`
- `JHOnlineData/mmGameMstarWqvga.cbm`
- `JHOnlineData/mmTitleMstarWqvga.cbm`

Do not reuse the already-installed `JHOnlineData/MMORPGTempcbm` as the download source, or the true install/update path will be masked.

Additional confirmed post-scene branch:

- after the first-scene HUD divide-by-zero is bypassed, the client can enter a later `18/7` resource-update loop whose request key is not `MMORPGTempcbm`
- the latest clean logs show repeated `trace_update_request_prepare ... name=侠剑江湖 type=1 start=0/4096/...` while the UI displays `"正在更新资源文件, 请稍候!"`
- when the mock incorrectly replies with `name=MMORPGTempcbm`, storage logs show the temp file being renamed to `JHOnlineData/MMORPGTempcbm` and the client then immediately failing another local open for `JHOnlineData/侠剑江湖`
- therefore this branch requires preserving the requested local resource key in the `18/7` response, even if the payload bytes are still coming from a fallback mock source
- the next rerun confirms that first wire-level fix: response `1/18/7` now echoes the request's non-ASCII `name` bytes and `type=1`
- however, the same run also proves a second contract boundary exists below the protocol layer: after accepting those echoed bytes, the client forwards the same resource key into local file rename/open calls, so the emulator's file shim must decode guest path bytes consistently with the network payload encoding or the update still stalls on a host-side garbled filename
- the latest rerun confirms that the path bridge is now good enough for the first reopen: storage trace shows the temp file rename succeeds, the immediate reopen succeeds, and the very first read returns `hex=fefefefe`
- static IDA for `sub_100D338()` (`0x0100D338`) explains the remaining mismatch: this local resource-open path expects the downloaded file to begin with a 4-byte little-endian payload length before the real body
- therefore, when `18/7(type=1,name=<non-empty>)` falls back to a generic payload instead of an exact local `JHOnlineData/<name>` file, the fallback must be wrapped as `u32le payloadLen + payload`; sending raw CBM bytes makes the reopen interpret `fefefefe` as the length field and remain stuck in the update flow
- the next rerun exposes one more cache-layer consequence of that same bad download: once `JHOnlineData/侠剑江湖` already exists on disk with the old `fefefefe` header, the client may reopen that stale local file immediately on the next scene-enter path before any fresh `18/7` request is sent
- the emulator therefore needs a paired local-cache guard in addition to the network wrapper: extensionless non-ASCII `JHOnlineData/<name>` files opened read-only must be rejected when their first `u32le` header is not a plausible `payloadLen <= fileSize-4`, and the mock server must likewise ignore the same bad file as an exact payload source for later named `18/7` replies

### Verified Startup Chain

Status: `confirmed`

Startup/update path:

1. startup screen state advances to `7`
2. net open event `5` triggers `send_version_update_request`
3. mock returns subtype `5 + 9` version response
4. `handle_version_update_response` sets update flags and `update_state=2`
5. `startup_handle_update_metadata` parses `type/id/code`
6. the CBE removes the startup screen on its own
7. the emulator resumes the lower screen
8. the client loads `JHOnlineData/mmTitleMstarWqvga.cbm` and enters the dynamic CBM screen

## Post-Scene `0x1B` Chain

### `0x1B/12` field `name`

Status: `confirmed`

`kind=0x1B subtype=12` is handled by `net_handle_fb_target_dispatch()` at `0x01010BC0`.

Confirmed flow:

- subtype `12` reads field `result`
- only when `result == 1`, it calls `ui_apply_named_posinfo_target_with_fb()`
- that wrapper stores field `fb` into active UI state and then forwards into `ui_apply_named_posinfo_target()`
- `ui_apply_named_posinfo_target()` reads server fields `name` and optional `posinfo`, copies `name` into active UI object state, and passes `name + posinfo` into UI manager method `+116`

This establishes that `0x1B/12.name` is a named-target / display-location semantic, not a resource filename semantic.

Additional confirmed request/response pairing:

- the current synthetic `0x1B/12` is only appended inside `mock_scene_change_combo_response()` when the incoming post-enter composite WT request contains object `1/0x1B/11`
- the triggering request is the observed `len=86` packet sent immediately after `trace_scene_send_map_enter_request`, with object sequence:
  - `1/0x0C/1`
  - `1/7/42`
  - `1/2/3(maptype,mapID,exitID)`
  - `1/0x1B/11`
  - `1/0x0C/1`
  - `1/7/42`
- this means the live `0x1B/12` should be treated as the server-side answer to the embedded `0x1B/11` subrequest inside the scene-enter follow-up packet, not as a direct answer to the `scene+posinfo` object itself

Additional confirmed buffer alias:

- `scene_send_map_enter_request()` (`0x0100F9B4`) serializes outgoing request field `mapID` from `*(_DWORD *)(R9+21676) + 1141`
- `ui_apply_named_posinfo_target()` (`0x0100E9B8`) writes incoming server field `name` from `0x1B/12` into that same `*(_DWORD *)(R9+21676) + 1141` buffer

This strongly suggests the correct `0x1B/12.name` is some map/scene/location text compatible with later `currentMapIdText` / `mapID` flow, not an actor or resource filename.

Additional confirmed producer / consumer chain for `uiState + 1141`:

- explicit writers:
  - `ui_apply_named_posinfo_target()` (`0x0100E9B8`) clears `uiState+1141` and copies incoming server field `name`
  - `scene_rebuild_runtime_nodes()` (`0x0100F7A6`) also clears `uiState+1141` and copies its `currentMapIdText` argument there, but only when its first argument `varg_r0_1` is nonzero
  - `net_handle_actor_move_info()` (`0x01012D9A`) case `12` also clears `uiState+1141` and copies incoming business field `name` there
- explicit readers / reusers:
  - `scene_send_map_enter_request()` (`0x0100F9B4`) reads `uiState+1141` as outgoing WT field `mapID`
  - `scene_rebuild_runtime_nodes()` reuses the same `currentMapIdText` string as the stacked extra argument into `sub_10352AE()`, which is the confirmed upstream source of the later `parse_actor_motion_descriptor()` misroute

Scene-enter branch behavior is now narrower:

- fresh scene-enter path inside `scene_runtime_init_and_sync()` (`0x010132A8 -> 0x01013586`) prepares a temporary text buffer at `R9+0x5E46`, validates it via `sub_100EEBC()` / `sub_100FA40()`, and then calls `scene_rebuild_runtime_nodes(varg_r0_1=1, ..., currentMapIdText=R9+0x5E46)`
- pending scene-switch / restore path inside the same function (`0x010131C4 -> 0x010131FE`) does **not** overwrite `uiState+1141`; instead it validates the existing `uiState+1141` contents with `sub_100FA40(uiState+1141)` and then calls `scene_rebuild_runtime_nodes(varg_r0_1=0, ...)`, which skips the internal copy into `uiState+1141`

This means the buffer is expected to persist as a scene/map text slot across restore/re-enter transitions. `0x1B/12.name` can therefore corrupt later map-enter and scene-rebuild behavior specifically because it overwrites a buffer that the restore path expects to remain a valid current-map/current-location text value.

Additional validator evidence for the fresh-enter text slot:

- `sub_100EEBC()` (`0x0100EEBC`), used by both the fresh-enter temporary buffer and the restore-path `uiState+1141` buffer, returns success when either:
  - a manager compare callback at `R9+0x4DA0 -> +0x48` accepts the input, or
  - the first byte of the input string is `'c'` (`0x63`)
- `sub_100FA40()` is the wrapper used at both callsites in `scene_runtime_init_and_sync()`
- runtime evidence already shows outgoing `mapID` strings such as `c00PenglaiXiandao_01`

This makes “internal scene/map identifier string” a stronger fit for the shared `+1141` / `R9+0x5E46` text slots than a natural-language map display name. It does not yet prove that every valid value must be `c...`, because the compare callback may also accept other forms, but the observed post-login path is now highly consistent with a c-prefixed scene id.

Related confirmed fallback behavior:

- `sub_100EEE0()` does **not** write `uiState+1141` itself
- instead, it first validates the current `uiState+1141` contents with `sub_100EEBC(uiState+1141)`
- only when that validation fails, it copies its caller-supplied `displayNamePtr` into a separate 4-entry fallback table at `R9+0x5CE4`
- current callers of `sub_100EEE0()` include:
  - `sub_100F094()` actor/scene-node parsing
  - `sub_10159DA()`
  - `scene_parse_npcinfo_and_spawn_npcs()` (`0x01037A9C`)

This reinforces that actor/NPC display names are treated as a fallback/auxiliary label source when the main current-scene text slot is invalid, not as the primary semantic of `uiState+1141`.

Supporting static evidence:

- `ui_apply_named_posinfo_target_with_fb()` at `0x0100EC66`
- `ui_apply_named_posinfo_target()` at `0x0100E9B8`
- the same helper is also reused by `net_handle_guild_hall_enter_response()` at `0x0103C224`, which further supports the interpretation that this is a generic named-target/location helper rather than a file-download API

### Later `type=6` request field `name`

Status: `confirmed`

The repeated `type=6` WT request is built by `send_update_chunk_request()` at `0x01036C66`, not by the startup version-check path.

Confirmed serialized fields there:

- `name`
- `screen`
- `type`
- `start`
- `version`

The startup version-check path is separate:

- `send_version_update_request()` at `0x0103B2D6`
- `handle_version_update_response()` at `0x0103751C`

On current good startup runs, `mock_update_installed yes` proves startup version update is already skipped, so later `type=6` traffic belongs to a different resource/update path.

### Resource-miss to `type=6` bridge

Status: `confirmed`

Local resource-open failure is bridged into the later `type=6` request path through the resource manager:

- `sub_1044DA0()` calls `sub_100D2BE()` to open a local resource by name
- on failure, it records the missing resource name into a net-manager pending-entry table
- `sub_1044EF6()` marks a pending callback slot for that same missing resource
- later `send_update_chunk_request()` consumes pending entries through `sub_1036C2E()` and serializes `name/screen/type/start/version`

This establishes that `type=6.name` is a local resource filename / pending-download key, not the original business meaning of `0x1B/12.name`.

Supporting static evidence:

- resource open helper: `sub_100D2BE()` at `0x0100D2BE`
- local resource stream wrapper: `load_resource_stream_by_name()` at `0x0100D498`
- resource pending-table writer: `sub_1044DA0()` at `0x01044DA0`
- pending callback marker: `sub_1044EF6()` at `0x01044EF6`
- request builder: `send_update_chunk_request()` at `0x01036C66`
- pending-entry copy helper: `sub_1036C2E()` at `0x01036C2E`

Additional confirmed callsite narrowing:

- `sub_100D2BE()` currently has only four code xrefs in `江湖OL.CBE`:
  - `sub_100D338()` at `0x0100D348`
  - `load_resource_stream_by_name()` at `0x0100D498`
  - `sub_1043206()` at `0x01043216`
  - `sub_1044DA0()` at `0x01044DB4`
- the two post-login/resource-update candidates in scope are `sub_1043206()` and `sub_1044DA0()`
- inside `send_update_chunk_request()` case `n2==4`, the serialized download key comes from the current pending entry at `*(R9+38284) + 2`, while `type` comes from the first `u16` of that same entry, `start` comes from offset `+52`, and `version` comes from `R9+38328`
- `sub_1036C2E()` flips the pending-entry state byte from `1` to `2` when it selects the matching entry for serialization

Latest confirmed runtime narrowing:

- the first observed reuse of the `0x1B/12`-derived UI name `MMORPGTempcbm` is not through `sub_1044DA0()/sub_1044EF6()` and not through `type=6`
- instead, after `kind=27 subtype=12` is consumed, the live path is:
  - `parse_actor_motion_descriptor()` callsite `0x0100D6FC`
  - `sub_100D534()`
  - `load_resource_stream_by_name()` at `0x0100D48A`
  - `sub_100D2BE()`
- in the newest trace, those four steps all carry the same name as the active UI cached name:
  - `name=MMORPGTempcbm`
  - `uiName=MMORPGTempcbm`
- other currently traced callers into `sub_100D534()` (`sub_100D564()` at `0x0100D570` and `sub_100DB82()` at `0x0100DB9C`) do not carry that same `MMORPGTempcbm` string on the observed path; they continue to load normal actor/UI resource names such as `h_warrior.actor`, `ui_h_war.actor`, and map/decoration actor resources

Latest confirmed upstream caller:

- the first confirmed upstream caller that feeds the misrouted name into `sub_10352AE()` is `scene_rebuild_runtime_nodes()` at `0x0100F8F6`
- runtime traces show this exact chain after `kind=27 subtype=12`:
  - `trace_sub_10352ae_call_from_scene_rebuild ... stack0Name=MMORPGTempcbm uiName=MMORPGTempcbm`
  - `trace_parse_actor_motion_entry ... arg4Name=MMORPGTempcbm uiName=MMORPGTempcbm`
  - `trace_resource_stream_call_from_actor_motion ... name=MMORPGTempcbm uiName=MMORPGTempcbm`
- no competing `trace_sub_10352ae_call_from_1036768` line carries that same string on the observed path

This establishes the currently active misroute as:

- `0x1B/12.name`
- copied into active UI name cache (`+1141`)
- reused by `scene_rebuild_runtime_nodes()` as the stacked extra argument for `sub_10352AE()`
- forwarded into `parse_actor_motion_descriptor()`
- treated as a motion-resource name by `load_resource_stream_by_name()`

Additional confirmed asset-format constraint:

- falling back to a normal on-disk actor resource such as `h_warrior.actor` is not a correct long-term substitute for the missing motion descriptor
- runtime evidence:
  - `scene_rebuild_runtime_nodes()` now forwards `stack0Name=h_warrior.actor`
  - `parse_actor_motion_descriptor()` consumes `arg4Name=h_warrior.actor`
  - `load_resource_stream_by_name()` opens the real file successfully
  - but the next resource-open attempts become malformed non-field strings such as `iorwalk1.gif...h_warriorwalk2.gif...jian.gif`, which shows the parser is interpreting the file with the wrong schema
- local asset evidence:
  - scanned `bin/JHOnlineData/*.actor` files in the repo all currently carry type byte `0x02` at offset `+4`
  - none of the tracked local `.actor` files carry the mock motion-wrapper marker `0xF1`

This means the repository currently has many ordinary actor/image-sequence `.actor` files, but no confirmed real motion-descriptor sample matching what `parse_actor_motion_descriptor()` expects.

### First-Scene Local Resource-Miss Request After HUD Seed Fallback

Status: request timing/shape `confirmed`; exact semantic family `hypothesis`

Newest confirmed runtime sequence on the current branch:

- `trace_status_meter_seed_fallback` fires once on the first-scene empty-head HUD path, after which `scene_draw_status_panels()` reaches divide sites with `displayMax=120/100`
- the same session then reaches `scene_runtime_tick label=actor_pass`
- immediately afterwards, local resource open fails through `sub_100D338() -> sub_100D2BE()` for GBK name `侠剑江湖`
- the client then sends a new unhandled `WT len=49` request whose decoded object sequence is:
  - `1/12/1`
  - `1/7/42`
  - `1/6/1`
  - `1/6/13`
  - `1/6/14`
  - `1/2/10`
  - `1/25/5`
- the same payload also carries field `Type=101`

Why this matters:

- the missing local key is the selected role name, not a known asset filename
- this makes the next blocker look less like a generic scene-channel stall and more like a resource-key misuse on the first in-scene actor/bootstrap path

Current best inference from static + runtime evidence:

- the request likely belongs to the net-manager missing-resource/update family rather than to the earlier scene-business `0x1E` / `0x1B` protocol chain
- evidence for that inference:
  - the packet is emitted immediately after a direct local-open fail
  - the net manager already has a confirmed resource-miss queue path (`get_net_manager_object()+56`, later `send_update_chunk_request()` for `type=6`)
- this is still only `hypothesis` until the new queue-entry traces confirm which enqueue method and request-type produced the `len=49` packet

Current narrowed response-side static facts:

- `1/2/10` lands in `net_handle_actor_move_info()` case `10`, which calls `sub_1012958()` and only iterates `otherinfo` when field `othernum > 0`
- `1/6/1` lands in `net_handle_task_response_dispatch()` case `1`, which treats field `taskinfo` as a counted blob and tolerates a zero-length / zero-count path
- corrected 2026-06-08: `1/6/13` is now confirmed safe for the all-empty tasktype shape. The task dispatcher case `13` walks six `tasktypes` entries; the confirmed record grammar is raw field payload containing six `tagged i8 + len16 C string` records. Older same-window crashes came from an extra blob-wrapper length and then from using tagged i16 instead of tagged i8.
- `1/6/14` has a confirmed safe zero-item branch: case `14` first reads field `action`; when `action==0`, it reads `tasknum` plus `taskinfo` and tolerates `tasknum=0` with an empty blob
- `1/25/5` still has no confirmed direct local consumer, but returning the same subtype with a minimal numeric `result` shell is structurally harmless; the older `1/25/12 {result=4}` clear path remains separately confirmed in `net_handle_info_banner_state()`

Current emulator-side compatibility response:

- `src/main.c` now contains a very narrow built-in `builtin-scene-resource-followup` reply for this exact `len=49` / `Type=101` packet shape
- current synthetic reply objects are:
  - `1/12/1 {learnednum=0, learnedskill=\"\"}`
  - `1/7/42 {booknum=0, booksinfo=\"\"}`
  - `1/6/1 {taskinfo=<empty blob>}`
  - `1/6/13 {tasktypes=<6 x tagged-i8 + empty C-string record>}`
  - `1/6/14 {action=0, tasknum=0, taskinfo=<empty blob>}`
  - `1/2/10 {othernum=0}`
  - `1/25/5 {result=4}`
- status: `confirmed`
- the next rerun proves the fuller 7-object reply is accepted on the live scene path:
  - `bin/logs/net_packets.log` shows `source=builtin-scene-resource-followup responseLen=246` for the exact former `WT len=49` request
  - `bin/logs/net_trace.log` logs `mock_scene_resource_followup_response objects=7 ... len=246` followed by `handled_packet ... count=7`
  - immediately after the queued `event=7` is consumed, `loadFlags` drop from `1,0,0` to `0,0,0` and the same session continues through repeated `scene_runtime_tick label=draw_pass/status_panels/actor_pass`
  - the same latest tail contains no new `send_payload`, no new unhandled-packet marker, and no assert/abort in `stdout_trace.log`
- the next manual in-scene interaction confirms the same conclusion from the other side too: after the accepted `len=49` follow-up, later touch events generate only local `touch_dispatch` trace lines and still do **not** emit a newer outbound WT packet
- implication: once this exact `1/12/1 + 1/7/42 + 1/6/1 + 1/6/13 + 1/6/14 + 1/2/10 + 1/25/5` family is answered, the visible center/bottom progress strip is no longer evidence of a missing server response by itself; the next blocker has moved into local scene/UI state
- the newest rerun keeps that protocol conclusion intact: even after `trace_loading_overlay_suppressed` begins firing for scene-bootstrap `sub_1003568()` calls, the remaining strip is still drawn locally in the settled scene and no newer outbound WT request appears. The current blocker remains inside scene/UI draw state, not packet sequencing.
- the next rerun narrows that local state further: the persistent strip is the scene-runtime `loading.gif` widget created by `scene_get_loading_gif_widget()`, not any additional wire-level wait. No newer WT request accompanies its steady-scene draw path.
- the latest rerun keeps the protocol conclusion unchanged even after partial UI suppression: once the scene `loading.gif` widget is disabled, no new outbound WT appears and the remaining strip fragments still come from local picture-library draw helpers. The current blocker is still purely local scene/UI behavior.
- a later manual in-scene click exposed one more genuine wire-level gap unrelated to the strip itself: the client can also send a standalone one-object `WT len=19 hdr=2/10 objs=1/2/10` after scene entry. Before this round, only the bundled `len=49` scene-followup path answered `1/2/10`, so the standalone request fell through to `assert(0)`.
- status: `partial -> now implemented`
- current mock behavior now also accepts the standalone `1/2/10` shape and returns the same minimal object as the bundled follow-up:
  - `1/2/10 {othernum=0}`

## Login Protocol

### Overview

The runtime login UI lives in `mmTitleMstarWqvga.cbm`.

Status: `confirmed`

Login-form flow:

- render: `0x2C96 -> login_form_render`
- touch dispatch: `0x2BE0 -> login_form_handle_touch`
- button/selection action: `0x2B1E -> login_form_handle_action`
- submit: `0x1E40 -> login_form_submit`
- request builder: `0x1B9C -> net_build_login_request`
- primary response handler: `0x16DC -> net_handle_login_response`

### Login Request

Status: `confirmed`

Account/password submit path:

`login_form_submit -> net_build_login_request(1, 1, 6)`

Request fields emitted by `net_build_login_request`:

- `coreVer`
- `appVer`
- `userName`
- `password`
- `imsi`

Notes:

- `userName` is used on the direct account/password submit path.
- A nearby alternate path uses `username` instead of `userName`. Status: `confirmed` for the existence of both spellings in client code, `hypothesis` for requiring server compatibility with both until a real server trace or deeper handler analysis confirms it.

### Request / Owner Split

Status: `confirmed`

The title module has two distinct login request families, not one request with interchangeable field spellings:

- `title_four_choice_screen_handle_action()` (`0x2724`)
  - when local `choiceSel == 0`, confirm/center input sends `net_build_login_request(1, 12, 5)`
  - this is the four-choice screen's direct-enter / alternate login path
- `login_form_handle_action()` (`0x2B1E`)
  - on action `4096`, after non-empty username/password checks, it calls `login_form_submit()`
  - `login_form_submit()` (`0x1E40`) sends `net_build_login_request(1, 1, 6)`
  - this is the explicit account/password submit path

`net_build_login_request()` (`0x1B9C`) also uses different credential sources on those two branches:

- request subtype `1`
  - writes `userName`
  - pulls username/password from the live edit buffers at `r9+10816` / `r9+10812`
- request subtype `12`
  - writes `username`
  - pulls username/password from the saved `mmorpg_LoginRecord` block at `r9+10772 + 16/+48`

Current high-confidence comparison:

| Local trigger | Request on wire | Request-side fields | Response owner | Success routing | Likely local destination |
| --- | --- | --- | --- | --- | --- |
| Four-choice screen, `choiceSel == 0`, confirm/center | `1/1/12` | `coreVer`, `appVer`, `username`, `password`, `imsi` | `0x2A50 -> login_alt_response_dispatch_wrapper` | `login_alt_result_dispatch() -> login_stage_success_dispatch()` | stage-dependent title target; `stageFlag==1` returns to the first local title screen, `stageFlag==4` enters the role-list family |
| Login form confirm (`4096`) | `1/1/1` | `coreVer`, `appVer`, `userName`, `password`, `imsi` | `0x2D80 -> login_primary_response_dispatch` | `net_handle_login_response(packet, 1) -> login_response_result_dispatch()` | primary success target at `+0x4C`, i.e. the server-list / role-list composite family before later role selection |

Implication:

- `1/1/12` should not be treated as a harmless spelling variant of `1/1/1`
- if the emulator answers a `1/1/12` request with the `1/1/1` primary-success contract, or vice versa, the packet may still look "login-shaped" but will route through the wrong owner and wrong local target family
- for the expected server-list selection flow, the highest-confidence path remains the explicit login-form `1/1/1` request followed by a primary `1/1/1` response carrying `result=1`, `serverinfo`, `servernum`, and `newVer`

### Login Response Dispatch

Status: `confirmed`

Two wrappers feed login responses into `net_handle_login_response`:

- `0x2D80`
  - matches `packet[4] == 1 && packet[8] == 1`
  - calls `net_handle_login_response(packet, 1)`
  - this is the primary account/password login response path
- `0x2A50`
  - matches `packet[4] == 1 && packet[8] == 12`
  - calls `net_handle_login_response(packet, 0)`
  - this appears to be a related alternate login/enter flow

### Response Fields

Status: `confirmed`

Fields consumed by `net_handle_login_response`:

- `result`
- `serverinfo`
- `servernum`
- `newVer`
- `information`
- `username`
- `password`

Behavior:

- `result` is treated as an ASCII digit, not a numeric integer.
- current emulator note: title-login mock packets that route through `login_response_result_dispatch()` must therefore encode `result` as raw byte `'1'`, `'2'`, etc., not numeric `0x01/0x02`. A live rerun confirmed that numeric `0x01` reaches `login_result_dispatch_entry` as `resultByte=1` but does not hit the success branch, while the same path statically switches on `'1'`.
- `serverinfo/servernum/newVer` are parsed into the in-memory server list/state when present.
- for `result == '3'` or `result == '4'`, the handler also copies returned `information`, `username`, and `password` into login-related buffers.
- for `result == '2'`, the handler copies `information` into a message buffer and marks a local error state.

### Success Path

Status: `confirmed`

Primary success path after account/password submit:

1. `login_form_submit` copies current edit buffers into `mmorpg_LoginRecord`.
2. `net_build_login_request(1, 1, 6)` sends the login request.
3. response wrapper `0x2D80` validates `type=1, subcmd=1`.
4. `0x2D80` calls `net_handle_login_response(packet, 1)`.
5. if `result == '1'` and the "save default account" flag is set, `0x2D80` calls `0x1430` to persist the active login record.
6. `0x2D80` then calls `0x23C0(resultChar)`.
7. `0x23C0('1')` calls the generic next-step callback object at `R9+0x28CC`, method slot `+0x14`, with target `*(R9+0x29E8 + 0x4C)`.

Newest runtime confirmation:

- once the mock `result` field is encoded as ASCII `'1'`, the live path reaches `login_result_dispatch_success`, swaps the active local object from `login_form` (`05010db0`) to the `+0x4C` family (`0501f838`), seeds `listPtr`, and immediately emits a live `1/1/16` request
- this makes `1/1/16` the next required server/mock contract after clean primary success, not an optional side experiment
- the current minimal follow-up `1/1/16 {result=1} + 1/1/4 {actorinfo}` is also accepted on the same live path: both subtype `16` and subtype `4` are forwarded into the active `target4c` family object, after which the local object advances into the sibling `target50` family

Additional confirmed title/login composite request:

- a newer live title branch sometimes emits one WT packet with object sequence `1/1/4 + 1/1/16` instead of the earlier standalone `1/1/16`
- observed wire shape is `WT len=46 hdr=1/4 objs=1/1/4,1/1/16 count=2`
- the `1/1/4` object carries `serverID=0` and `moneytype='2'`, while the trailing `1/1/16` object is empty
- current mock handling therefore needs to inspect the full object list, not just the first-object header bytes, and answer this composite packet with the same parser-safe stage reply as standalone `1/1/16`:
  - `1/1/16 { result = 1 }`
  - `1/1/4 { actorinfo = counted title-side role-entry table }`

Evidence:

- runtime negative before the fix: `bin/logs/net_packets.log` shows `主机处理信息 unhandled_packet WT len=46 hdr=1/4 objs=1/1/4,1/1/16 count=2`, immediately followed by `assert(0)`
- matching payload bytes in `bin/logs/net_trace.log`: `unhandled_packet_payload len=46 hex=5754002e01010400250873657276657249440006000400000000096d6f6e65797479706500030001320101100005`
- existing accepted standalone stage reply is already confirmed by the same title-family consumer chain: `title_handle_role_list_response()` on the `target4c` path in `mmTitleMstarWqvga.cbm`

Additional confirmed local-state details:

- `login_form_init()` (`0x2AA8`) explicitly sets `*(r9+10735) = 1`, so the account/password login form's "保存为默认帐号" checkbox is on by default on that path.
- `0x23C0('1')` is a local UI/screen transition, not a direct packet sender.
- the same callback object/method is reused by the role-list parser:
  - `sub_247A()` calls `R9+0x28CC -> +0x14` with target `*(R9+0x29E8 + 0x50)`
  - this means `+0x4C` and `+0x50` are sibling screen/state targets, not ad hoc function callbacks
- the target family behind the later `+0x50` role-list path is now clearer:
  - `sub_4290()` dispatches by `*(r9+10748)`: `0 -> sub_3ECE()`, `1 -> sub_4016()`, `2 -> sub_3FD6()`
  - `sub_3AD2()` resets that family and explicitly sets `*(r9+10748) = 0`
  - `sub_39A4()` explicitly sets `*(r9+10748) = 1` and prepares the local role-entry buffers
  - `sub_3544()` (the confirmed `type=1, subcmd=4` role-list parser) finishes by calling `sub_247A()`

Current highest-confidence interpretation:

- primary `1/1/1` success with `serverinfo/servernum/newVer` transitions into a local server-selection / next-screen target at `+0x4C`
- later `1/1/4` role-list success transitions into the sibling role-selection target at `+0x50`
- this is why a stripped validation reply can consume fields correctly yet emit no immediate follow-up packet: the next step is first a local screen/state switch, not a mandatory network send

Important current mock caveat:

- on the account-login branch, the current built-in mock can bypass this wrapper entirely if it answers a `1/1/1` request with an object whose response subtype is not `1`
- existing runtime evidence shows exactly that mismatch on one live branch:
  - request: `1/1/1`
  - built-in response label: `source=builtin-login`
  - actual response object header: `1/1/6`
- when that happens, title wrapper `0x2D80` fails its `subcmd == 1` check, so `net_handle_login_response()` is not called at all on that path
- this is why fields such as `serverinfo/servernum/newVer` and the later default-account save path can appear "missing" even though the request was recognized as `builtin-login` by the host

### Login Success Branch Split

Status: `mixed`

Confirmed title-module behavior:

- the role-list UI has its own later actor-confirm submit path in `mmTitleMstarWqvga.cbm`
- `sub_4016()` handles confirm/select actions on the role-list screen
- when the user confirms a role, `sub_4016()` calls `sub_3E66(...)`
- `sub_3E66()` builds a network request containing the literal field name `actorinfo` plus three selection-derived values
- there is also a separate title-module response handler `sub_3544()` for `type=1, subcmd=4`
- `sub_3544()` reads fields `result`, optional `servconf`, and then a field also named `actorinfo`
- in that `subcmd=4` path, `actorinfo` is parsed as a counted role-entry list (capped to 5 entries locally), not as the later in-scene actor-status blob
- after parsing that list, `sub_3544()` calls `sub_247A()`, which invokes the same generic next-step callback slot `*(r9 + 10464)()`
- the decompiler shorthand `*(r9 + 10464)()` is now known to be the same router method used by `0x23C0`: `R9+0x28CC -> +0x14`
- that role-list success callback uses target `*(R9+0x29E8 + 0x50)`, whereas the primary `1/1/1` success path uses sibling target `*(R9+0x29E8 + 0x4C)`
- the local composite server/role screen behind that path is stateful:
  - `*(r9+10748) == 0` routes input into `sub_3ECE()` (server-selection navigation)
  - `*(r9+10748) == 1` routes input into `sub_4016()` (role-selection / confirm)
  - `*(r9+10748) == 2` routes input into `sub_3FD6()` (follow-on editor/create-name path)
- `sub_3ECE()` can auto-promote from server mode into role mode by calling `sub_39A4()` when the current server list count is `1`
- `sub_3544()` is also stateful across two subtype values:
  - when `subtype == 16`, it reads and caches `result`
  - only when that cached result is `1` does a later `subtype == 4` packet actually parse `actorinfo`
- current runtime request evidence now adds one more confirmed split point: the live account-confirm tap is sending a top-level `1/1/12` login request (`send_payload ... 01010c00 ... coreVer/appVer/username/password/imsi`), not the earlier assumed `1/1/6`

Current higher-confidence interpretation:

- the branch between "login succeeded" and "show role list" is more likely controlled by outer response stage/wrapper (`subcmd=1` versus `subcmd=4`) than by a magic inner field inside the `subcmd=1` login-success object's `actorinfo`
- the active post-login title screen may also be using the alternate `subcmd=12` success chain (`sub_2A50 -> login_alt_result_dispatch -> login_stage_success_dispatch`) before any role-list-specific stage is even eligible
- reusing the same field name `actorinfo` across these title/game stages is now a confirmed source of confusion: title `subcmd=4.actorinfo` behaves like a role-list payload, while main-CBE login `actorinfo` is the later actor/player blob parsed by `parse_actorinfo_response()`

Confirmed runtime evidence from the current mock branch:

- when the login-success mock returns `actorinfo` immediately, the client does not stop on a role-list request
- the next observed traffic jumps straight into the later post-login game chain (`type=1/2/3`), eventually reaching fresh-enter scene loading
- no separate `0x0A/0x20` role-list request appears on that actorinfo-first path
- the first post-login game packet is not just `1/5/10 id=...`; it is a bundled WT request containing both `1/5/10 id=10001` and `1/7/7 type=1`
- previous built-in replies only answered that bundle with group info plus `0x0A/0x1A` and scene `0x1E/1/3/7`, which left no top-level `7` family response before the first HUD draw
- a focused `1/7/8` experiment was tried next: append one top-level `1/7/8` object carrying `type=4, result=1, seq=1`, because static `sub_1033544()` analysis shows that exact branch can produce the missing `type=15` HUD source head and trigger `scene_rebuild_status_meter_node(2) -> scene_rebuild_status_meter_node(1)` when the source head is still empty
- that experiment is now confirmed unsafe on the default path. Runtime does reach `trace_business_handler ... kind=7 subtype=8`, but then crashes earlier inside `sub_1033544()` because the branch first clones local pointer `*(R9+38020)`, which is still null on the current post-login path. The experiment therefore remains useful as evidence, but is now gated behind `CBE_GROUP_TYPE1_MISC_SYNC8=1` instead of staying enabled by default

More precise current reading of that bundled post-login request:

- `source=builtin-group-type1` is not one clean real server answer yet; it is a mixed convenience bundle the emulator currently uses to push the client forward
- the bundled request is not emitted by the title/login screen. Runtime and static evidence now both place it in `scene_runtime_init_and_sync()` on the first scene screen (`activeScreen=01053450`):
  - `bin/logs/net_trace.log` shows the `len=35` send after `trace_sub_1010228_callsite label=scene_runtime_init_and_sync ... activeScreen=01053450`
  - static `scene_runtime_init_and_sync()` at `0x01013594..0x01013636` first calls `sub_1010228()`, then queues three outgoing `major=1, kind=7, subtype=7` requests with field `type = 1..3`
  - the same block uses the scene-local queue/dispatcher objects at `R9+0x5554` and `R9+0x5520`, so this is a scene-bootstrap follow-up sequence, not a login-form request
- `sub_1010228()` is the pre-send helper that explains why the first `type=1` request is bundled with `1/5/10`:
  - it rebuilds the 0x130-byte local group/party snapshot at `*(R9+0x5CE4+0x10)`
  - it copies current node fields such as id/name/status bytes from `*(R9+0x5C64+0x40)`
  - it sets local send/state flags at `R9+0x5C74` and `R9+0x5C79` to `1`
  - runtime evidence already shows the first queued `type=1` leaves as the combined `1/5/10 id=10001 + 1/7/7 type=1` packet, while later `type=2` and `type=3` are sent alone
- the `1/5/10` half does correspond to a real client request and a real handler family:
  - response object `1/5/10` lands in `net_handle_group_info()` subtype `10`
  - that subtype reads `num`, `groupinfo`, and `leadid`
  - `groupinfo` is parsed as a counted blob of group-member records, looped `num` times, and each decoded entry is forwarded into `sub_1011E1E(...)`
  - static recovery of the subtype-10 record reader shows each `groupinfo` record is decoded as `u32 id, string name, u8, u8, u8, u32, u32, u32, u32`.
  - runtime evidence from the failed all-tagged experiment confirms the `u32` fields must be raw big-endian values inside the blob: row1 id became `0x00140000`/`0x00040000` instead of `105`.
  - runtime evidence from the failed all-raw experiment confirms the three `u8` fields still use the tagged `00 01 VV` grammar: row1 id became `105`, but `row+0x22/+0x23` stayed `0,0`, so `scene_draw_team_member_status_list()` (`0x01014388`) selected a null callback and crashed with `pc=0`.
  - the callee `sub_1011E1E()` / `AddRoleToList()` (`0x01011E1E`) writes the decoded entry into the `Global_R9+23796` four-row compact table used later by Battle.cbm as `battleState+0x50`
  - `leadid` is used as the active/leader id for subtype `10`
  - unlike subtype `3`, subtype `10` enters the parse path unconditionally, so `result` is not the decisive gate there
- negative experiment:
  - using `1/5/10.groupinfo` to seed the touched monster is confirmed unsafe as a default behavior. It does populate `Global_R9+23796` and Battle.cbm then reads compact ids `10001,105,0,0`, but the same row is treated by the main scene as a party/group member.
  - runtime evidence: after the mixed-grammar seed, the map showed an offline teammate entry (`ui_offline.gif` resource path) and battle operate mapping logged `wireMapped=1_to_1`, producing self-directed damage instead of player-to-enemy damage.
  - `src/main.c` therefore keeps the parser experiment behind `CBE_GROUPINFO_TEMPLATE_SEED=1`, default off. The remaining enemy-template-table problem must be solved through a non-party source, not by abusing `groupinfo`.
- the current `0x0A/0x1A` object is also backed by a real handler, but it belongs to a different family:
  - it lands in `net_handle_role_login_gift_glamour()` subtype `26`
  - that branch reads only `name` and `money`
  - it copies `name` into the three local role/status objects and mirrors `money` into the same trio
  - extra fields the emulator currently sends there such as `result`, `type`, and `npcnum` are not used by subtype `26`
- the appended `0x1E/1`, `0x1E/3`, and `0x1E/7` objects are scene-channel helpers:
  - `0x1E/1` consumes `scene + posinfo`
  - `0x1E/3` expects `curpage/pagenum/colnum/colnames/roomnum/roomlist/npcnum/npcinfo`
  - `0x1E/7` expects `roomid/colnum/colnames/rolenum/rolesinfo`

Current implication:

- the bundled request is best understood as "group info request + another top-level `7` family sync request", not as a single monolithic semantic
- more specifically, `1/7/7` now looks like a scene-side "misc player fields" bootstrap request family:
  - the sender is in scene init, not title/login
  - the live sequence always emits `type=1`, `type=2`, and `type=3` together during the same first-scene bootstrap window
  - current confirmed response consumers fit that reading: `type=2` expects a later top-level `7/20` carrying `pcimg`, and `type=3` expects top-level `7/32` carrying `expcard`
  - `type=1` is the broadest member of the trio and is the one currently still missing its true top-level `7` reply; it is also the only one that leaves bundled with group info
- the emulator's current `builtin-group-type1` response only answers the group half faithfully
- the other half, `1/7/7 type=1`, still lacks its true top-level `7` reply; the current `0x0A/0x1A + 0x1E/1/3/7` additions are useful scene-bootstrap shims, but they are not yet proven to be the real server contract for that request

Hypothesis / current server-mock reading:

- returning `actorinfo` in the initial login-success payload is effectively equivalent to pre-selecting a role
- the safer role-list-first server behavior is therefore:
  - login success still returns a normal success object so the title module can leave the login form
  - the actual role-list content may need to arrive as its own `type=1, subcmd=4` stage rather than as extra objects stuffed into the initial `subcmd=1` success packet
  - `actorinfo` is withheld until the client later submits the explicit role-selection request

Current built-in mock experiment:

- `src/main.c` now defaults to a staged retry instead of the previous same-packet mixed-object retry
- the primary login reply still uses the normal actor-success object so the current title/game parser contract remains satisfied
- immediately after that login reply is queued, the mock now queues a second `event=7` packet containing a dedicated title role-list object
- that follow-up packet is currently built as two title-side objects:
  - `major=1, kind=1, subtype=16` with `result = 1`
  - `major=1, kind=1, subtype=4` with `actorinfo = counted role-entry table`
- the counted role-entry table is currently modeled from `sub_3544()` as:
  - count
  - repeated `u32 roleId, u8 job, u8 sex, string roleName, u32 level`

Implementation note for the current mock branch:

- the live `1/1/12` path must be keyed from the WT header `kind/subtype` bytes `request[5]/request[6]`
- reading `request[6]/request[7]` on the observed `01010c00` login request misclassifies the stage as `12/0`, which prevents the alternate-success shell and the intended `after-main` staged role-list follow-up from firing

Newest order-specific runtime evidence:

- the mixed same-packet experiment is now confirmed sensitive to object ordering as an open question
- `rolelist + actorinfo` in one login-success packet still auto-enters scene on the current branch
- the next active experiment is `actorinfo + rolelist`, to test whether a later role-list object can override the earlier actor-success side effects inside the same event

Supporting evidence:

- title-module role confirm path: `sub_4016()` (`0x4016`) and `sub_3E66()` (`0x3E66`) in `mmTitleMstarWqvga.cbm`
- title-module role-list response path: `sub_3544()` (`0x3544`) in `mmTitleMstarWqvga.cbm`, which validates `type=1, subcmd=4` and parses counted `actorinfo` entries
- current runtime log: `bin/logs/net_trace.log` shows `mock_login_actor_response ...` immediately followed by outgoing `type=1`, `type=2`, and `type=3` packets, with no intervening built-in role-list request

### Non-Success Result Routing

Status: `confirmed`

Post-handler routing from `0x23C0`:

- `result == '1'`
  - invoke generic success callback `*(v1 + 10464)()`
- `result == '2'`
  - show a client message via `sub_10C6(&unk_258C, 0)`
- `result == '5'`
  - show a client message via `sub_10C6(&unk_2580, 0)`
- `result == '6'`
  - show a client message via `sub_10C6(&unk_25A0, 0)`

### Alternate Success Path

Status: mixed

Confirmed alternate login-related response path:

1. wrapper `0x2A50` validates `type=1, subcmd=12`
2. it calls `net_handle_login_response(packet, 0)`
3. it then calls `0x19C2(resultChar)`
4. `0x19C2('1')` falls through to `0x1956('1')`
5. `0x1956('1')` dispatches to a stage-dependent callback:
   - if local stage flag `*(r9+8276)+357 == 1`, call callback stored at `*(v1 + 10800)`
   - if local stage flag `*(r9+8276)+357 == 4`, call callback stored at `*(v1 + 10804)`

Status notes:

- `confirmed`: wrapper checks, call chain, and callback dispatch exist as described
- `hypothesis`: the gameplay meaning of subcmd `12`, stage flag values, and the two callbacks still needs runtime confirmation

Additional current-mock implication:

- the alternate wrapper still does call `net_handle_login_response()`, but it does **not** contain the later default-account persistence logic from `0x2D80`
- current built-in `1/1/12` success shells are also minimal (`result=1` only), so even when this path is hit there is no `serverinfo/servernum/newVer` payload to populate the local server-selection tables
- current `src/main.c` default mock split now mirrors the request family directly:
  - `requestSubtype == 1` returns `vm_net_mock_build_login_primary_validation_response()`
  - `requestSubtype == 12` returns `vm_net_mock_build_login_alt12_success_response()`
- current validation build now narrows the queued `1/1/15 {result=1}` follow-up to the alternate `1/1/12` request family only
- implication: primary `1/1/1` success can now be tested as a cleaner control path, while the older alternate `1/1/12` branch still keeps its previous staged-mode follow-up behavior
- latest clean-control runtime result is negative but useful:
  - the rerun shows only the expected primary request/response pair (`send_payload ... hdr=1/1`, `mock_login_primary_validation_response top=1,1,1 ...`, `handled_packet ... hdr=1/1 objs=1/1/1`)
  - there is no queued `1/1/15` follow-up in the same window
  - despite that, the live callback owner at delivery time is still `dispatch=05017509`, and `trace_title_login_state` is unchanged across `pre/post_data_event_05017509`
  - sampled fields remain `mode=0`, `listPtr=0`, `roleFamily=0`, `activeScreen=010534b4`
- updated implication: removing `1/1/15` contamination is not enough to restore the expected server-list flow. On the current path, a clean primary `1/1/1` success is still being delivered while `sub_938` owns the callback window, so the remaining blocker is more likely "who should switch the active response owner/target before primary success lands" than "which extra follow-up packet should repair things afterward"

## Business Scene-Entry Notes

Business-network entry in the main CBE is `0x01012E4C`.

Status: mixed

Confirmed fields seen across mocked game responses:

- login/role:
  - `actorinfo`
  - `playerinfo`
- scene/map:
  - `scene`
  - `posinfo`
  - `npcnum`
  - `npcinfo`
- resource/update:
  - `version`
  - `start`
  - `totalsize`
  - `crc`
  - `data`
  - `result`
- other observed payloads:
  - `rolesinfo`
  - `roleinfo`
  - `iteminfo`
  - `giftinfo`
  - `battleinfo`

For current `type=2/3` game-entry responses, the mock needs:

- default response subtype `0x1A`
- `scene = "00蓬莱仙岛_01"` as GBK bytes
- `posinfo` carrying two tagged `i16` coordinates. The current mock default is `223,382`, matching the first entry spawn point parsed from `bin/JHOnlineData/c00蓬莱仙岛_01.sce`.

If actor resource offsets need deeper decoding later, prefer reverse-engineering the `actorinfo` layout from CBE `0x0100FA88` rather than forcing actor state by direct global writes.

Status notes:

- `confirmed`: the listed field names are observed in the client and current mock flow
- `confirmed`: current mock-driven entry requires a scene-channel object carrying GBK `scene` and `posinfo` to progress
- `hypothesis`: exact semantics and full binary layout for `actorinfo`, `playerinfo`, `npcinfo`, `rolesinfo`, `roleinfo`, `iteminfo`, `giftinfo`, and `battleinfo` are not yet fully decoded

### Mock Player Position Persistence

Status: `confirmed` for current mock behavior; `hypothesis` for the full live-server save contract.

Confirmed runtime/request evidence:

- normal in-map walking emits repeated `WT len=32 hdr=2/1 objs=1/2/1` packets with a single `moveinfo` field
- the observed `moveinfo` value is a 10-byte blob such as `03 03 03 00 00 00 00 00 00 00`; this looks like a compact direction/step stream rather than absolute coordinates
- `net_handle_actor_move_info()` only consumes server `moveinfo` on subtype `2`; subtype `1` upload acks can remain empty without parser side effects
- current client scene nodes expose the already-applied local position at `currentNode+0x18/+0x1A` and target at `currentNode+0x11E/+0x120`, as traced by `trace_scene_current_node_publish` / `trace_scene_current_node_draw`

Current mock-server behavior:

- when a `2/12` request is received, the emulator now returns a minimal one-object `1/2/12` response with `name` seeded from the scene-key helper (`vm_net_mock_scene_key_name()`) so `net_handle_actor_move_info()` can safely refresh `uiState+1141`
- this path is treated as a live `hypothesis`: the latest unhandled sample is `send_payload len=9 ... 5754000901020c0005`, which has no payload fields
- when a `2/1 moveinfo` upload is received, the emulator reads the current player scene node without modifying guest memory and persists `scene,x,y` to `nvram/jhol_mock_player_pos.bin`
- scene-change responses also persist the target scene and spawn point selected from `mapID/exitID`
- later login/scene-entry builders reuse that persisted state for:
  - the `actorinfo` scene key and actor grid/motion arguments
  - `30/1` / `30/2` scene fields
  - `posinfo`
- `CBE_SCENE_KEY`, `CBE_SCENE_POS_X`, and `CBE_SCENE_POS_Y` remain explicit environment overrides for controlled reruns

The live-server save timing is still not fully proven. The current mock treats the already-applied local node position at upload time as the authoritative server-side last position.

Correction after the next rerun:

- do not persist arbitrary scene text read through best-effort guest-label decoding
- evidence: a saved non-canonical scene key was fed back into later scene/resource fields; the client then emitted `18/7 type=6 name=c00蓬莱仙岛_01` and attempted to download/update that scene key, while storage trace showed the local open path degraded to raw `c00...`
- current code now only reuses a persisted scene key if it matches a known canonical internal scene key; otherwise it keeps the saved coordinates but falls back to the default safe scene key
- this preserves position memory while avoiding scene-key encoding contamination of the resource-update path

Second correction after map/position split was observed:

- movement uploads update only the persisted coordinates and preserve the last trusted scene key
- the persisted scene key is updated only from explicit scene-change targets derived from `2/3 mapID/exitID`
- safe scene keys are validated against local `.sce` resources; c-prefixed saved filenames such as `c00蓬莱仙岛_03.sce` are replayed to fresh-enter builders as extensionless `c00蓬莱仙岛_03`, matching the historical actorinfo/sceneKey contract while still loading the correct local `.sce`
- this avoids the split state where a later-map coordinate is paired with an earlier-map scene key

### `actorinfo` trailing string and `R9+0x5E46`

Status: `mixed`

Confirmed parser behavior:

- `parse_actorinfo_response()` / `parse_actorinfo_playerinfo_blob()` at `0x0100FA88` writes directly to the fresh-enter text buffer `R9+0x5E46`
- the write happens on the `a2 == 0` `actorinfo` path at `0x0100FD00 .. 0x0100FD08`
- the source is a late string field from the `actorinfo` blob, copied with `sub_100FA56(srcReader, R9+0x5E46, 0x1E)`
- the same function returns `R9+0x5E46` at `0x0100FD24 .. 0x0100FD2E`

Confirmed neighboring field shape:

- immediately before that string copy, the parser reads another scalar field and stores it both into the actor structure and, on the `actorinfo` path, into scene/global state
- immediately after the `R9+0x5E46` copy, the parser reads two more trailing scalar fields into actor structure slots
- this matches the current mock builder pattern in `src/main.c:vm_net_mock_build_actor_info()`, where the final sequence is:
  - string
  - scalar
  - string
  - scalar
  - scalar

Hypothesis:

- the second trailing string in `actorinfo` is much more likely to be a scene/map key or scene resource identifier than a motion-resource filename
- evidence supporting that interpretation:
  - fresh-enter later reuses `R9+0x5E46` as `currentMapIdText`
  - the same value is validated by `sub_100EEBC()` / `sub_100FA40()`
  - restore/fresh-enter logic later reuses the derived persistent slot as outgoing `mapID`
  - local assets already include scene resources named like `c00蓬莱仙岛_01.sce` and `c00蓬莱仙岛_03.sce`

Open mismatch to keep in mind:

- the current mock builder still fills that second trailing `actorinfo` string from `motionResource`
- runtime/static evidence now strongly suggests that semantic is wrong or at least incomplete
- this does **not** yet prove the exact canonical wire value:
  - it may be a full scene filename such as `c00蓬莱仙岛_01.sce`
  - or an extensionless/internal scene key later mapped to the `.sce` asset

Current mock experiment:

- `src/main.c` now routes that second trailing `actorinfo` string, and the synthetic `0x1B/12.name`, through a dedicated `vm_net_mock_scene_key_name()` helper instead of `motionResource`
- default mock value remains the existing GBK scene key `c00蓬莱仙岛_01`
- `CBE_SCENE_KEY` can now override this field for targeted reruns

Latest confirmed local-load behavior after that change:

- the extensionless scene key is now consumed as a real local scene resource, not as an actor/motion resource
- storage trace shows:
  - `file_open_resolve_map_extension from=JHOnlineData/c00蓬莱仙岛_01 to=JHOnlineData/c00蓬莱仙岛_01.sce`
  - the `.sce` payload contains `SCE2` plus embedded reference `00蓬莱仙岛_01.map`
  - the client then opens `JHOnlineData/00蓬莱仙岛_01.map`
- the `.map` payload expands into scene art such as `m_peak1.gif`, `m_village05.gif`, and `m_apotheosis.gif`
- this strongly confirms that the corrected field belongs to the local scene/map resource family

### `actorinfo` mid-body 20-byte string and `parse_actor_motion_descriptor()`

Status: `confirmed`

Confirmed parser/runtime behavior:

- `parse_actorinfo_response()` at `0x0100FA88` contains an earlier two-string block before the final `actorResource + i16 + sceneKey + i16 + i16` tail
- on the `a2 == 0` actor path, those two strings are copied with `sub_100FA56()` into actor-structure slots `actor+0x100` (size `10`) and `actor+0x10A` (size `20`)
- a later separate string field is copied into `actor+0xD8` before the final scene-key copy into `R9+0x5E46`
- newest `scene_draw_actor_pass()` trace now pins `actor+0x10A` directly: the late failing open is `trace_resource_open_helper ... namePtr=0540010A name=h_warrior.actor`, i.e. the direct image path is consuming the 20-byte mid-body slot itself

Latest runtime evidence:

- the same clean rerun still shows a correct `.actor -> inner gif` chain during scene bootstrap:
  - `trace_resource_stream_call_from_db82 ... name=h_warrior.actor`
  - then repeated `trace_resource_open_helper ... lr=0100d34d ... name=h_warriorwalk1.gif`, `h_warriorwalk2.gif`, `jian.gif`, `guang1.gif`, ...
- but the later actor-pass failure is different:
  - `trace_scene_runtime_tick label=actor_pass ... tick=197/291`
  - immediately followed by `trace_resource_open_helper ... lr=0100d34d ... namePtr=0540010A name=h_warrior.actor`
- static IDA for `parse_actorinfo_response()` plus that `0540010A` pointer makes the active mismatch concrete: the mid-body 20-byte slot is a direct image/decode input, while the later `actor+0xD8` string remains the real actor-container field

Implication:

- this mid-body 20-byte `actorinfo` string is not safe to treat as a role display name
- it is also not safe to treat as the later `.actor` container field
- current strongest fit is “direct preview/frame image resource name”, while the later `actor+0xD8` field remains the `.actor` resource used by the normal descriptor parser path

Current mock change:

- `src/main.c:vm_net_mock_build_actor_info()` now fills that specific 20-byte mid-body string from a new direct-image helper (`h_warriorwalk1.gif`, `hW_warriorwalk1.gif`, `h_assassinwalk1.gif`, `hW_assassinwalk1.gif`, `h_magewalk1.gif`, `hW_mage1.gif`) instead of `roleName` or `.actor`
- the later trailing actor-resource field still uses `vm_net_mock_actor_resource_name()` and remains the source for the correct `.actor -> inner gif` parser path
- the later trailing scene-key field remains separate and still uses `vm_net_mock_scene_key_name()`

### Scene `actorinfo` order correction (`a2==0`)

Status: `confirmed`

The earlier “`actorId, visual bytes, name, playerId/display string, then u32 level/current/base/...`” summary was still too coarse. Full decompile of `parse_actorinfo_response()` at `0x0100FA88` now fixes the scene-side order after the second string more precisely:

- tagged `u32 summaryStatus`, then truncated/stored into `actor+0x138`
- `u32 primaryCurrent`, `u32 primaryBaseMax`, `u32 secondaryCurrent`, `u32 secondaryBaseMax`
- `u32 extra132`
- six tagged `u32` values truncated into word-sized status/stat slots around `actor+0x122`
- `u32 summary176`, `u32 gap09C0`
- two `u8` state bytes
- `u32 primaryDisplayMax`, `u32 secondaryDisplayMax`
- two more tagged `u8` fields, stored into word slots around `actor+0x11E/+0x120`
- bounded strings `shortLabel(+0x100)` and `previewImage(+0x10A)`
- three trailing `u32` fields at `+0xCC/+0xD0/+0xD4`
- bounded `actorResource(+0xD8)`, `i16 actorResourceArg(+0xAC)`, bounded global `sceneKey`, then trailing `i16 +0xF0/+0xF4`

Additional confirmed consumers:

- `scene_copy_status_summary_fields()` copies `actor+0x68[0..]` (`id + short text`) plus `actor+0x138`; this confirms the second bounded string at `actor+0x6C` is part of the compact HUD summary family
- `scene_draw_status_panels()` still uses `actor+0xB4/+0xBC/+0xB8/+0xC0` for current/base values and `sceneStatusMeterNode+0xC4/+0xC8` for derived display maxima
- `parse_actor_motion_descriptor()` still consumes the bounded `previewImage` slot at `actor+0x10A`, while the later `actor+0xD8` bounded string remains the real `.actor` resource field

Current mock behavior now follows this corrected order. The previous single `gap0CC0` placeholder was split into three separate trailing `u32` fields, and the display-name string remains a separate field from the later `shortLabel/previewImage` pair.

### `1/2/10 otherinfo` self-actor seed

Status: `confirmed`

`sub_1012958()` (`net_handle_actor_move_info` case `10`) proves that `1/2/10` is not just an optional “others list count” packet. When `othernum > 0`, the parser walks `otherinfo` and calls `scene_node_find_or_create()` for each entry.

Confirmed per-entry order from `sub_1012958()`:

- `u32 actorId`
- `u8 visualVariant`
- `u8 visualGroup`
- `str labelText`
- `u8 targetPosX`
- `u8 targetPosY`
- `str shortLabel`
- `str longLabel`
- `i16 gridPosX`
- `i16 gridPosY`

Current parser-safe mock behavior now returns an empty list for both:

- the bundled scene follow-up `1/2/10`
- the standalone one-object `WT len=19 hdr=2/10 objs=1/2/10`

The current minimal response is `othernum=0` plus empty `otherinfo`. Evidence: IDA pseudocode for `sub_1012958()` shows it reads `othernum` first and only initializes/walks the `otherinfo` stream when `othernum > 0`; it still returns success and writes `*(R9+23836)=1` after the branch. This makes the empty object a confirmed parser-safe response, while the non-empty per-entry record remains a hypothesis until its field order/timing is reverified.

Latest confirmed limitation:

- after the corrected `actorinfo` tail lands, scene loading can now reach a later local crash from the same broad consumer family: `sub_100F094()` calls selector helper `sub_1004E10()` with a live selector object whose internal table pointer at `selector+0x0C` is still null
- this is currently treated as a local scene-init contract gap, not as proof that the non-empty `1/2/10` record shape above is fully semantically correct
- earlier builds carried a blocker-removal guard at `0x01004E10`, but this class of runtime skip guard has since been removed. The remaining work is to identify which real init/resource path should populate the selector table.
- after that guard, the next live crash is even later in the same scene-local family: `scene_draw_status_panels()` reaches the left portrait block and tries to `BLX` `sceneHudCurrentPortraitWidget->draw(+0x18)` with a null callback
- runtime evidence already shows the six portrait UI actor resources (`ui_h_* / ui_hw_*`) loading successfully before this point, so the remaining issue is currently classified as a local portrait-widget init/callback gap rather than a network/resource-body miss
- earlier builds also carried a `0x010146DA` portrait null-callback guard; that guard is removed under the no-parser/draw-bypass rule.

## Legacy Source

`docs/net_mock_protocol.md` is retained as a historical working note and source document. New durable protocol conclusions should be recorded here first, then the legacy note can be trimmed or cross-referenced as needed.

### `event=7` wrapper vs title/login callbacks

Status: `confirmed`

Current emulator/runtime evidence now separates three layers that had previously been conflated during login-flow tracing:

1. queued task callback
   - current queued `event=7` replies use `0x0103489A -> net_wrapper_event_dispatch`
2. net-manager callback
   - `net_wrapper_event_dispatch` first calls `get_net_manager_object()+0x44`
   - runtime watcher reads the same slot as `R9+0x9588+0x44`
3. business/title callback
   - only after the net-manager leg, and only when the current business object state allows it, the wrapper calls `*(R9+0x94A8)+0x14`

Confirmed current-login observation:

- in the “click confirm but no screen transition” runs, `net_state_observe cb44=01037473`
- static RE confirms `0x01037472 -> handle_version_update_response`
- therefore the queued login reply is still entering the wrapper through the version/update-family callback before any title-local callback can drive a `0x23C0` / `0x247A` style screen-switch path

Implication:

- `runtime_state dispatch=05017509` should not be read as “the queued task directly called the title success callback”
- it is only the current business callback stored on the active object
- lack of `trace_title_screen_callback` at `0x23D4/0x248A/0x1984/0x1994` is consistent with the wrapper never reaching those title-success BLX sites

### Title login local state family at `R9+0x29E8`

Status: `confirmed`

The active title/login state block used by the current screen includes at least:

- `+0x00` / runtime `state0`
- `+0x02` / runtime `focus`
- `+0x07` / runtime `saveDefault`
- `+0x14` / runtime `mode`
- `+0x3C` / runtime `listPtr`

Confirmed local routines around this block:

- `0x2620 -> login_record_init_and_load()`
  - initializes/loads `mmorpg_LoginRecord`
  - writes the local `saveDefault/state0` bytes during button-path setup
- `0x53EC`
  - inbound parser for top-level `type=1` family with subtypes `6/7/8/15`
- `0x5324`
  - subtype `7` handler; appends entries into `*(R9+10788)` and updates list-related local state
- `0x39A4`
  - subtype `15` success path; sets `*(R9+10748)=1`

Current implication:

- the active login/selection state machine behind `010534b4` is better explained by a `type=1, subtype=7/15` follow-up family than by the earlier `1/1/4` role-list hypothesis
- this matches runtime evidence where the current `1/1/1` success reply and staged `1/1/16 -> 1/1/4` follow-up produce no writes at all to `mode/listPtr/roleFamily/target*`
- the current default mock experiment has therefore been reduced to the narrowest confirmed gate first: `1/1/15 {result=1}` without the old staged `1/1/4` role-list payload

### Active `010534B4` title screen and `05017509` owner

Status: `confirmed`

The currently active title screen on the stuck login path is now pinned to a specific `mmTitleMstarWqvga.cbm` transition screen rather than to the earlier `login_primary_response_dispatch()` owner.

- runtime `screen_func_table` for `screen=010534b4` maps cleanly to `sub_C82()` with relocation base `0x05016BD0`:
  - `init=05017621 -> sub_A50 + 1`
  - `destroy=050174b1 -> sub_8E0 + 1`
  - `logic=05017411 -> sub_840 + 1`
  - `render=0501716f -> sub_59E + 1`
  - `pause=05017075 -> sub_4A4 + 1`
  - `resume=05017025 -> sub_454 + 1`
- `sub_A50()` also installs the active business callback by writing `*(R9+10312)+20 = sub_938`
- the runtime `dispatch=05017509` snapshot is exactly that installed `sub_938 + 1`
- `sub_938(event=7)` does not route `type=1, subtype=1` login replies at all; it only iterates packet objects where `kind == 1` and `subtype` is one of `2`, `3`, or `6`
- therefore a lone `1/1/1` reply, including `result=2`, cannot enter `login_primary_response_dispatch() -> login_response_result_dispatch()` while this `010534b4/sub_938` screen owns `R9+21812`

Implications:

- the current failure packet is not merely "missing one more follow-up object" for the `010534b4` owner; the active owner ignores subtype `1` entirely
- the fixed primary failure prompt for `result='2'` does not require any extra stage packet once the correct owner is active
- static `net_handle_login_response(packet, 1)` only needs the `result` byte for the primary fixed-prompt branch; copying `information` is only done on the alternate `a2 == 0` path

Related local confirm path:

- the current confirm button does not locally switch `R9+21812` to `login_primary_response_dispatch()`
- static action flow is:
  - `login_form_handle_action(4096)` validates non-empty username/password and calls `login_form_submit()`
  - `login_form_submit()` copies the current edit buffers into `mmorpg_LoginRecord`, writes stage byte `*(R9+0x2054 + 0x104 + 5) = 5`, and sends `net_build_login_request(1, 1, 6)` which appears on the wire as top-level `1/1/1`
  - no write to `businessDispatch` occurs on this path; latest runtime watcher still shows the only `businessDispatch -> 05017509` install happens during `sub_A50()` initialization of `010534b4`
- implication: the next expected response owner while this screen remains active is still `sub_938`, not `0x2D80`
- current best hypothesis is therefore that this screen expects a different reply subtype family after the `1/1/1` submit, most likely one of the `sub_938`-accepted `type=1/subtype=2|3|6` objects, rather than a direct primary-wrapper-owned `1/1/1` result packet
- current live mock experiment has been narrowed accordingly: the default non-`1234` `requestSubtype==1` success path now returns the existing actor-style `1/1/6` object so the next rerun can test whether `sub_938` begins mutating local title state when fed one of its accepted subtypes; the old `1/1/1` validation packet remains reachable with `CBE_LOGIN_RESPONSE=primary-validate` for control comparisons

Latest runtime result:

- status: `confirmed`
- the `1/1/6` experiment does advance this screen, but not by mutating the old `trace_title_login_state` fields in place
- on the first confirmed non-`1234` run, the wire payload really was `1/1/6`: `bin/logs/net_packets.log` raw dump begins `575401b001010106...`, even though the current packet-summary line still misleadingly prints `objs=1/1/1`
- after the queued `title-mode15` follow-up and the `1/1/6` data event, `010534b4` is removed (`screen_manager idx=6 remove r0=010534b4`), `0105a814` is unwound, and a new module screen `01053450` is created (`screen_manager idx=4 add r0=01053450`)
- the owner also changes: `businessDispatch` leaves `05017509` and is reinstalled as `01012e4d` on the new `01053450` screen
- scene/runtime state advances in the same window (`sceneState=7`, `loadFlags=1,0,0`), which never happened under the earlier `1/1/1` primary-validation replies
- the newer isolated control removes one remaining ambiguity: queued `1/1/15` is not the cause of that jump. With `request 1/1/1` follow-up queueing disabled (`skip_login_followup_event ... queueMode15=0`), a lone `1/1/6` success object still tears down `010534b4`, installs `01053450`, and immediately resumes the usual scene-bootstrap request sequence (`1/5/10 + 1/7/7 type=1`, then `1/7/7 type=2`, `1/7/7 type=3`)
- the subtype-2 comparison now shows the same control-flow result with a smaller business payload. An isolated `1/1/2` success object (`raw ... 01010102 ...`) still removes `010534b4`, installs `01053450`, and resumes the same scene-bootstrap request family, even though the payload drops the subtype-6-only fields and only carries `result, revivetype, ruffianflag, type, lastexp, curexp, persentexp, actorinfo`
- the subtype-3 comparison lands in the same place again. An isolated `1/1/3` success object (`raw ... 01010103 ...`) uses the same smaller field set and payload length as subtype `2`, and still removes `010534b4`, installs `01053450`, and resumes the same scene-bootstrap request family

Current combined implication:

- on the current emulator path, all three `sub_938`-accepted login-success siblings that have been tested in isolation (`1/1/2`, `1/1/3`, `1/1/6`) converge on the same control-flow result:
  - `010534b4` is removed
  - `businessDispatch` is reinstalled as `01012e4d`
  - `01053450` is created
  - the client enters the same scene/bootstrap sequence and reaches `sceneState=7` with `loadFlags=1,0,0`
- therefore the remaining mismatch with the expected pre-map title flow is no longer explained by choosing the wrong subtype among `2/3/6`
- the later `trace_title_router_gate` / `trace_title_flow_action` runs narrow the branch point further:
  - `sub_938` is not directly calling the router callback on the current path; by the time it checks the router gate, `routerFlag17=1` and `routerFlag18=1`, so the `router+44` callback is suppressed
  - the actual teardown is performed by the local `sub_5B0` mini-flow immediately afterwards
  - the decisive local condition is not a late network field but an already-armed object state: the `sub_5B0` object has `obj28=1`, `obj2C=1`, `local10340=1`, and `local10344=1` before the login success completes
  - once `sub_938` accepts the login object, the same local object's `byte2` becomes `3`; `sub_5B0` then hits `sub_5B0_remove_current_screen` and calls `sub_EC(3)`, which is the immediate precursor to `01053450`

Implication:

- the remaining title-stage mismatch is now most likely an upstream local-mode contract problem, not a wrong `1/1/x` success subtype among `2/3/6`
- the next RE target should be: which earlier local title/startup path arms `obj2C=1` and `local10344=1` on the `sub_5B0` object before a true role/manage workflow is entered, and what the expected alternative state should be on a real non-direct-enter login

Newer upstream arming result:

- status: `confirmed`
- that earlier arming path is now identified more narrowly: the `obj28=1` / `obj2C=1` transition is performed by `sub_938(event=5)` itself, not by a separate hidden writer
- static `sub_938()` case `5` does exactly three things in order:
  - writes `*(R9+0x283C+0x28) = 1`
  - writes `*(R9+0x283C+0x2C) = 1`
  - sends `net_build_login_request(99, 1, 0)`
- runtime matches that sequence on the active `010534b4` title screen:
  - at `tick=110`, a second `open_channel connect=2` queues `event=5`
  - at `tick=117`, that `event=5` fires and immediately sends a `len=9` `99/1` request
  - the emulator's current built-in answer is `source=builtin-short-63-1-echo`
  - the resulting `event=7` callback leaves the visible title snapshot unchanged, but the later `sub_5B0` traces already show the object staying in armed mode-1 state before normal account-login success arrives
- the latest dedicated rerun strengthens that from a static/runtime match into a behavior claim:
  - right before `sub_938_case5_arm_mode1`, the local object is still `obj28=0 obj2c=0 local10340=0 local10344=0`
  - immediately after the case-5 trace, the next `sub_5B0_load_mode` sample becomes `obj28=1 obj2c=1 local10340=1 local10344=1`
  - after the current `99/1` echo response is consumed, `trace_title_login_state` remains unchanged and `sub_5B0` keeps looping in mode `1` with `byte2=0`
  - therefore the current mock `99/1` reply is not neutral in effect; it is a confirmed insufficient response that leaves the later direct-enter preconditions armed
- implication:
  - the remaining mismatch is now best framed as a `99/1/0` title-stage contract question
  - the unknown is no longer who flips `obj2C`, but what the real `99/1` response is supposed to do next on this screen before accepted login success siblings (`2/3/6`) are allowed to set `byte2=3`
  - a simple transport close is now ruled down as the primary missing piece: enabling a queued `event=9` immediately after the current `0x63/1` echo does not clear `obj2C/local10344`, does not move the local `sub_5B0` object out of mode `1`, and does not change the later direct-enter outcome
  - a corrected owner/gate watcher run narrows the observed `99/1` side effect further: during the current `event=7` callback, runtime `0501760c` clears `objC.flag10` at `01056100`, and the paired global watcher confirms that same byte is `titleLoadingGate` (`R9+21808`)
  - that gate clear is then consumed locally by the loading widget: `loading_gif_widget_draw()` drops `obj8.flag10` to `0` at `010461c8`, which explains why the already-understood frame counter `obj8_stateA` freezes at `25`
  - importantly, this still does not disarm the title owner's armed mode-1 state; `ownerObj.obj28=1`, `ownerObj.obj2C=1`, `local10340=1`, and `local10344=1` all persist until later login success sets `ownerObj.byte2=3`
  - so the current emulator behavior for `99/1` is now best described as "clear titleLoadingGate / stop the loading widget" rather than "advance the title object into a non-direct-enter mode"; the remaining missing contract is whatever should also clear or reroute `obj2C/local10344` before `sub_5B0` reaches `sub_EC(3)`
  - static recovery now explains that write exactly: runtime `0501760c` is `sub_938+0x104` (`0xA3C`), the generic tail of `sub_938(event=7)`. After the event-7 packet scan completes, case `7` unconditionally clears the local child object at `*(R9+10312)` by writing `+12 = 0` and `+16 = 0`, then returns via the parent callback
  - the later login-success-side gate clear at runtime `050175cc` is the sibling write inside the accepted-object branch (`sub_938+0xCC` / static `0x9FC`): once `type=1/subtype=2|3|6` is accepted, `sub_938` clears that same `+16` byte before checking the router gate, and only then sets `R9+10302` (`ownerObj.byte2`) to `3` or `4`
  - therefore a pure `99/1` event on the current screen is not taking any hidden semantic branch inside `sub_938`; it simply fails the `type=1/subtype=2|3|6` filter and falls through to the generic case-7 cleanup, which is why it clears `titleLoadingGate` but leaves `obj2C/local10344` armed and `byte2` unchanged
  - `sub_5B0()` now gives a plausible concrete consequence for that cleanup. While `obj2C == 1`, its local mode-1 branch watches `*(_WORD *)(*(_DWORD *)(v0 + 8) + 10)` and, once that counter exceeds `300`, it queues `title_activate_role_manage_screen` via `sub_10C6(&unk_82C, title_activate_role_manage_screen)`. That same `+10` field is the traced `obj8_stateA` frame counter
  - `sub_722()` is the matched local disarm/reset path used by the existing role-list / role-manage screens: it zeroes the child counter, restores `*(R9+10312)+16 = 1`, and clears `10332/10336/10340/10344` back to `0`
- because the current `99/1` generic cleanup clears `titleLoadingGate`, the loading widget stops and `obj8_stateA` freezes at `25`, so the built-in `>300 -> title_activate_role_manage_screen` branch in `sub_5B0` never gets a chance to fire before later login success sets `byte2=3`
- this makes the current mismatch look less like “wrong accepted login subtype” and more like “the pre-login title-mode timer/transition is being cut short”: either the real `99/1` contract should keep the counter alive long enough to hit that branch, or some other companion packet/local callback should invoke the same role-manage/reset path directly
- delayed-success control has now refined that further. When the main accepted login-success `event=7` is held back by `360` ticks, the counter is not permanently stuck at `25`: on the latest rerun it resumes, reaches `obj8_stateA=301` at `tick=541`, and runtime logs the exact threshold-site callback `sub_5B0_queue_activate_role_manage`.
- the threshold side effects are now concrete too. In the same `tick=541` window, `obj8.stateA` resets `301 -> 0`, `obj8.flag10` clears `1 -> 0`, and `objC.flag10` / `titleLoadingGate` clears `1 -> 0`, but the owner itself stays armed (`byte2=0`, `obj28=1`, `obj2C=1`, `local10340=1`, `local10344=1`) and the delayed accepted login-success packet is still pending in the scheduler.
- implication: `sub_5B0`'s local timeout branch is real and is now runtime-confirmed, but the currently observed effect is only the child-widget reset plus a queued role-manage activator. The still-missing contract is whatever should execute after `sub_10C6(&unk_82C, title_activate_role_manage_screen)` is queued, because the current session shows no immediate screen replacement or owner-byte transition before the run ends.
- the callback-queue watcher now narrows that one level further. The active title path already commits a queue entry much earlier, during the first `99/1` exchange (`sub_1032_queue_enter` + `sub_1032_queue_commit` at `tick=110`), with `queueText=050177f0` and `queueCallback=0`.
- when `obj8_stateA` later crosses `300`, `sub_5B0_queue_activate_role_manage` does reach `sub_1032`, but only the entry trace appears; there is no second commit trace because `queueActive` is already `1` at entry. So the role-manage activator is currently blocked at queue admission time, before `title_activate_role_manage_screen()` itself ever runs.
- implication: the remaining contract gap is now specifically a stale title-local queue item. Either the earlier `99/1`-era queued item is supposed to clear long before the timeout branch fires, or the emulator is missing the consumer/state change that would release that queue and allow the later role-manage callback to be committed.
- static/runtime correlation now identifies that stale item more concretely. Runtime `queueText=050177f0` relocates back to static data `&unk_C20`, and `sub_938(event=7)` enqueues `sub_10C6(&unk_C20, 0)` exactly on the early branch where its packet helper `(*...+20)(R9+10320, a1, 10, 19)` succeeds. So the long-lived queue item is not a generic later popup; it is created directly by the current `99/1` handling path before any accepted login object is scanned.
- the widened rerun also rules down the obvious local clearers on the current path: after the initial `tick=110` enqueue, no later queue-field writes occur at all, and none of the candidate consumer/reset functions (`title_activate_popup_screen_if_idle`, `sub_F50`, `sub_FF0`, `sub_5922`, `sub_5A74`, role-manage menu/network handlers) execute before the delayed accepted login-success packet eventually forces `byte2=3`.
- implication: the stale-queue problem is currently upstream of those clear/reset helpers. Either the emulator is missing the local callback/timer that should revisit the `&unk_C20` queue item, or the `packet_check(...,10,19)` success branch in `sub_938` is being taken under conditions that should not occur on the intended non-direct-enter title path.
- the newest popup-driver rerun narrows that again: the timer/driver itself is not missing. After the initial `99/1` queue commit, `sub_59E_popup_dispatch` keeps re-entering on the active `010534b4` screen with `queueActive=1`, `queueText=050177f0`, `queueCallback=0`, `popupTarget=0`, and `popupCount=0`, so the stale queue state is being revisited by the local driver.
- the same rerun also rules out a simpler “late accepted login success recreated the popup item” explanation. At the delayed success delivery that later sets `ownerObj.byte2=3`, `sub_938_packetcheck_10_19_result` reports `r0=0`, which matches the static `if (packet_check(...,10,19)) sub_10C6(&unk_C20, 0)` branch shape and means that branch did not succeed on the accepted-object path.
- implication: the remaining contract gap is now below the generic popup timer. The unresolved step is whatever popup-object method or local condition should turn the already-revisited `&unk_C20` queue item into a real popup-screen activation or queue release before the later accepted login success tears the screen down.
- the new bridge watcher narrows that one layer further. On the active `010534b4` title screen, `sub_564()` really is driving the shared objC bridge: before the stale queue becomes active, the bridge still reaches `cb28_poll`, writes `objC.stateC=3`, calls `cb24_dispatch`, and can later enter `cb30_guard -> cb2C_pump` while the child event object reports `eventObj+16 != 0`.
- once `queueText=050177f0` / `queueActive=1` is committed, that richer bridge path disappears. The repeated revisit collapses to `cb30_guard` only, with bridge state holding at `field4=2`, `stateC=0`, and child event state `eventObj+16 == 0`. Static `sub_1034868()` explains why that matters: it only calls `sub_103478E()` when `*(_DWORD *)(eventObj + 16)` is non-zero, otherwise it returns immediately.
- implication: the remaining popup/queue mismatch is now best framed as a child-event rearm problem inside the shared bridge. The high-level queue is present and the timer is still revisiting it, but the bridge's child event object is no longer armed for the `cb2C_pump` path that would actually advance or recycle the queued popup work.
- the newest rerun identifies the concrete arm/disarm helpers too. `event_packet_add_field()` is what writes `eventObj+5 = 1` and increments `eventObj+16`, while the bridge pump path `sub_103478E()` later clears `eventObj+5` and immediately calls `event_packet_init()`, which resets `eventObj+16` back to `0`.
- a newer static pass sharpens the semantics of that child object: it is an outbound WT builder, not an inbound parser. `sub_103478E()` first calls `eventObj+40` (`event_packet_calc_size`) and `eventObj+32` (`event_packet_build_WT`), hands the resulting bytes to the net-manager bridge, and only then resets the object with `event_packet_init(..., 0, n10, n19)`. So the open question is specifically who should add the next outbound object after the stale `&unk_C20` queue item becomes live.
- the newest caller trace now answers part of that directly: the live `event_packet_add_field()` calls on this path come from `alloc_outgoing_game_event()` (`0x0100E2E4`, LR `0100E2F9`). On the latest delayed-success run, that caller appears once in the original pre-queue cycle and then not again until a later explicit local touch/submit path; the stale queue / popup-driver revisit loop does not call it by itself.
- implication: the interesting missing contract is no longer "who cleared `eventObj+16` later". The bridge itself clears it as part of its normal pump/reset cycle. The real gap is what should rearm that child event object again after `queueText=050177f0` has been committed, because on the current emulator path no later call re-populates `eventObj+16`, so `cb30_guard` never re-enters `cb2C_pump`.
- static bridge recovery now narrows that “who should rearm it” question further. `mmTitleMstarWqvga.cbm::sub_564()` is structurally the same as main-CBE `startup_maybe_start_async_net_task()` (`0x0103A77C`): both wait on a local `+0x24` pending field and `+0x20` counter, call shared-bridge `cb28`, then call scene-object slot `+0x15C`, and finally pass that return value into shared-bridge `cb24`.
- main-CBE `scene_object_vtable_init()` identifies slot `+0x15C` as `sub_1018DC6()`, which does not allocate outgoing events itself. It only loads the host string from `mmorpg_config` (or falls back to `jhol.51coolbar.com:20888`) and returns that pointer.
- combined implication: the periodic title/startup bridge that leads to `alloc_outgoing_game_event()` is now:
  - local async driver (`sub_564` / `startup_maybe_start_async_net_task`)
  - host lookup (`scene_object + 0x15C -> sub_1018DC6`)
  - shared-bridge `cb24_dispatch -> open_channel`
  - queued `event=5` callback
  - later `alloc_outgoing_game_event()` in the business request sender
- this means the next active question is not “which direct caller should magically jump to `alloc_outgoing_game_event()`”, but “why the next `cb24/open_channel/event=5` cycle does not restart once the stale `&unk_C20` queue item is live on the current title path”.
- the newest async-counter watcher answers one half of that. The bridge does successfully reach `cb24/open_channel` once on the active `010534b4` path: `ownerObj.asyncCounter` climbs until `31`, then `sub_564_before_cb24_dispatch` / `sub_564_after_cb24_dispatch` fire, `open_channel connect=2` is issued, and `ownerObj.asyncPending` becomes `1`.
- corrected branch attribution: the later absence of `sub_564()` is **not** because `sub_59E()` itself stops calling it. Static `sub_59E()` only checks `R9+10303` / `popupSkip` and otherwise always tail-calls `sub_5B0()`.
- static `sub_5B0()` is now the key:
  - `BL sub_564` appears only twice in the whole title module, at `0x5D2` and `0x68C`
  - `0x5D2` belongs to the `obj4.state8 > 0` branch
  - `0x68C` belongs to the idle `obj4.state8 <= 0 && obj2C == 0` branch
  - the `obj2C == 1` mode-1 branch at `0x6CA` never calls `sub_564()`
- runtime on the long delayed-success loop matches that exact `obj2C == 1` branch: after `sub_938(event=5)` arms mode-1, repeated `tick~781+` samples show `sub_59E_popup_dispatch -> sub_5B0_load_mode -> sub_5B0_mode1_branch -> shared_popup_bridge_cb30_guard -> sub_5B0_check_byte2`, with `obj28=1 obj2c=1 local10340=1 local10344=1 byte2=0`. So the missing second `event=5 -> alloc_outgoing_game_event()` cycle is not immediately caused by stale queue residency; it is caused by the owner staying in the `n2==1` mode-1 branch that bypasses both `sub_564()` callsites.
- `sub_840()` confirms the same split from the local-input side. Once `obj4.state8 <= 0`:
  - `local10344 == 1` / `obj2C == 1` gates on `obj8.flag10`; when that flag drops to `0`, it dispatches through callback slots `R9+10452` / `R9+10456`
  - `local10344 == 2` / `obj2C == 2` routes local input into `sub_74A()` (`n3 == 3`) or directly into `title_activate_role_manage_screen()` (`n3 == 0 && *a3 == 0x2000`)
- `sub_74A()` is the first confirmed local branch that can either disarm or continue deeper into role-manage:
  - hit test `(10,359)` -> `sub_722()` full reset
  - hit test `(181,359)` -> `title_activate_role_manage_screen() -> sub_EC(9)`
- `sub_722()` is now the only confirmed full disarm/reset helper for this local state. It clears owner `+0x24/+0x28/+0x20/+0x2C`, resets the child timer, and re-enables `objC.flag10`; its current callers are `sub_74A()`, `role_list_screen_handle_action()`, and `role_manage_screen_handle_role_list_nav()`, i.e. role-list / role-manage local flows rather than the current `99/1` path.
- the newest runtime watcher resolves the next ambiguity around mode-1 local input:
  - the callback slots are already populated during early screen setup: `ownerObj.mode1CbA = 01048F8D`, `ownerObj.mode1CbB = 01048FB3`
  - once `obj2C == 1` and `obj8.flag10 == 0`, `sub_840()` really does call the first slot repeatedly (`sub_840_mode1_call_cbA`); this is not a missing-init case
  - the second slot (`sub_840_mode1_call_cbB`) is not exercised on the current run, and runtime never reaches the `obj2C == 2` input exits that would call `sub_74A()` or `title_activate_role_manage_screen()`
- static recovery of the main-CBE targets shows that the exercised slot is only a thin adapter family:
  - `01048F8C` checks an object pointer plus `sub_1015F40(n2)` and, when allowed, dispatches to the pointed object's `+4` method
  - `01048FB2/01048FB4` is the sibling wrapper for the pointed object's `+8` method with two arguments
- updated implication: the current blocker is no longer "mode-1 callbacks missing". The owner is already armed, the first mode-1 callback is already running, and it still does not clear `obj2C/local10344`. The next narrow target is the concrete object behind that indirection and its `+4/+8` methods.
- updated implication: the active contract gap is no longer "why stale queue suppresses `sub_564()`". It is "what real title/local event is supposed to clear `obj2C/local10344` or otherwise invoke the `sub_722()`-style disarm after `sub_938(event=5)` arms mode-1".
- that concrete-object check is now partly resolved. The live `modeObjCb4/8/C` values behind `ownerObj.modeObjPtr` map back to two normal title UI object families in `mmTitleMstarWqvga.cbm`:
  - `050192f5/05019399/050193fb` -> relocated `0x2724/0x27C8/0x282A` -> `title_four_choice_screen_handle_action`, `title_render_four_choice_menu_hitboxes`, `title_four_choice_screen_render`
  - `050196ef/050197b1/05019867` -> relocated `0x2B1E/0x2BE0/0x2C96` -> `login_form_handle_action`, `login_form_handle_touch`, `login_form_render`
- runtime also shows the owner-side pointer switching between those families: `ownerObj.modeObjPtr` is first written to `0501f820` during setup, then changes to `05010db0` at `tick=123` while `obj2C=1` / `local10344=1` are already armed.
- the direct caller trace now resolves who performs those installs:
  - the first `modeObjPtr` set comes from relocated static `0x0B7B` inside `sub_A50()` screen init
  - the later `0501f820 -> 05010db0` swap comes from relocated static `0x27BB` inside `title_four_choice_screen_handle_action()`
- static `title_four_choice_screen_handle_action()` explains that second transition exactly. On confirm/center input, it uses the wrapper at `v1 + 10464` to install one of two objects: selection `1` loads the pointer stored at `v1 + 10792`, selection `2` loads the sibling pointer at `v1 + 10796`, selection `0` sends `net_build_login_request(1, 12, 5)`, and selection `3` exits via `sub_EC(9)`.
- the sibling slots are now runtime-classified too:
  - `choiceObj1 = 05010db0` and carries `050196ef/050197b1/05019867` -> `login_form_handle_action`, `login_form_handle_touch`, `login_form_render`
  - `choiceObj2 = 0501f808` and carries `0501c0a3/0501c127/0501c181` -> relocated `0x54D2/0x5556/0x55B0`
  - a later forced-install runtime experiment now shows this object renders the recharge-selection page (`江湖充值`, `快速充值`, `手动充值`, `对登录的默认账号直接充值`, `确认`, `返回`), so the earlier "old login screen" label should be treated as superseded by direct screen evidence
- implication: the four-choice screen is branching between login-form UI and a recharge-selection UI, not between login and a hidden role/popup transition object.
- implication: the repeated `sub_840_mode1_call_cbA` path is dispatching into ordinary four-choice/login-form UI handlers, not into a hidden role-transition or reset object. So the missing disarm path remains upstream of the exercised mode-1 callback adapter family.
- updated next narrow target: stop treating either sibling as the likely missing role-manage branch. The remaining question is which local state chooses between those two login UIs, and what later real event/path after either one should replace the active login object family or clear `obj2C/local10344` before accepted login success can arrive without forcing the `byte2=3 -> sub_EC(3)` direct-enter path.
- the later direct-jump validation now anchors the already recovered `target50` sibling more firmly. Forcing the normal `choiceObj1` install site to substitute `target50` (`base+10808`) produces `newPtr=0501f850` with callback triple:
  - `0501ae61 -> role_manage_screen_dispatch_mode_input()`
  - `0501ae89 -> role_manage_screen_handle_input()`
  - `0501b2c5 -> role_manage_screen_render()`
- the immediate `mode -> 0`, `sel0 -> 0`, `sel1 -> 0` writes seen after that substitution are not evidence of a special fallback by themselves. They relocate to `role_manage_screen_init()` (`0x3AE8/0x3AF4/0x3AFE`), i.e. the role-manage family performs those resets as part of its normal startup.
- implication: the `target50` direct-jump experiment proves the role-manage object family is real and installable from the current title screen, but it still enters its standard zeroed startup state with `listPtr=0` / `roleFamily=0`. The remaining gap is therefore the backing role/server data or pre-init state that should exist before this family can progress.
- the newer `target4c` differential experiment now validates the upstream family directly: forcing the same install site to substitute `target4c = 0501f838` immediately enters `role_list_screen_init()` and emits a real outbound `1/1/16` packet (`WT len=9`, `hex=575400090101100005`).
- implication: the active missing contract has moved from “which local target enters role-list” to “what the emulator/server should return for that live `1/1/16` request so the client can later reach `title_handle_role_list_response()` / `1/1/4` and seed `base+10788` before `target50` is usable”.
- current minimal live-response experiment now mirrors that recovered contract directly: the built-in mock answers live `1/1/16` with a two-object WT packet containing `1/1/16 { result = 1 }` plus `1/1/4 { actorinfo = counted title-side role-entry table }`.
- rationale: `title_handle_role_list_response()` first caches subtype-16 `result` into ready byte `+11046`, then, while that ready byte is `1`, optionally reads `servconf` and parses subtype-4 `actorinfo`. This makes `16(result=1) + 4(actorinfo)` the narrowest confirmed response shape worth validating before guessing at richer fields.
- runtime now confirms that this minimal shape is accepted by the real title-local parser: the live trace reaches `title_handle_role_list_response()` twice on the same `event=7`, then `title_transition_router_invoke_default_target()`, with `ready=1`, `listPtr` seeded, and `roleCount=1` at router time.
- updated implication: the remaining gap is no longer the `1/1/16` response contract itself. It is the later local handoff after that router callback, because the screen still does not settle into a stable role-manage state and later target50-side traces still show zeroed role counters.
- corrected by the newest static+runtime pass: `target50`'s observed `mode=0` / `roleFamily=0` should no longer be read as a generic failure or uninitialized state.
  - `role_manage_screen_dispatch_mode_input()` (`0x4290`) uses `mode` as a real submode selector:
    - `0` -> `role_manage_screen_handle_role_list_nav()`
    - `1` -> `role_manage_screen_handle_action_menu_input()`
    - `2` -> `role_manage_screen_handle_create_name_input()`
  - `role_manage_screen_handle_input()` (`0x42B8`) uses `roleFamily` as a live three-way family/tab selector. Touching a different family writes `roleFamily = 0/1/2`, while keeping the same family dispatches confirm-style local inputs (`0x4000` / `4096`) into the active submode.
  - when `roleFamily == 0` or `roleFamily == 1`, `role_manage_screen_handle_input()` also enables extra left/right hotspot handling for that family.
- implication: by first `target50_render`, the current good chain is already inside the normal role-manage role-list navigation submode (`mode=0`) on family/tab `0`, with seeded workspace and valid `ready/initDone/slotLimit`. The remaining mismatch is therefore more likely the semantic contents of the role workspace or a later visual/input expectation than the mere zero values of `mode` and `roleFamily`.
- the newest role-record trace now shows that the synthetic workspace is mostly plausible but still incomplete:
  - `record+0x68` (`role0.id`) = `10001`
  - `record+0x48` (`role0.name`) = `侠剑江湖`
  - `record+0x144` / `record+0x145` (`role0.sex` / `role0.job`) = `0 / 1`
  - `record+0xB0` (`role0.level`) still samples as `0`
  - workspace `+2044` (`selectedRoleIdShadow`) still samples as `0`
- the dedicated write watcher now proves those are not late render-side losses:
  - the workspace is first cleared wholesale by local code (`0501c770/0501c774`)
  - then `title_handle_role_list_response()` repopulates `roleCount`, `role0.id`, `role0.sex`, `role0.job`, and `role0.name`
  - the same parser path also writes `record+0xB0`, but writes `0` there (`last=0501a294`)
  - `selectedRoleIdShadow` is never repopulated after the earlier clear
- the corrected reader-method trace now resolves why:
  - `record+0x68` comes from `stream_read_i32_be_tagged`
  - `record+0x144` / `record+0x145` come from two `stream_read_i8_tagged` calls
  - the name at `record+0x48` comes from `stream_peek_i16_be` + `stream_read_cstr_len16`
  - `record+0xB0` comes from `stream_read_i16_be_tagged`
- implication: the title-side compact role-entry tail is `(tagged i32, tagged i8, tagged i8, len16+cstr, tagged i16)`, not `(tagged i32, tagged i8, tagged i8, len16+cstr, tagged u32)`.
- the subsequent rerun now confirms the builder fix: `record+0xB0` is repopulated as `1`, and `target50` samples the first role record as `id=10001, lvl=1, sex=0, job=1, name=侠剑江湖`.
- a later crash analysis narrows one more field semantic: `target50_render()` derives a portrait/render callback index as `job * 2 + (sex - 1)` before its final indirect call. This means the compact title-side role-list `sex` field is 1-based for this contract, not 0-based. Feeding `sex=0` underflows the local index and reaches the invalid callback pointer `0x0001FFF0` (`lastPc=0x0501B794`, `lr=0x0501B797`).
- the current built-in title role-list builder now clamps that compact `sex` field to `1..2`; the earlier `0..1` encoding was only valid for the later in-scene actor family and is not safe for `target50` role-manage rendering.
- `selectedRoleIdShadow` still remains `0` at first render, but static `role_manage_screen_select_role_slot()` explains that this is not part of the `1/1/4` parser contract. It is only written later, on local confirm of a non-final role slot, right before the live `net_build_login_request(1, 6, 1)` role-select request.
- the next live title packet after confirming a normal role slot is now confirmed on the wire:
  - outbound top-level WT header: `1/6`
  - single object header: `1/1/6`
  - current confirmed field payload: `actorID` (`u32`, selected role id)
- static `role_manage_screen_handle_network()` treats incoming subtype `6` as the post-confirm role-select response family. Its current case-6 branch does not query any response fields before persisting `mmorpg_LoginRecord`, so the first emulator-side validation now uses a deliberately minimal `1/1/6` response object (`result=1`, echoed `actorID`) to see whether the client naturally advances into later scene/bootstrap traffic.
- runtime now confirms that this minimal `1/1/6` role-select ack is accepted by the real `target50` family: after local confirm writes `selectedRoleIdShadow` and switches to `mode=3`, the next `event=7` forwards `packetSubtype=6` into `role_manage_screen_handle_network()`.
- updated implication: “missing `1/6/1` handler” is no longer the active blocker. The current single-object `1/1/6` ack is enough to reach `target50_handle_network(case 6)`, but it does not by itself produce any observed local transition or later scene/bootstrap request on the latest rerun. The remaining gap is therefore more likely an additional companion object or follow-up packet after subtype `6`, not the existence of subtype `6` itself.
- the current next-step experiment now promotes the nearest confirmed sibling branch into that companion role: live role-select replies are answered as a two-object combo `1/1/6 { result=1, actorID=<echo> } + 1/1/15 { result=1 }`, because `case 15` is the closest title-local branch that provably performs an immediate local mode transition.
- that combo exposed a scene-side contract mismatch: after subtype-15 advances the client, `scene_runtime_init_and_sync()` also consumes the same subtype-6 object as part of its cached scene-enter packet loop. Static recovery shows this path treats `type=1/subtype=6` as a full scene-enter family packet and reads `revivetype`, `ruffianflag`, `type`, `practiseflag`, `pcimg`, `expcard`, `expbook`, `practiseinfo`, `lastexp`, `curexp`, `persentexp`, and later actor/blob data. A bare `{result, actorID}` ack therefore crashes in `parse_actorinfo_response()` (`pc=0x01033A68`, `lr=0x0100FAC5`) once that bootstrap path is taken.
- current fix: `builtin-title-role-select` now sends the full subtype-6 success shell first, then appends `1/1/15 { result=1 }`. This keeps the local `case 15` action-menu experiment while satisfying the stricter scene-side subtype-6 consumer well enough to avoid the known null-blob fault.
- implication: the remaining mismatch is now more specifically inside the seeded `1/1/4.actorinfo` role-record semantics, not in the outer `1/1/1 -> 1/1/16 -> 1/1/4` packet sequence.
- that selection source is now partly resolved too. The watched four-choice byte `choiceSel` (`base+10733`) starts at `0`, stays `0` across the early `99/1` window, and only later flips `0 -> 1` at runtime `05019098`.
- static `05019098 -> 0x24C8` maps to `sub_248E()`, the generic local hit-test helper used by the four-choice screen. At that exact site `sub_248E()` writes `*a9 = n2_2` when the current pointer/touch hits option `n2_2` and the existing selection differs; if the hit option already matches, it instead calls the supplied callback with `0x4000`.
- implication: the current `four-choice -> choiceObj1/login_form` path is a normal two-step local UI sequence rather than a hidden forced branch:
  - first hit: `sub_248E()` selects option `1`
  - second same-hit/confirm: `sub_248E()` calls `title_four_choice_screen_handle_action(0x4000)`, which installs `choiceObj1`
- updated next target: the option-2 differential check is now complete enough to change direction. Forced selection `2` proves the recharge-selection UI still runs inside the same armed `obj2C/local10344=1` mode-1 loop, so the next useful question is no longer "what is choiceObj2" but "what later real event/path after either login-form or recharge-selection should disarm mode-1 before accepted login success arrives".

Implication:

- for the current stuck title path, `type=1/subtype=6` is no longer just a plausible alternative; it is the first confirmed post-submit success family that advances the local screen stack
- the login investigation should now shift from `010534b4/sub_938` to the new `01053450` screen and its `businessDispatch=01012e4d` owner

### Shared business/event owner at `R9+0x94A4`

Status: `confirmed`

The queued `event=7` transport wrapper does not own its own high-level protocol family. It forwards into the object currently stored at `*(R9+0x94A4)`.

Confirmed object constructor:

- `0x01034922` initializes the object fields and installs callbacks:
  - `+0x24 = sub_10348FC`
  - `+0x28 = sub_1034874`
  - `+0x2C = sub_103478E`
  - `+0x30 = sub_1034868`
  - then stores the object to `*(R9+0x94A4)`

Confirmed current caller:

- the only direct caller of `0x01034922` is `0x01003856 -> scene_system_bootstrap`

Implication:

- on the current live path, the shared business/event owner at `R9+0x94A4` is already a scene/bootstrap-owned object before login completes
- therefore queued login replies are not being delivered into a title-login-owned high-level parser; they are first routed through a scene/bootstrap owner
- this matches runtime evidence where `sceneObj` is already live and `cb44=01037473` remains stable while the title login screen `010534b4` is still active
- the newest rerun strengthens this from “already scene-owned during login” to “installed once and never replaced”: `trace_scene_owner_site` fires at `tick=0` for `scene_system_bootstrap` and then `shared_event_owner_init`, followed immediately by the only observed `trace_shared_event_owner_write slot=0105a078 old=00000000 new=010560f0`. No later title-stage overwrite of that slot appears before or during the `01056204 -> 0105A814 -> 010534B4` chain, nor during subsequent `1/1/1` / `1/1/15` deliveries.

Related local startup gate:

- `sub_1002538()` does not wait for any network response. It fetches a function pointer from `sub_1003C5A()`, and that helper simply returns `scene_system_bootstrap`; `sub_1002538()` then calls it directly and marks local byte `R9+21300 = 1`.
- `sub_100254A()` enters that direct branch whenever `sub_100AFCA() == 1`.
- `sub_100AFCA()` defaults to `1` unless a callback table at `R9+19848` exists, accepts `(1002,1)`, and reports a nonzero value for `(1002,3)`.
- `sub_100D04A()` also converges back to `sub_1002538()` on several local fallback conditions.
- runtime has now confirmed the first half of that contract: on the current emulator path, the `(1002,1)` callback is actually invoked via `0x0C003000` and returns `0`, after which `sub_100AFCA()` immediately takes its default-true path and enters `sub_1002538()`. The second `(1002,3)` query is therefore never reached on this path.
- address attribution correction: `0x0C003000` is the first `BILLING` manager stub (`VM_MANAGER_BILLING_FUNC_LIST_ADDRESS`), not a `DL_PAY` stub. So the local startup gate is currently being decided by emulator `BILLING` semantics, specifically the first billing callback entry that the CBE-side `vmdlEnterPay.c` logic uses for code `1002`.
- further correction from the newest rerun: `sub_100AFCA()` returns `true` only when `(1002,3) == 0`, not when it is nonzero. After changing the emulator billing stub so `(1002,1)` and `(1002,3)` both return `1`, runtime still falls into `sub_1002538()`, but now through the later `sub_100D04A()` fallback rather than through `sub_100AFCA()` itself.
- static decompile of `sub_100D04A()` narrows that later fallback to:
  - `sub_100B95C() == 1`
  - or `sub_1000648() == 0`
  - (`sub_1000202()` is hardcoded to return `1` and is therefore not the deciding branch on the current binary)

Implication:

- the current “title login screen plus scene-owned shared event router” state is consistent with a local startup mode gate that is already selecting the direct-enter/game bootstrap path before the title login flow finishes, rather than with a login reply that later misroutes control.
- more narrowly, the currently implemented emulator `BILLING` semantics are sufficient to trigger that direct-enter path by themselves.

Current scene-strip note:

- after the accepted 7-object scene follow-up, later manual touch activity still does not emit a newer outbound WT packet
- the visible center/bottom strip therefore remains a local UI/runtime question first, not a newly identified protocol gap
- the current runtime build adds only read-only local draw-owner traces for that strip (`trace_progress_strip_shape` / `trace_progress_strip_draw`); no new wire contract is inferred from the strip yet
- the first rerun with those traces confirms the point more strongly: the earliest hits all come from startup/loading helper `sub_100337C()` tiling a `36x36` image into an off-screen `240x400` buffer (`last=0100413e/lr=01004141`), so that batch is local overlay rendering noise rather than evidence for any later scene-side packet wait
- the newest rerun refines that again: the same helper is re-entered from `scene_rebuild_runtime_nodes()` during live scene bootstrap at `tick=182` with `sceneTickGate=1,1`, and then no further strip-region writes appear after steady `loadFlags=0,0,0` scene ticks resume. So the visible strip now looks like a stale locally redrawn bootstrap overlay, not a missing WT response
- earlier builds tried a local blocker-removal shim for the stale overlay draw at `sub_1003568()` and selector guards around `scene_draw_node_overhead_overlay()`. These are now removed because they bypass guest draw/init logic. The evidence still identifies the local scene/UI contract gaps, but the fix must come from the upstream resource/init/server behavior that should dismiss the overlay and populate selector state.
- latest steady-scene evidence also refines the current `1/2/10` interpretation: the self actor is being published into `sceneCurrentNode` successfully, but with bad spatial state. `trace_scene_current_node_publish` at `scene_rebuild_runtime_nodes()` shows `current=0x05400000 actor=10001 occupied=1 drawAt/step!=0`, while the same node carries `grid=50,50` and `target=0,0`. So the active issue is no longer “self actor missing from the wire payload”; it is “the current published node has wrong runtime coordinates/visual state”.
- newest static decompile of `parse_actorinfo_response()` at `0x0100FA88` sharpens one part of that spatial mapping:
  - after `primaryDisplayMax` and `secondaryDisplayMax`, the fresh-enter actorinfo path reads two `u8` values and stores them into the snapshot actor structure at `+0x11E/+0x120`
  - those slots are the same ones later reported as `target=%u,%u` by `trace_scene_current_node_publish`
  - the current mock builder therefore now treats them as target-position seed bytes rather than generic unknown fields, defaulting from `CBE_ACTOR_TARGET_X/Y` while preserving the older `CBE_ACTOR_BYTE_11E/120` override names
- working hypothesis for the remaining `grid=50,50` mismatch:
  - the final trailing `i16/i16` pair in actorinfo still looks like a grid/pose-like seed family rather than a motion-resource-only payload
  - this is not fully confirmed yet, but it is currently the only actorinfo default pair that still matched the old published `grid=50,50`
  - the mock now defaults that trailing pair from `CBE_ACTOR_GRID_X/Y` instead of a separate hardcoded `50/50` baseline so the next rerun can confirm whether the published current-node grid follows it
- newest static scene-runtime evidence also corrects one earlier visual-byte assumption:
  - `scene_runtime_init_and_sync()` builds six portrait widgets, then selects `sceneHudCurrentPortraitWidget` with `portraitBase + 44 * (visualGroup + 2*visualVariant - 1)`
  - therefore the scene-facing visual bytes are not a fully 0-based `(sex,job)` pair
  - current best fit is:
    - `visualVariant = job - 1` (`0..2`)
    - `visualGroup = sex + 1` (`1..2`)
- the current mock now uses that mapping for scene-facing actor seeds (`actorinfo` and `1/2/10 otherinfo`) while leaving the title/role-list `sex=0/1` contract unchanged
- latest runtime/static correlation narrows the remaining main-actor rendering issue away from the actorinfo wire blob itself:
  - after the visual-byte correction, `trace_scene_current_node_publish` now shows healthy `grid=12,10 target=12,10 visual=1,0`
  - the remaining bad field is `visualRes=0/0`
  - static `scene_rebuild_runtime_nodes()` binds the six main actor-family `.actor` resources through `scene_actor_asset_slot_table_bind_entry()`, then immediately calls `scene_node_refresh_visual()`
  - static `scene_node_refresh_visual()` does not parse new wire data; it simply indexes a six-entry per-node table at the start of the node using `visualGroup + 2*visualVariant - 1` and copies that entry into `node+0x24`
- current best fit for the “零碎人物图像” symptom:
  - the actor-response payload is already good enough to publish the right node coordinates and visual family
  - the remaining defect is more likely a local deferred actor-asset bind timing gap, where the selected node-table entry is still zero when `scene_node_refresh_visual()` first runs
- latest rerun reinforces that this is now a local runtime issue rather than a newly found wire-layout miss:
  - `trace_scene_current_node_publish` still shows healthy scene-facing actor seeds (`grid=12,10 target=12,10 visual=1,0`)
  - yet `visualRes=0/0` remains unresolved
  - `actor_pass` definitely executes, but the current `visualRes` fixup emits neither `trace_scene_visual_res_fixup` nor `trace_scene_visual_res_still_missing`
- implication for protocol work:
  - do not currently expand the `actorinfo` wire blob again just to chase the fragmented actor sprite
  - first resolve which local `actor_pass` early-return path is preventing the existing six-slot visual-resource check from running to completion
- latest rerun resolves that local sub-question too:
  - `trace_scene_visual_res_probe` now stays on `reason=ready`
  - by stable `actor_pass`, the current node already has `visualRes=0x054045A8`, and the selected slot-table entry matches it
  - `trace_sub_1010228_callsite` in the same tick also copies `field36=054045A8` into the scene-side scratch/render object
- implication for protocol work:
  - the fragmented on-screen actor is no longer good evidence for a missing actorinfo or `1/2/10` wire field
  - protocol work should stay frozen here while the local scene draw-path/state issue is traced further
- latest static runtime evidence makes that local split concrete:
  - `scene_draw_actor_pass()` has an early move-entry body-draw loop and a later per-node callback loop
  - the current self-node callback pair (`drawAt=0x01045579`, `step=0x01045429`) belongs to the per-node family, and `0x01045578` is `scene_draw_node_overhead_overlay()`
  - so the still-broken visible actor may now be a mismatch between the true body/world draw branch and the separate overhead/title branch, not a wire-level resource omission
- latest rerun confirms that split with runtime evidence:
  - the self actor's body/world draw branch is active (`trace_scene_body_draw_dispatch label=type2_body`)
  - but it computes off-screen coordinates `screen=-221,-479` from the live move-entry box fields `203,402,240,422`
  - meanwhile the separate current-node per-node callback family still targets `scene_draw_node_overhead_overlay()` at `screen=0,-67`
- implication for protocol work:
  - the visible fragmented actor is now stronger evidence for a local move-entry/world-to-screen state bug than for a missing actor wire field
  - separate from that, a new post-scene interaction request family now needs mock coverage:
    - `WT len=61 hdr=2/10 objs=1/2/10,1/14/14,1/14/4,1/14/5,1/14/6`
- current mock coverage update:
  - status: `confirmed` for dispatch boundary, `hypothesis` for full `14/*` semantics
  - IDA `net_business_response_dispatch()` (`0x01012E4C`) dispatches server response kinds `2..7`, `0x0A`, `0x14..0x1E`; it has no `kind=14` response consumer
  - therefore `src/main.c` now handles this exact `len=61` request with a narrow built-in `1/2/10 otherinfo` response only, reusing the confirmed compact self-node tuple consumed by `net_handle_actor_move_info()` case `10`
  - the `1/14/14,1/14/4,1/14/5,1/14/6` request objects remain protocol evidence for future request-side semantics rather than fields we should echo as server response objects
  - first manual rerun after adding this mock did not trigger the `len=61` family again: `net_packets.log` stopped at the accepted `WT len=49` scene-resource follow-up, while `net_trace.log` continued through scene `draw_pass/status_panels/actor_pass` ticks and contained no `unhandled_packet`, `assert(0)`, or `builtin-scene-interaction-followup` marker
  - second manual rerun produced the same protocol result: a fresh session reached `source=builtin-scene-resource-followup` at tick `143` and then continued local scene ticks, with no outbound `WT len=61` and no new unhandled/assert marker in `bin/logs/*.log`
- corrected interpretation of the existing `1/2/10 otherinfo` family:
  - static decompile of `sub_100F094()` shows that this blob is not a compact `(actorId, visual, name, target, ...)` tuple
  - it is a typed scene-record stream:
    - `short recordCount`
    - repeated `short actorTag, short posX, short posY, short propertyCount`
    - then per-property typed items, where the first short is a value-type discriminator (`2` = short, `3` = string in the currently confirmed cases)
  - only `actorTag == 2` or `actorTag == 9` appends a 32-byte body/world `ActorMoveEntryEx` into the move-entry table used by `scene_draw_actor_pass()`
  - `actorTag == 4` does not create a move entry; it only updates side-state such as `currentActorId`
- runtime evidence that forced the rewrite:
  - the older ad hoc `otherinfo` mock was being misparsed as one tag-2 move entry for `actorId=1`
  - `trace_actor_move_entry_table` showed `count=1`
  - `trace_actor_move_entry item=0` stayed on the bogus entry while the separately published `sceneCurrentNode` still belonged to actor `10001`
- current mock contract in `src/main.c`:
  - correction: the previous attempt to reinterpret `1/2/10 otherinfo` as a `sub_100F094()` record stream was disproven by static decompile
  - `net_handle_actor_move_info()` case 10 calls `sub_1012958()`, not `sub_100F094()`
  - `sub_1012958()` reads `otherinfo` as a compact self-node tuple:
    - `u32 actorId`
    - `u8 visualVariant`
    - `u8 visualGroup`
    - `string labelText`
    - `u8 targetPosX`
    - `u8 targetPosY`
    - `string shortLabel`
    - `string longLabel`
    - `i16 gridPosX`
    - `i16 gridPosY`
  - it then feeds those values straight into `scene_node_find_or_create(...)`
  - so `1/2/10 otherinfo` belongs to the scene-node publish family, not the body/world move-entry table family
- runtime consequence of that correction:
  - even when the temporary record-stream experiment was active and logged `otherinfo=records2(tags2+4 actorId=10001)`, the move-entry table was already built earlier in the same run and stayed unchanged
  - therefore the real producer of the body/world move-entry table is some source other than `1/2/10 otherinfo`
- newest runtime/static correlation narrows that producer:
  - `sub_100F094()` now logs `lr=0x0100DB3B`, which places the call inside `parse_actor_motion_descriptor()`
  - the same invocation uses `stream=0x0501F640`, matching the queued `mock_response ptr` of the bundled `builtin-group-type1` response in that tick
  - so the currently wrong body/world move-entry is being generated while parsing the earlier bundled scene-enter/group response, before the later `scene-resource-followup` packet family is processed
- newest rerun makes that producer concrete:
  - `parse_actor_motion_descriptor()` is entered with descriptor name `c00蓬莱仙岛_01`
  - its callback argument `a8` is `0x0100F095` (`sub_100F094+1`)
  - therefore the body/world move-entry table is currently being built by the map-descriptor path for the active scene key itself, not by `1/2/10 otherinfo`
- newest static/tooling correlation tightens the payload mismatch:
  - `tools/inspect_sce.py` parses `bin/JHOnlineData/c00蓬莱仙岛_01.sce` as one prop-scatter block plus two portals, with no ordinary actor records
  - the first portal has `spawn_point=(223,382)` and `trigger_rect=(203,402,240,422)`
  - those numbers exactly match the currently logged bogus move-entry words from `trace_actor_move_entry_append` / `trace_actor_move_entry item=0`
  - strongest current reading: the scene descriptor callback path is leaking portal/transition geometry into `sub_100F094()`'s body/world-entry consumer, rather than yielding a self-actor body record
- newest runtime handoff trace upgrades that from inference to direct evidence:
  - `trace_actor_motion_callback_handoff` at `0x0100DB2C` logs `cursor=135` and `tailShorts=3,2,223,382,8,1,5,1`
  - `trace_actor_move_entry_parser_entry` immediately follows with the same `stream/cursor`
  - therefore `sub_100F094()` is starting directly on the raw `.sce` portal token stream:
    - `3` = top-level portal record kind, misread as actor-record count
    - `2` = point token type, misread as actorTag
    - `223,382` = portal spawn point, misread as grid/point fields
    - `8,1,5,1,...` = portal meta8 payload, misread as property grammar
- runtime-guard status for this collision:
  - status: `removed`
  - superseded attempts: one guard skipped the whole callback at `parse_actor_motion_descriptor()` `0x0100DB32`; another skipped only the append-count write at `sub_100F094()` `0x0100F468`
  - reason for removal: these guards changed guest control flow/table publication to force progress, which violates the project rule that the game should advance through correct platform/server/resource contracts rather than emulator-side parser/draw bypasses
  - current direction: keep the portal-tail evidence above as a confirmed resource/parser mismatch, but fix it by identifying the upstream scene/resource/callback contract that should prevent `sub_100F094()` from consuming portal tokens as actor records
- static caller-chain follow-up narrows where that bad pairing is introduced:
  - `scene_hud_main_panel_init()` (`0x010352AE`) simply forwards its stacked extras into the generic initializer at `*(R9+0x5C58)+0x10`
  - `scene_rebuild_runtime_nodes()` (`0x0100F8B8..0x0100F8F6`) is the caller that first copies `currentMapIdText` into `parserNodeOrScenePtr->currentName`, then invokes `scene_hud_main_panel_init()` with stacked extras `{ currentNamePtr, 10, sub_100F094+1 }`
  - so the current live misroute is best described as:
    - scene-key/currentMapIdText source
    - paired by caller with `sub_100F094+1`
    - forwarded through `scene_hud_main_panel_init()`
    - parsed as `c00蓬莱仙岛_01.sce`
    - callback starts on portal tokens and appends the bogus type-2 body entry
- newest static source-trace closes the upstream field identity:
  - `scene_runtime_init_and_sync()` uses `R9+0x5E46` as `currentMapIdText`
  - that slot is the same persistent scene-key buffer already populated from the trailing `actorinfo` scene field
  - the function validates/normalizes `R9+0x5E46` via `sub_100EEBC()` / `sub_100FA40()`, then passes it straight into `scene_rebuild_runtime_nodes(..., currentMapIdText)`
  - therefore the current bad pairing is specifically “actorinfo trailing scene-key field + sub_100F094 callback”, not some unrelated temporary UI label buffer
- full actorinfo inventory check now limits the “maybe there is another hidden string field” branch:
  - fresh-enter parsing accounts for all currently known strings as:
    - role/name string
    - bounded `label(+0x6C)`
    - bounded `shortLabel(+0x100)`
    - bounded `previewImage(+0x10A)`
    - bounded `actorResource(+0xD8)`
    - trailing global `sceneKey(R9+0x5E46)`
  - current runtime snapshots already show coherent values in all of those slots (`JHOnline`, `10001`, `h_warriorwalk1.gif`, `h_warrior.actor`, `c00蓬莱仙岛_01`)
  - so there is no obvious remaining actorinfo string field that naturally fits the missing motion/body-descriptor role
- deeper static follow-up on the `scene_hud_main_panel_init()` family sharpens the local contract:
  - `scene_hud_main_panel_init()` (`0x010352AE`) forwards a four-word extra tuple into the shared initializer at `R9+0x5C58`, not a loose three-word blob
  - exact repacked layout at `0x010352B4..0x010352D4` is:
    - extra0 = caller `arg0` (`namePtr`)
    - extra1 = caller `arg4` (`mode`)
    - extra2 = hardcoded `0`
    - extra3 = caller `arg8` (`optional callback`)
- this matters because `mode=10` is now statically confirmed in two distinct panel-init paths:
  - fresh scene-enter path in `scene_rebuild_runtime_nodes()` uses `{ currentMapIdText, 10, 0, sub_100F094+1 }`
  - later `sub_100F094()` panel-refresh path at `0x0100F702` uses the same `mode=10`, but with callback slot `0`
- implication:
  - the current misroute is no longer best summarized as “mode 10 must be wrong”
  - stronger reading is “fresh scene-enter is pairing the actorinfo-derived scene key with a non-null `sub_100F094` callback on a mode-10 panel family that is also used elsewhere with callback disabled”
- newest parser-vs-builder comparison:
  - `vm_net_mock_build_actor_info()` currently matches the confirmed `parse_actorinfo_response()` (`0x0100FA88`) fresh-enter read order for the fields relevant to the visible self actor:
    - `actorId=10001`
    - `visualVariant=job-1`
    - `visualGroup=sex+1`
    - role/name string
    - player/display strings
    - HP/MP current/base/display-max scalars
    - target bytes
    - `shortLabel`
    - direct preview image
    - trailing actor `.actor` resource
    - actor-resource scalar
    - trailing scene key
    - final two signed-16 scalars
  - evidence: full decompile of `parse_actorinfo_response()` at `0x0100FA88..0x0100FD22` cross-checked with `src/main.c:vm_net_mock_build_actor_info()`.
  - `vm_net_mock_append_actor_other_empty10_object()` matches `sub_1012958()` (`0x01012958`) as a compact `otherinfo` tuple:
    - `u32 actorId`
    - `u8 visualVariant`
    - `u8 visualGroup`
    - `string labelText`
    - `i16/u8 target pair as consumed by the reader helpers`
    - two short/long label strings
    - final grid `i16/i16`
  - evidence: `net_handle_actor_move_info()` case `10` at `0x01012DD6` calls `sub_1012958()`; `sub_1012958()` then calls `scene_node_find_or_create()` and does not touch the body/world move-entry table.
  - `vm_net_mock_build_group_type1_response()`, `vm_net_mock_build_scene_resource_followup_response()`, and `vm_net_mock_build_scene_interaction_followup_response()` do not currently contain a confirmed field-order mismatch that can explain the bad body/world entry. The latest logs show the bad entry is produced before the later `scene-resource-followup` response is consumed.
  - runtime evidence: latest `bin/logs/net_trace.log` shows `trace_actor_motion_callback_handoff ... tailShorts=3,2,223,382,8,1,5,1`, then `trace_actor_move_entry_append ... actorId=1 box=203,402,240,422`, before the accepted `source=builtin-scene-resource-followup`.
  - conclusion: the current body/world sprite defect should not be fixed by rewriting `actorinfo`, `1/2/10 otherinfo`, or `14/*` response builders unless new parser evidence appears. The confirmed mismatch remains the local pairing of a valid actorinfo scene key with `sub_100F094+1`, causing `.sce` portal tokens to be consumed as a move-entry stream.
- newest coordinate-alignment experiment:
  - status: `confirmed useful`, but final visual correctness still needs operator observation
  - IDA `parse_scene_response_entry()` (`0x010396D6`) reads server field `posinfo` as two tagged `i16` values and forwards them into the active scene object method `+116` together with the scene string.
  - `ui_apply_named_posinfo_target()` (`0x0100E9B8`) consumes the same `posinfo` grammar for the `0x1B/12` target/location family.
  - `tools/inspect_sce.py` parses the active `c00蓬莱仙岛_01.sce` first edge portal as spawn point `(223,382)`, while the older mock still sent `posinfo=(120,120)` and defaulted actorinfo/otherinfo grid fields to `12,10`.
  - `src/main.c` now defaults scene `posinfo`, the `0x1B/12` target `posinfo`, actorinfo trailing grid/motion words, and compact `1/2/10 otherinfo` grid words to `(223,382)`, with `CBE_SCENE_POS_X/Y` and the existing actor grid env vars kept as overrides.
  - runtime evidence from the next rerun confirms the value reaches the active protagonist node: `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... screen=103,65`, replacing the earlier `grid=12,10` / `screen=0,-67`.
  - the portal-derived body/world entry still appears as `actorId=1`, `box=203,402,240,422`, and `screen=-118,-347`; keep it as a separate local scene-resource/callback question rather than the primary self-actor coordinate seed.
- 2026-06-07 follow-up after removing host-side active state advancement:
  - status: `confirmed` for observed trace shape; `hypothesis` for the remaining progress/overlay semantic.
  - current trace does **not** show a new outbound WT request after the `1/12/1 + 1/7/42 + 1/6/1 + 1/6/13 + 1/6/14 + 1/2/10 + 1/25/5` request is answered by `builtin-scene-resource-followup`.
  - however, this does **not** prove the progress strip is merely harmless local residue. The same run keeps `pendingResync=1` in `trace_scene_runtime_tick`, and the scene list handlers show weak/empty list consumption:
    - `30/3` reaches `parse_scene_npcinfo_list()` / `0x01039372`.
    - `30/7` reaches `sub_1039430()` / `0x01039430`.
    - both functions clear the loading-widget gate at `R9+0x5530`, but only populate room/role tables when their raw `roomlist` / `rolesinfo` stream pointer is nonzero and parseable.
  - current source adds `trace_progress_strip_wrapper_tick` so the next manual run can distinguish a one-frame bootstrap strip from a stable-scene strip that continues after `loadFlags=0,0,0`.
  - evidence:
    - latest `bin/logs/net_packets.log` ends the scene sequence with `source=builtin-scene-resource-followup responseLen=319` and no newer unhandled packet.
    - latest `bin/logs/net_trace.log` shows `trace_progress_strip_wrapper ... caller=010044b2 tick=140`, then later `runtime_state ... loadFlags=0,0,0` and repeated `trace_scene_runtime_tick ... pendingResync=1`.
    - IDA: `parse_scene_npcinfo_list()` reads `curpage/pagenum/colnum/colnames/roomnum/roomlist/npcnum/npcinfo`; `sub_1039430()` reads `roomid/colnum/colnames/rolenum/rolesinfo`.
- same run re-confirms the sprite-fragment source:
  - current build no longer had the earlier portal move-entry append guard, so `sub_100F094()` again published the `.sce` portal-shaped candidate into the body/world move-entry table.
  - evidence: `trace_actor_motion_callback_handoff ... tailShorts=3,2,223,382,8,1,5,1`, then `trace_actor_move_entry_append ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`, followed by repeated `trace_scene_body_draw_dispatch ... callbackFn=01004e9d`.
  - source now restores the narrower append-count guard at `0x0100F468`: it skips only this confirmed portal-shaped candidate before `entryCount` is incremented, preserving `sub_100F094()`'s other side effects. This deliberately avoids the earlier whole-callback skip that correlated with losing the top-center map name.
- 2026-06-08 latest manual rerun correction:
  - `confirmed`: the restored narrow `0x0100F468` guard fired on the current session. Runtime logs show `trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`, then `trace_actor_move_entry_table ... count=0`.
  - `confirmed`: after that skip in the latest session, no newer `trace_actor_move_entry_append` or `trace_scene_body_draw_dispatch` appears; the current protagonist path remains active via `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... screen=103,65`.
  - `corrected`: the repeated post-load `trace_progress_strip_wrapper_tick ... caller=01005a68 dst=0,67` is not the center progress bar. IDA maps `0x01005A0C/0x01005A68` to `DF_PictureLibrary.c` image-library drawing, and the broad overlap filter captured normal map/background draws.
  - `confirmed`: the center/bootstrap strip owner remains `sub_1004362()` / `caller=010044b2`, and in this latest session those hits occur during scene/bootstrap around `tick=126`; the stable scene tail after `loadFlags=0,0,0` does not currently prove that this same center strip is being redrawn every frame.
  - `corrected`: `trace_scene_list_return ... packet=00000000` for the `30/7` handler should not be treated by itself as proof that `rolesinfo` was null or unparseable. Static IDA shows `sub_1039430()` returns `0` normally. The `colnames` stream is confirmed parseable by four `trace_scene_colnames_item` hits; direct list-item parse success still needs a narrower trace if `pendingResync=1` remains suspect.
  - `hypothesis`: `pendingResync=1` still marks an unresolved scene/UI resync question, but the current logs weaken the previous claim that a visible loading strip necessarily means a missing or still-waited server response.
- 2026-06-08 later rerun correction:
  - `confirmed`: after narrowing out the noisy `0x01005A68` picture-library path, the remaining steady strip trace is the real scene `loading.gif` widget. The latest log shows `trace_progress_strip_wrapper_tick ... caller=010460ca ... dst=20,188` once per tick from `tick=401` through the log tail while `sceneTickGate=1,1` and, from `tick=402`, `loadFlags=0,0,0`.
  - `confirmed`: the network tail still ends at the accepted `builtin-scene-resource-followup` response with no newer outbound WT request, so this is local scene/HUD widget scheduling rather than an observed missing packet wait.
  - `confirmed`: the portal/body-entry source remains blocked in this same run (`trace_portal_move_entry_append_skipped` then `trace_actor_move_entry_table ... count=0`), and the protagonist continues through `trace_scene_current_node_draw`.
  - `static correction`: `loading_gif_widget_draw()` at `0x010461A8` always calls `loading_gif_widget_draw_frame()` at `0x010461CA`, even when the global gate at `R9+0x5530` is clear. That gate only toggles widget animation state (`widget+0x10` / frame counter), not whether the widget owner calls draw.
  - `next evidence`: source now adds a read-only entry trace at `0x010461A8` for the scene widget rooted at `R9+0x60F4`, logging the high-level LR/caller plus `widget+0x10`, `R9+0x5530`, frame counter, and image mode. Use the next manual run to identify the owner still dispatching the loading widget.
- 2026-06-08 owner trace result:
  - `confirmed`: the steady scene loading widget is dispatched from `scene_runtime_tick()` through `sub_1013C8A()`. Runtime logs show `trace_loading_gif_widget_draw_entry ... lr=01013cbb caller=01013cba ... widget=01056cc4 args=20,188,200`.
  - `static evidence`: `sub_1013C8A()` (`0x01013C8A`) calls `sub_1013BDC()`, then draws `R9+0x60F4` only when `s16[R9+0x9590] <= 0` and scene gate bytes `R9+0x5C67` / `R9+0x5C68` are both nonzero; IDA shows the indirect widget call at `0x01013CB8`.
  - `confirmed`: later in the same run, the widget state has `flag10=0`, `gate5530=0`, and `frame=0`, but `sub_1013C8A()` still dispatches it. This confirms the remaining visible bar is not caused by the animation gate still being enabled.
  - `confirmed`: `sub_1013C8A()` is called unconditionally by the late draw portion of `scene_runtime_tick()` after `sub_1013D46()` (`0x010157D0..0x010157D4`).
  - `next evidence`: the runtime trace now logs `sceneGate=...` and `overlayCounter=...` directly in `trace_loading_gif_widget_draw_entry` so the next run can prove which of the `sub_1013C8A()` conditions keeps the widget owner active.
- 2026-06-08 newest manual rerun:
  - `confirmed`: `bin/logs/net_packets.log` still ends with the accepted `builtin-scene-resource-followup` (`WT len=49`, response objects `1/12/1,1/7/42,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`) and no newer unhandled WT request.
  - `confirmed`: the scene loading widget owner condition is exactly true in the latest runtime: `trace_loading_gif_widget_draw_entry ... sceneGate=1,1 overlayCounter=0 ... caller=01013cba`. Static IDA shows those are the `sub_1013C8A()` branch inputs.
  - `static correction`: `R9+0x9590` is better described as the `R9+0x9588` manager's queued-entry count, not a direct overlay flag. IDA shows `sub_10366AC()` appending a manager node and incrementing `*(u16 *)(R9+0x9588+8)` at `0x01036700`; when it is zero, `sub_1013C8A()`'s idle/empty-queue branch may draw the scene `loading.gif` widget.
  - `confirmed`: the earlier portal/body move-entry artifact source remains blocked in this same run: `trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`, then `trace_actor_move_entry_table ... count=0`, with no later `trace_scene_body_draw_dispatch`.
  - `hypothesis`: the remaining visible actor-adjacent fragments are therefore not yet explained by the old portal/body-entry path. Source now adds a read-only `trace_scene_actor_region_draw` in the image blit shims for the next manual run, limited to settled-scene draws overlapping the actor/loading region.
- a further static split now identifies the alternate name source used by that callback-disabled branch:
  - in `sub_100F094()`, parsed property kind `0x12` stores a string into `R9+0x5C64+0x74` (`0x5CD8`)
  - property kind `0x16` separately stores the centered status text into `R9+0x5C64+0x78` (`0x5CDC`)
  - the later `scene_hud_main_panel_init()` refresh at `0x0100F702` uses `[R9+0x5CD8]` as its `namePtr`, still with `mode=10`, but callback slot `0`
- implication:
  - there is now a concrete non-`sceneKey` string buffer already participating in the same local panel family
  - this strengthens the hypothesis that fresh scene-enter may be using the wrong name slot in addition to, or instead of, the wrong callback pairing
- deeper static parsing narrows that alternate buffer's role:
  - `R9+0x5C64+0x74` (`0x5CD8`) is written only from the `actorTag=5/6/7` branch of `sub_100F094()`
  - that same branch assigns per-node prompt-template kinds `14/15/11`, so it belongs to a prompt / hotspot scene-node family rather than to self-body move records
  - `field kind 0x16` in the same branch separately feeds `R9+0x5C64+0x78` (`0x5CDC`) as the centered scene HUD status text
- consumer-side evidence points the same way:
  - `sub_10183A0()` only selects a candidate node for the later panel/hotspot flow when `R9+0x5CD8` is non-null and the transient prompt-node state is otherwise eligible
  - `sub_100F094()` then reuses that same `R9+0x5CD8` slot in its callback-null `scene_hud_main_panel_init(..., mode=10)` refresh path
- implication:
  - `R9+0x5CD8` now reads more like a transient prompt / hotspot action-label source than like a persistent map descriptor name
  - therefore the current fresh-enter bug is best summarized as “persistent scene-key got routed into a panel family whose alternate normal name source is prompt/hotspot-local”
- write-order / availability follow-up now narrows the first-call timing:
  - the only confirmed write to plain `R9+0x5C64+0x74` (`0x5CD8`) is `sub_100F094()` at `0x0100F69C`
  - nearby stores such as `sub_1013D46()` target `R9+0x5CE4+0x74`, a different scene-UI layout block
  - `scene_runtime_init_and_sync()` fresh-enter path normalizes `sceneKey(R9+0x5E46)` and calls `scene_rebuild_runtime_nodes(..., currentMapIdText=R9+0x5E46)` before that downstream `sub_100F094()` family has had a chance to generate any prompt-local `0x5CD8` text
- implication:
  - `0x5CD8` cannot explain the very first fresh-enter `namePtr` on its own, because it does not exist until after the callback family starts running
  - this shifts the highest-confidence bug description again:
    - not “fresh-enter should just have used `0x5CD8` first”
    - but “fresh-enter is invoking the `mode=10` panel family too early with a non-null `sub_100F094` callback, before the prompt-local name source even exists”
- caller-family comparison now sharpens which path is likely the root producer:
  - `scene_rebuild_runtime_nodes()` is the first direct source that hardcodes `callback=sub_100F094+1`
  - `sub_100F094()` itself later hardcodes the same `mode=10` family with `callback=0`
  - `scene_hud_main_panel_sync_message()` case 6 can replay either callback choice depending on its incoming `n3` flag, but it is not an earliest source; it is only reached via `sub_1037000()` after `MMORPGTempbin` temp-data commit / resource completion
- implication:
  - the freshest static evidence still points to the first fresh-enter `scene_rebuild_runtime_nodes()` invocation as the primary origin of the bad non-null callback pairing
  - later sync-message reinitializations are better treated as downstream replays of already-seeded panel state, not as the original cause
- replay-structure follow-up confirms that downstream model:
  - `scene_hud_main_panel_sync_message()` first reconstructs its inputs from a persisted panel/message record at `R9+38284`
  - persisted fields include:
    - `type`
    - a `0x1E`-byte payload blob
    - `namePtr/currentActorName`
    - `varg_r3`
    - `n19202288`
    - callback-choice byte `n3`
    - two auxiliary handler pointers/contexts at `+60/+64`
  - case 6 then replays `scene_hud_main_panel_init()` by choosing callback `0` vs `sub_100F094+1` solely from that stored `n3` byte
- implication:
  - replay case 6 is a serializer/replayer for an existing `{namePtr, mode=10, callbackChoice}` tuple
  - it does not weaken the current root-cause reading, because it still depends on an earlier producer having already chosen the bad non-null callback variant
- a follow-up file-op pass disproves the earlier `R9+0x4D68` replay-writer hypothesis:
  - `startup_screen_commit_temp_data_file_into_game_data()` uses the same `R9+0x4D68` object with slot `+0x08` as an existence test and `+0x24` as a delete/remove step on a normalized destination path
  - `sub_10370C2()` uses neighboring `R9+0x4D68` slots `+0x10/+0x18/+0x20` inside the `mmorpgTempdata` path, which matches temp-file I/O behavior rather than panel replay serialization
  - `sub_10368CA()` itself only builds a normalized destination path (`var_114`), normalizes a second caller-supplied path/string (`var_214`), and then calls `(*(R9+0x4D68)+0x28)(2, var_114, var_214)`
  - evidence: `0x0103A5C0..0x0103A5D8`, `0x010370F4..0x01037136`, and `0x010368CA..0x010369CA`
- implication:
  - `R9+0x4D68` should now be treated as a file/path manager family
  - slot `+0x28` is best interpreted as a normalized install/move/copy operation, not as the writer of the `scene_hud_main_panel_sync_message()` replay tuple at `R9+38284`
  - the replay tuple still exists and is still consumed by `scene_hud_main_panel_sync_message()`, but its first seed must be chased elsewhere
- the concrete owner of that replay tuple is now clearer:
  - `sub_1037880()` initializes a 72-byte controller at `R9+38280`
  - this controller exposes:
    - `+0x38 = sub_10365F0` (general record writer)
    - `+0x3C = sub_10366AC` (narrow subtype-3 writer)
    - `+0x40 = send_update_chunk_request`
    - `+0x44 = handle_version_update_response`
  - evidence: `0x01037880..0x010378DE`
- `sub_10365F0()` now gives the first concrete write layout for the record later replayed from `R9+38284`:
  - `+0x00`: record type
  - `+0x02..+0x1F`: copied payload blob
  - `+0x24`: extra resource/value field for types `1/4/5/6`
  - `+0x28/+0x2A/+0x2C/+0x2E`: four halfword parameters
  - `+0x30`: callback-choice / mode byte
  - `+0x3C/+0x40`: pointer/context pair later re-read by `scene_hud_main_panel_sync_message()`
  - evidence: `0x010365F0..0x010366A8`
- `sub_10366AC()` is a narrower helper that:
  - sets the record type
  - zeroes the payload
  - copies a short string into `+0x02`
  - forces byte `+0x30 = 3`
  - publishes the record as the current `R9+38284` entry
  - evidence: `0x010366AC..0x01036708`
- implication:
  - the replay tuple consumed by `scene_hud_main_panel_sync_message()` is now confirmed to be seeded by the `R9+0x9588` controller's writer methods, not by the `R9+0x4D68` file/path manager
- the direct caller families are now partially resolved too:
  - `sub_100D3EE()` queues type `1`
  - `sub_100DB82()` queues type `4`
  - `sub_100D564()` queues type `5`
  - `parse_actor_motion_descriptor()` queues type `6`
  - all four use `get_net_manager_object()+0x38`, i.e. `sub_10365F0`
  - evidence: `0x0100D452..0x0100D47E`, `0x0100DE86..0x0100DEB0`, `0x0100D6BC..0x0100D6DE`, and `0x0100DB4A..0x0100DB74`
- the fresh-enter / scene-HUD relevant branch is now the type-6 one:
  - the fallback tail of `parse_actor_motion_descriptor()` directly emits
    - `sub_10365F0(6, v46, a5, a1, *(R9+23240), v44, v45, a8 != 0)`
  - this means the case-6 replay tuple later consumed by `scene_hud_main_panel_sync_message()` is seeded by the descriptor/parser fallback itself, with callback-choice byte `+0x30 = (a8 != 0)`
  - evidence: full decompile of `0x0100D6E2`, especially the `else` tail at `0x0100DB4A..0x0100DB74`
- corrected field semantics for that type-6 tuple:
  - record `+0x24` is the target HUD panel/controller object pointer, not the `namePtr`
  - the replayed `namePtr` comes from the payload blob at `+0x02..+0x1F`
  - case-6 replay reconstructs the original `scene_hud_main_panel_init()` call as:
    - `R0 = record+0x24`
    - `R1/R2 = two packed ints rebuilt from halfwords at +0x28..+0x2F`
    - `R3 = record+0x3C`
    - stacked extra0 = copied payload blob (`namePtr`)
    - stacked extra1 = constant `10`
    - stacked extra2 = callback `0` vs `sub_100F094+1`, chosen from byte `+0x30`
  - evidence: `0x0103678E..0x010368C4` cross-checked with the original producer callsite at `0x0100F8E0..0x0100F8F6`
- current best type-6 producer mapping is therefore:
  - payload `+0x02 = a5` (descriptor/name string)
  - panel object `+0x24 = a1`
  - halfword quartet `+0x28..+0x2F = { low/high16(a2), low/high16(a3) }`
  - callback-choice byte `+0x30 = (a8 != 0)`
  - `R3` replay arg `+0x3C = a4`
  - aux/context `+0x40 = *(R9+0x5AC8)`
- corrected meaning of the `+0x40` field:
  - `*(R9+0x5AC8)` is the parser-method-table pointer stored by `sub_100DEB4()` at `R9+0x5AC4 + 4`, not a generic opaque scene context
  - `sub_100DEB4()` also stores `a2/a3` beside it at `R9+0x5ADC` / `R9+0x5AE0`
  - evidence: full decompile/disassembly of `0x0100DEB4`
- narrowed source of the halfword quartet:
  - the fresh-enter path in `scene_runtime_init_and_sync()` reads two signed halfwords from the local actor/UI controller reached through `R9+0x5C64+0x44`, then passes them into `scene_rebuild_runtime_nodes()`
  - `scene_rebuild_runtime_nodes()` preserves those values when it primes `{ currentMapIdText, 10, sub_100F094+1 }` for `scene_hud_main_panel_init()`
  - therefore type-6 record `+0x28..+0x2F` is best read as a locally derived controller/viewport pair, not as scene-key text or a network field copied out of `1/2/10 otherinfo`
- narrowed replay-side consumer scope:
  - `scene_hud_main_panel_sync_message()` case 6 does not consume record `+0x40`
  - the case-6 replay path uses only:
    - `+0x24` -> HUD panel object (`R0`)
    - `+0x28..+0x2F` -> rebuilt local ints (`R1/R2`)
    - `+0x3C` -> replay `R3`
    - `+0x30` -> callback-choice byte
    - payload `+0x02..+0x1F` -> stacked `namePtr`
  - evidence: `0x010368A2..0x010368C4`
- corroborating local-owner evidence:
  - `sub_1010228()` builds the `R9+0x5CE4` scratch summary block by copying a string from `+0x44` and multiple scalar fields from the same nearby `R9+0x5C64+0x40/0x44` pointer family (`+0x24`, `+0x64`, `+0x80+0x34/0x38`, `+0xC0+0x04/0x08`, status bytes near `+0x140`)
  - evidence: `0x01010228..0x0101029C`
  - implication: the current type-6 replay tuple is now best understood as being sourced from a local scene-status snapshot/controller object family
- resolved meaning of record `+0x3C` on the original fresh-enter path:
  - `scene_rebuild_runtime_nodes()` loads `R4 = R9 + 0x5F08` and forwards it as:
    - `R3` to `scene_hud_main_panel_init()`
    - `R1` to `scene_hud_prompt_grid_init()`
  - therefore type-6 record `+0x3C` is best read as a pointer into the shared scene HUD actor-asset/controller root at `R9+0x5F08`
  - evidence: `0x0100F89E..0x0100F90A`
- narrowed shape of the packed local ints:
  - `scene_runtime_init_and_sync()` seeds two packed `s16` pairs before fresh-enter rebuild:
    - `{0, 67}`
    - `{240, 293}`
  - those same packed ints flow into both `scene_hud_main_panel_init()` and `scene_hud_prompt_grid_init()`
  - `scene_hud_prompt_grid_init()` stores the first pair into local shorts `+0xA/+0xC` and mirrors their negatives into `+0x2/+0x4`, strongly suggesting a viewport/window origin+extent contract
  - evidence: `0x0101301C..0x0101302C`, `0x0100F8EC..0x0100F90A`, `0x01046CB2..0x01046CD6`
- traced one layer further upstream, those packed ints originate from the actorinfo blob:
  - `parse_actorinfo_response()` fresh-enter path writes the last two `v34()` dwords from the stream into `sceneStatusSnapshotNode + 240/+244`
  - `scene_runtime_init_and_sync()` later forwards the low-16-bit view of those same two stored dwords into `scene_rebuild_runtime_nodes()`
  - evidence: `0x0100FD10..0x0100FD22` and `0x010134D2..0x01013586`
  - implication: the current fresh-enter tuple `{0,67}` / `{240,293}` is best treated as the low-halfword projection of the actorinfo blob's final two trailing `i32` fields, not as a separately invented local constant
- corrected by a later static pass:
  - those two final fresh-enter scalars are read by `v34()`, the same helper used for `actorResourceArg`
  - the stronger current reading is therefore “signed-16 values widened into dword slots at `sceneStatusSnapshotNode + 240/+244`”, not literal trailing actorinfo `i32`
  - evidence: `parse_actorinfo_response()` at `0x0100FCEA..0x0100FD22`
- corrected consumer mapping for the same pair:
  - `scene_runtime_init_and_sync()` still sign-extends `sceneStatusSnapshotNode + 240/+244` and passes them in `R1/R2` to `scene_rebuild_runtime_nodes()`
  - but `scene_rebuild_runtime_nodes()` does **not** feed those values into `scene_hud_main_panel_init()` or `scene_hud_prompt_grid_init()`
  - the only visible in-function use is carrying them as extra `R1/R2` on `scene_node_reset_at()` calls inside the node-clone loop, and `scene_node_reset_at()` itself does not consume those registers
  - evidence:
    - `scene_runtime_init_and_sync()` at `0x010134D2..0x01013586`
    - `scene_rebuild_runtime_nodes()` at `0x0100F832..0x0100F90A`
    - `scene_node_reset_at()` at `0x010459EC..0x01045A94`
- implication:
  - for the current `c00蓬莱仙岛_01 -> sub_100F094` scene-entry bug, the actorinfo tail pair at `+240/+244` is no longer a first-order suspect for the bad mode-10 panel family
  - the strongest remaining first-order pair is still `payload namePtr = currentMapIdText/sceneKey` plus callback-choice `(a8 != 0)`
- corrected ownership note for the generic-init path:
  - the heap-local function tables written by `scene_object_vtable_init(*[R9+0x54AC])` at offsets like `a1 + 1032 .. a1 + 1088` are not the same thing as the fixed global ops descriptor returned by `scene_get_actor_asset_ops_descriptor()`
  - `scene_hud_main_panel_init()` still dereferences `R9+0x5C48 + 0x10`, while `scene_object_vtable_init()` operates on the separately allocated scene object pointer stored in `[R9+0x54AC]`
  - evidence:
    - `scene_system_bootstrap()` allocation/call chain at `0x010038BC..0x010038CC`
    - `scene_get_actor_asset_ops_descriptor()` at `0x01018058..0x0101805C`
- implication:
  - do not equate heap-local scene-object slots such as `a1+1040/a1+1044` with the fixed global `R9+0x5C58/+0x5C5C` callbacks consumed by `scene_hud_main_panel_init()` and `scene_runtime_init_and_sync()`
  - the true writer for the fixed global `R9+0x5C48` descriptor remains unresolved
- narrowed further by static scan and loader semantics:
  - a focused whole-binary `0x5C48` scan found only the known reader family and literal-pool copies:
    - `scene_runtime_init_and_sync()`
    - `scene_get_actor_asset_ops_descriptor()`
    - `scene_hud_main_panel_init()`
    - `scene_actor_asset_slot_table_load_entry()`
    - `scene_draw_node_overhead_overlay()`
    - `sub_10183A0()` literal-pool reference
  - no additional main-binary store site for the base descriptor was exposed
  - constructor review of nearby scene/bootstrap initializers also only found writers for adjacent but different objects:
    - `sub_10035E6()` on `R9+0x55AC`
    - `sub_1048FEE()` on `R9+0x5EF0`
    - `sub_1031790()` on the large `R9+0x7CB4` callback bank
    - `scene_actor_asset_slot_table_init()` on `R9+0x5FD0`
- stronger current interpretation of the fixed ops block:
  - the emulator loader copies the module's data bytes from `fileBuffer + g_cbeInfo.BssDataOffset` into `ROM_ADDRESS + g_cbeInfo.headerInt2`, then sets `Global_R9 = ROM_ADDRESS + g_cbeInfo.headerInt2`
  - evidence: `src/main.c:9141-9146`, `src/main.c:9158`, `src/cbeParser.c:80-124`
  - implication:
    - `R9+0x5C48` lives in the copied module data image, not in the heap-local scene object built at `[R9+0x54AC]`
    - the shared actor-asset ops descriptor is now more likely image-seeded / loader-relocated small-data than a scene-local runtime constructor product
    - for the active fresh-enter `sceneKey + sub_100F094` misroute, the next high-value static target is to identify the preseeded callback values already present at `R9+0x5C48 + {8,0xC,0x10,0x14}`, not to keep searching for a missing scene-bootstrap writer in the main binary

## 2026-06-08 latest operator rerun: actor-adjacent fragments are progress-panel tiles

- confirmed fragment draw owner:
  - the new read-only actor-region blit trace hit its cap with 160 samples.
  - every sample in the visible actor/loading region uses `vMDrawImageWithClipEx()` from `srcInfo=05016cd0`, `srcPtr=05019d98`, `srcDim=36,36`, with source rectangle `sx=12 sy=12 w=12 h=12`.
  - the destination tiles advance by 12-pixel steps across the center region, e.g. `dx=48,60,72...180` and `dy=192,204...348`.
  - the immediate return path is `lr=01004141`, `caller=01004175`, `last=0100413e`.
  - static IDA maps that path to `sub_1004096()` / `sub_1004150()`, and the higher caller `0x010044B2` is inside `sub_1004362()`, the 36x36 nine-slice/panel tiler.
- corrected interpretation:
  - the currently visible "fragments" are not confirmed as actor body-frame fragments.
  - they are confirmed as the same tiled panel/progress skin path already logged by `trace_progress_strip_wrapper ... caller=010044b2`.
  - the older portal-derived `ActorMoveEntryEx` source remains blocked in this run (`trace_portal_move_entry_append_skipped`, then `trace_actor_move_entry_table ... count=0`), and no `trace_scene_body_draw_dispatch` appears after that.
- loading owner remains active:
  - runtime still shows repeated `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0 ... args=20,188,200`.
  - static IDA confirms `sub_1013C8A()` dispatches the widget at `0x01013CB8` when `s16[R9+0x9590] <= 0` and `R9+0x5C67/0x5C68` are both nonzero.
  - therefore the visible panel/strip and the actor-adjacent fragments are now best treated as one symptom: the scene loading/progress widget owner remains scheduled after the scene has otherwise reached stable `status_panels` / `actor_pass`.
- map-name/status correction:
  - `sub_100F094()` only writes the top-center scene/status text in the `actorTag == 4` branch, property kind `22`, storing the read string into `R9+0x5CD8+4` at `0x0100F68A`.
  - the logged portal-tail collision starts at raw `.sce` portal words (`tailShorts=3,2,223,382,8,1,5,1`) and the only skipped candidate is `actorTag == 2`, which only populates the 32-byte move-entry table.
  - hypothesis: the missing map title in the latest visual run means the current scene-key/descriptor stream did not deliver a valid `actorTag=4/property=22` status record, or that path was never reached after the `.sce` portal grammar collision. This is not yet evidence for an `actorinfo` field-order mismatch.
- network status:
  - `net_packets.log` still ends after the accepted `builtin-scene-resource-followup` response (`WT len=49`, response objects `1/12/1,1/7/42,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`).
  - no newer WT request or unhandled packet appears in this run.

### 2026-06-08 follow-up correction: status record exists, dispatch completion is still open

- corrected by the next manual run:
  - the extended callback-tail trace shows more than the portal prefix:
    - `tailShorts=3,2,223,382,8,1,5,1,3,6,...`
    - `tailHex` continues into a second scene descriptor filename (`c00蓬莱仙岛_02.sce`) and additional record tokens.
  - `sub_100F094()` reaches both scene text write sites:
    - `0x0100F6A0`: property kind `0x12` writes `R9+0x5CD8` (`trace_scene_status_text_write ... newText=<empty>`)
    - `0x0100F68A`: property kind `0x16` writes `R9+0x5CDC` (`trace_scene_status_text_write ... newText=蓬莱-铜雀台`)
  - the final `0x0100F78E` snapshot reports `count=0` and `statusText=蓬莱-铜雀台`.
- implication:
  - the older "no actorTag=4/property=22 status record" hypothesis is disproven for this run.
  - the portal-prefix collision is still real for the move-entry table, but it does not prevent the later status-text record from being parsed.
- remaining loading-widget evidence:
  - `sub_1013C8A()` continues to dispatch the loading widget with `sceneGate=1,1` and manager queue count `R9+0x9590 == 0`.
  - `trace_progress_strip_wrapper_tick` stays on the real `loading.gif` path (`caller=0x010460CA`, dst `20,188`).
- new dispatch hypothesis:
  - `builtin-scene-resource-followup` is queued and fired (`mock_response ptr=050179a0 len=319`, then `fire_event ... r0=050179a0 tick=138`), but current handler traces do not show its `12/1`, `7/42`, `6/*`, `2/10`, or `25/5` objects entering the expected business handlers.
  - next trace target is `net_business_response_dispatch()` itself: entry gate, unpack result/object count, per-object kind/subtype, and early-exit reason.

### 2026-06-08 dispatch-gate confirmation

- confirmed:
  - the scene-resource follow-up packet does enter `net_business_response_dispatch()` with the expected packet pointer and object count:
    - `trace_business_dispatch_state label=entry ... r0=050179a0 ... objectCount=7`
  - it is not unpack failure and not a missing queued event. It is rejected by the dispatch gate check at `0x01012E6E..0x01012E72`:
    - `dispatchGate=0`
    - `trace_business_dispatch_state label=early_gate_off`
  - therefore none of the follow-up response objects (`12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5`) are consumed on this run.
- preceding state:
  - the earlier group/type scene-enter response enters the same dispatcher with `dispatchGate=1`, unpacks 5 objects, and dispatches `5/10`, `10/26`, `30/1`, `30/3`, and `30/7`.
  - after that dispatch's `fallback_exit`, the same gate is already `0`.
- implication:
  - persistent loading is now tied to the fact that scene-resource follow-up data is delivered after the client has closed the scene object's business-dispatch gate.
  - the next question is packet sequencing / bundling / gate ownership: why is `sceneObj+0x164` cleared before the follow-up response that carries the requested resource/misc objects?
  - do not patch the gate open as a fix; trace the writer and then decide whether the mock should include required objects in the earlier dispatch window or trigger the client's expected gate reopening path.

### 2026-06-08 dispatch-gate writer and bundled follow-up experiment

- confirmed writer:
  - the scene gate reopens for scene entry at `pc=0508338c` (`trace_scene_dispatch_gate_write ... old=0 new=1 ... activeScreen=01053450`).
  - during the first scene-enter dispatch, item `30/1` clears the same byte: `trace_scene_dispatch_gate_write ... pc=01039766 ... old=1 new=0`.
  - static IDA identifies the clearing code as `parse_scene_response_entry()` / the `30/1` scene handler. It reads `scene` and `posinfo`, invokes scene-object methods, then stores zero to `sceneObj+0x164` at `0x01039766`.
- confirmed dispatcher behavior:
  - `net_business_response_dispatch()` checks `sceneObj+0x164` once near entry (`0x01012E6E..0x01012E72`) before unpacking/dispatching the packet.
  - in the latest run, the first group/type response dispatches 5 objects: `5/10`, `10/26`, `30/1`, `30/3`, `30/7`.
  - after `30/1` clears the gate, the same in-progress dispatch still continues to `30/3` and `30/7`.
  - later packets enter with `dispatchGate=0`; `7/7 type=2`, `7/7 type=3`, and the `builtin-scene-resource-followup` packet all take `early_gate_off`, so the follow-up's `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5` objects are not consumed.
- protocol implication:
  - the follow-up packet bytes are not currently the immediate mismatch; the ordering is. The client has already closed the scene business-dispatch gate by the time the mock sends those objects as a separate response.
  - this supersedes earlier wording that treated the accepted `builtin-scene-resource-followup` as proof that the remaining progress/loading panel was purely local residue.
- implementation experiment:
  - `vm_net_mock_build_group_type1_response()` now appends the same seven scene-resource follow-up objects after `30/1`, `30/3`, and `30/7`, keeping them inside the initial dispatch call without forcing `sceneObj+0x164` open.
  - the standalone `vm_net_mock_build_scene_resource_followup_response()` remains and reuses the same helper, so the response body stays byte-shape-aligned while the current sequencing hypothesis is tested.
  - hypothesis: because the dispatcher does not recheck the gate between items, the bundled objects should dispatch in the same call even after `30/1` clears the gate. The next run should confirm this through `trace_business_dispatch_item` before claiming the loading/progress owner is fixed.

### 2026-06-08 oversized bundle negative result

- confirmed negative result:
  - bundling all seven follow-up objects into `group-type1` produced `mock_group_type1_response ... objects=12 len=781`, but the first dispatcher call failed inside packet unpacking:
    - `trace_business_dispatch_state label=entry ... r1=0000030d ... dispatchGate=1`
    - `trace_business_dispatch_state label=unpack_error ... r0=00000003 ... objectCount=10`
  - no per-item dispatch occurred for that first group response, so `30/1` did not close the gate.
  - the later standalone 7-object follow-up was then accepted by the still-open gate and dispatched through `12/1`, `7/42`, `6/1`, and `6/13`, but stopped with a fault before item 4:
    - stdout: `pc=0000000a`, `lr=01012e9f`, `lastPc=01012e9c`, `r1=05001560`, `r5=4`
    - interpretation: after `6/13`, the per-item pre-dispatch callback loaded at `0x01012E9C` was `0xA`, so the next item dispatch tried to branch to an invalid low address.
- protocol implications:
  - a 12-object response is not a valid bundled shape for this scene-enter dispatch window; current evidence suggests the unpack/table path is limited to about 10 objects here.
  - `6/13 tasktypes` and `6/14 task action` must not be treated as confirmed-safe scene-entry follow-up objects in this dispatcher state. Their field shape may be individually parseable, but this runtime path corrupts the next per-item dispatch callback.
- superseded narrowed experiment:
  - `vm_net_mock_build_group_type1_response()` now bundles only five core follow-up objects: `12/1`, `7/42`, `6/1`, `2/10`, and `25/5`.
  - this keeps the group response at 10 objects total: `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, plus the five core follow-up objects.
  - later status: the following run confirmed the 10-object size fits, but same-window `2/10` corrupts the current actor node draw callback. The active experiment is now the 9-object set recorded below.

### 2026-06-08 10-object bundle result and 2/10 downgrade

- confirmed positive result:
  - a 10-object `group-type1` response is accepted by `event_packet_init()`:
    - `mock_group_type1_response ... objects=10 ... len=675`
    - `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`
  - the dispatcher processes all ten entries in order:
    - `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `2/10`, `25/5`.
  - `30/1` still clears `sceneObj+0x164` at `0x01039766`, and the dispatcher does not recheck the gate between entries. Later separate `7/7 type=2/3` responses are then blocked as expected.
- confirmed negative result:
  - same-window `2/10` corrupts the active actor node draw path on the next frame:
    - stdout reports `pc=9883a110`, `lr=01014597`, `lastPc=01014594`.
    - IDA maps `0x01014594` to `scene_draw_actor_pass()` calling `ActorSceneNode.drawAt` from node offset `+0x148`.
  - before this dispatch, the node publish trace had valid callbacks:
    - `trace_scene_current_node_publish ... current=05400000 ... drawAt=01045579 step=01045429`.
  - therefore `1/2/10 otherinfo` remains matched enough to create/update actor state in some paths, but it is not confirmed safe in the current scene-enter dispatch window. Treat its field completeness/timing as unresolved.
- current narrowed experiment:
  - `vm_net_mock_build_group_type1_response()` now bundles only `12/1`, `7/42`, `6/1`, and `25/5`, for 9 objects total with the base scene-enter response.
  - `2/10` remains only in the standalone follow-up response for comparison; on the normal path that standalone response should be blocked after `30/1` closes the dispatch gate.
  - hypothesis: if the next run avoids the bad `drawAt` crash but the loading panel persists, the remaining blocker is more likely the omitted `6/13/6/14` and/or the scene local state they would normally drive, not `2/10` itself.

### 2026-06-08 9-object bundle result

- confirmed:
  - the current `group-type1` response is accepted with nine objects: `mock_group_type1_response ... objects=9 ... len=579` and `trace_business_dispatch_state label=after_unpack_ok ... objectCount=9`.
  - dispatched order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `25/5`.
  - after `30/1` clears `sceneObj+0x164` at `0x01039766`, same-call dispatch still reaches the bundled `12/1`, `7/42`, `6/1`, and `25/5`.
  - the later standalone seven-object scene-resource follow-up is still blocked by `dispatchGate=0`, so its `6/13`, `6/14`, and `2/10` entries are not consumed on the normal path.
- confirmed:
  - removing same-window `2/10` avoids the previous actor-node draw callback fault. Runtime draw evidence stays stable at `scene_draw_actor_pass()` / `0x01014594`: `actorId=10001`, `grid=223,382`, `callback=01045579`, `drawAt=01045579`, `step=01045429`.
  - status text parsing is still present in the descriptor path: `trace_scene_status_text_write ... statusText=蓬莱-铜雀台`, followed by `trace_actor_move_entry_table ... count=0 ... statusText=蓬莱-铜雀台`.
- unresolved:
  - the loading/progress widget remains active through `sub_1013C8A()` with `sceneGate=1,1` and `R9+0x9590 == 0`.
  - the 9-object set is therefore a stable partial match, not a complete scene-entry protocol match.
- next evidence:
  - source now adds read-only write traces for `R9+0x5C67`, `R9+0x5C68`, and `R9+0x9590`. Use the next run to identify which parser or scene runtime path sets these owner inputs and whether a valid response should clear them.

### 2026-06-08 loading-owner gate correction

- confirmed:
  - live writes show `R9+0x5C67` and `R9+0x5C68` are set by `scene_runtime_init_and_sync()` at `0x010132C8/0x010132CA`, before the first group/type1 dispatch.
  - the latest run has no corresponding clear write and no write to `R9+0x9590`; the loading widget remains because `sub_1013C8A()` sees `sceneGate=1,1` and queue count `0`.
- static protocol evidence:
  - `net_handle_business_followup_events()` handles top-level `kind=27/subtype=11` and writes `R9+0x5C67` from the scene object's `fb` state.
  - `kind=27/subtype=12` is the confirmed primary path that writes that `fb` state via `ui_apply_named_posinfo_target_with_fb()`.
- active experiment:
  - the bundled first-window follow-up no longer includes unconfirmed `25/5`.
  - it now includes `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` followed by empty `27/11`, alongside `12/1`, `7/42`, and `6/1`.
  - object count remains 10 total in `group-type1`, matching the previously accepted maximum-sized shape while avoiding same-window `2/10` and `6/13` / `6/14`.

### 2026-06-08 27/12 plus 27/11 result

- confirmed:
  - the 10-object first-window bundle is accepted with `len=634` and object order:
    - `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`.
  - `27/12` and `27/11` both dispatch through `net_handle_fb_target_dispatch()` / `0x01010BC0`.
  - after normal dispatch, `net_handle_business_followup_events()` reaches `case27_11` at `0x010106D8` and clears `R9+0x5C67` at `0x010106F4`.
  - runtime evidence: `trace_scene_loading_owner_write field=sceneGateA_R9_5C67 ... old=1 new=0 ... pc=010106f4`.
- confirmed correction:
  - the former steady scene `loading.gif` owner is no longer active in this run: `trace_loading_gif_widget_draw_entry` count is `0`, and the narrowed progress wrapper has no `trace_progress_strip_wrapper_tick` hits.
  - therefore the current visible actor-adjacent fragments, if still present, are not confirmed to be the old `sub_1013C8A()` / `R9+0x60F4` loading widget.
- traffic consequence:
  - the previous larger `WT len=49` scene-resource-followup request does not occur after this bundle.
  - the client later sends only the small `WT len=14` pair (`12/1`, `7/42`), answered by the existing tail skill response; this later packet is still blocked by `dispatchGate=0` if delivered through the scene business dispatcher.
- hypothesis / next evidence:
  - tail traces now show repeated `sub_1003568 -> sub_100337C(0)` calls with `sceneTickGate=0,1`.
  - source now records actor-region image draws specifically in the `sceneGate=0,1` window, because the earlier trace required `1,1` and exhausted its cap before `27/11` cleared gateA.
  - use the next manual run to decide whether residual fragments are a live redraw from this gateA-cleared overlay path or stale pixels not repainted by the normal scene pass.

### 2026-06-08 27/11 alone leaves the scene tick overlay active

- confirmed from the next manual run:
  - the accepted first-window packet still has `objects=10 len=634`, and dispatch reaches only `27/12` then `27/11` after the core scene entries.
  - there is no `case27_4` trace in this run, so this log predates the new `27/4` builder experiment.
  - `case27_11` clears `R9+0x5C67` at `0x010106F4`, producing `sceneTickGate=0,1`.
  - the old `sub_1013C8A()` / `loading.gif` owner stays stopped: there are no `trace_loading_gif_widget_draw_entry` or narrowed `trace_progress_strip_wrapper_tick` hits.
- confirmed fragment owner:
  - immediately after `R9+0x5C67` is cleared, `scene_runtime_tick()` repeatedly calls `sub_1003568 -> sub_100337C(0)` from the branch at `0x01014D74..0x01014D80`.
  - the actor-region blit trace confirms the visible actor-adjacent fragments are live panel-tile draws in this gate state, not stale pixels:
    - `trace_loading_overlay_call label=sub_1003568 ... sceneTickGate=0,1`
    - `trace_scene_actor_region_draw ... srcDim=36,36 ... sx=12 sy=12 w=12 h=12 ... sceneGate=0,1 loadFlags=1,0,0`
- static evidence for the next packet candidate:
  - `net_handle_business_followup_events()` case `27/4` reads `min`, `result`, `type`, `fb`, and `info`.
  - when `result==1 && type==1`, it calls `sub_1010486()`, stores `fb` into the scene object, optionally copies `info`, then writes `R9+0x5C67=1` at `0x0101087E` and `R9+0x5C68=1` at `0x01010882`.
- implementation hypothesis:
  - the mock should not stop at `27/11`; it likely needs the matching `27/4` ready/finalize object inside the same first scene-enter dispatch window.
  - `src/main.c` now keeps the accepted 10-object limit by replacing the bundled all-empty `6/1 taskinfo` slot with `27/4(result=1,type=1,fb=1,info="")`, yielding follow-up objects `12/1`, `7/42`, `27/12`, `27/11`, and `27/4`.
  - the next manual run should confirm `trace_followup_case label=case27_4` and owner writes restoring both scene tick gate bytes to `1,1`; only after that can the remaining panel fragments be attributed to another wait flag such as `loadFlags=1,0,0`.

### 2026-06-08 27/4 ready branch result

- confirmed:
  - the first-window response with bundled `27/4` is accepted: `mock_group_type1_response ... objects=10 ... len=669`, followed by `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - object order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `27/12`, `27/11`, `27/4`.
  - `net_handle_business_followup_events()` reaches both `case27_11` and `case27_4`:
    - `case27_11` clears `R9+0x5C67` at `0x010106F4`.
    - `case27_4` then writes `R9+0x5C67=1` at `0x0101087E` and `R9+0x5C68=1` at `0x01010882`.
- confirmed correction:
  - `27/4` fixes the `sceneTickGate=0,1` branch caused by `27/11` alone; the post-`27/11` `sub_1003568` actor-region panel-tile trace no longer continues as the primary tail symptom.
  - however, restoring the gate pair to `1,1` makes the older `sub_1013C8A()` branch true again while `s16[R9+0x9590] == 0`, so the scene `loading.gif` widget is again drawn every tick:
    - `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`
    - `trace_progress_strip_wrapper_tick ... caller=010460ca ... dst=20,188`
- confirmed traffic regression:
  - because the bundled 10-object experiment spent the former `6/1 taskinfo` slot on `27/4`, the client again sends the larger `WT len=49` scene-resource follow-up request.
  - that standalone follow-up response is still rejected by `net_business_response_dispatch()` with `dispatchGate=0`, so its `6/1`, `6/13`, `6/14`, `2/10`, and `25/5` entries are not consumed.
- implementation hypothesis:
  - `6/1 taskinfo` must remain in the first scene-enter dispatch window, and the `27/12/27/11/27/4` trio must also remain there; the lower-priority `12/1 + 7/42` skill/book pair can be deferred because the client already has a separate small request path for it.
  - `src/main.c` now changes the bundled first-window follow-up set to `6/1`, `27/12`, `27/11`, and `27/4` for 9 total objects with the base scene entries.
  - next verification should check whether the large `WT len=49` request disappears again, whether only the small `12/1 + 7/42` request remains, and whether `R9+0x9590` / `sub_1013C8A()` changes after `loadFlags=0,0,0`.

### 2026-06-08 9-object taskinfo plus fb-target trio result

- confirmed:
  - the 9-object first-window response is accepted with `mock_group_type1_response ... objects=9 ... len=613`.
  - object order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `case27_11` and `case27_4` still execute, and `27/4` restores `R9+0x5C67/5C68` to `1,1`.
- confirmed negative result:
  - the large `WT len=49` scene-resource follow-up request still returns.
  - because the only intentionally omitted objects are now `12/1 + 7/42`, this proves the skill/book pair also belongs in the first scene-enter dispatch window if the goal is to avoid the large blocked follow-up.
  - the standalone large response is still rejected by `dispatchGate=0`.
- current hypothesis:
  - the first-window set needs `12/1`, `7/42`, `6/1`, and the ready/finalize `27/4`.
  - the `27/12 + 27/11` pair is now lower priority for the next experiment: `27/11` is confirmed to create the bad `sceneTickGate=0,1` branch when not immediately finalized, and including both consumes two scarce object slots.
  - `src/main.c` now uses first-window follow-up objects `12/1`, `7/42`, `6/1`, and `27/4`, for 9 total objects.
- next verification:
  - check whether the large `WT len=49` disappears again.
  - check whether `case27_4` alone reaches the ready/finalize side effects without `27/12/27/11`.
  - if the large request disappears but `loading.gif` persists with `overlayCounter=0`, move the next static/runtime target to the `R9+0x9588` manager-count producer path.

### 2026-06-08 12/1 + 7/42 + 6/1 + 27/4 negative result

- confirmed from the latest operator rerun:
  - the current 9-object first-window packet is accepted: `mock_group_type1_response ... objects=9 ... len=615`.
  - object order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/4`.
  - `net_handle_business_followup_events()` reaches `case12_1` and `case27_4`; `27/4` writes `R9+0x5C67/0x5C68` to `1,1`.
  - the client still sends the large `WT len=49` request for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the standalone large response again enters `net_business_response_dispatch()` with `dispatchGate=0` and is not consumed.
  - the visible strip/fragments remain the `sub_1013C8A()` / `loading.gif` owner path: `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- implication:
  - matching `12/1`, `7/42`, `6/1`, and `27/4` is not sufficient to satisfy the client's first scene resource wait.
  - the next likely missing first-window entries are among `6/13`, `6/14`, and `25/5`; same-window `2/10` remains excluded because an earlier accepted 10-object run corrupted `ActorSceneNode.drawAt` at `scene_draw_actor_pass()` / `0x01014594`.
- active experiment:
  - superseded by the next result below.

### 2026-06-08 6/13 same-window negative result

- confirmed from the latest operator rerun:
  - the first-window packet with `6/13` is accepted at the WT packet layer: `mock_group_type1_response ... objects=10 ... len=449`, followed by `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - object order starts as intended: `5/10`, `10/26`, `30/1`, `12/1`, `7/42`, `6/1`, `6/13`.
  - immediately after dispatch item index 6 (`kind=6 subtype=13`), the next per-item callback is corrupted:
    - stdout: `pc=04710400`, `lr=01012e9f`, `lastPc=01012e9c`, `r5=7`.
    - runtime trace: `trace_scene_loading_owner_write ... pc=0104d43c/0104d410`, where IDA identifies those addresses as memcpy internals, not legitimate scene-gate writers.
  - static IDA at `net_handle_task_response_dispatch()` case `13` (`0x01047896`) shows it initializes a stream from field `tasktypes`, then loops `R4=2..7`: read a short, write the type id into `R9+0x580C + 0x16*i + 0x14C`, clear a 10-byte name slot, read a length/pointer pair, and memcpy that many bytes into the name slot.
- corrected interpretation:
  - the current `vm_net_mock_append_tasktypes_empty13_object()` encoding is a mismatch for the case-13 inner string/length reader. It used the generic WT sequence string helper, but the parser's memcpy length becomes garbage in this context and overwrites scene/global state.
  - `6/13` and `6/14` must stay out of the first scene-enter dispatch window until their exact inner blob encoding and safe timing are recovered.
- active experiment:
  - superseded by the next result below.

### 2026-06-08 25/5 same-window negative result

- confirmed from the latest operator rerun:
  - the safe 10-object packet is accepted: `mock_group_type1_response ... objects=10 ... len=633`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `25/5`, `27/4`.
  - `case12_1` and `case27_4` both execute, and `27/4` leaves `R9+0x5C67/0x5C68` at `1,1`.
  - there is no crash and actor state remains stable (`actorId=10001`, valid draw callback), but the client still sends `WT len=49` at tick 134 for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the scene `loading.gif` owner still draws through `sub_1013C8A()` with `sceneGate=1,1 overlayCounter=0`.
- implication:
  - `25/5` is not the missing first-window object that suppresses WT49.
  - the remaining candidates are the `27/11` state event, same-window `2/10 otherinfo`, or correctly encoded/timed `6/13/6/14`.
- active experiment:
  - superseded by the next result below.

### 2026-06-08 27/11 plus 27/4 negative result

- confirmed from the latest operator rerun:
  - the 10-object packet is accepted: `mock_group_type1_response ... objects=10 ... len=621`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/11`, `27/4`.
  - `case27_11` and `case27_4` both execute.
  - without a preceding `27/12`, `case27_11` writes `R9+0x5C67` as `1 -> 1`; `case27_4` then writes `R9+0x5C67/0x5C68` as `1,1`.
  - the client still sends `WT len=49` at tick 150, and the `sub_1013C8A()` / `loading.gif` owner remains active.
- implication:
  - `27/11` by itself is not the state event that suppressed WT49 in the earlier `27/12+27/11` run. The preceding `27/12(result=1, fb=1, name, posinfo)` is likely required because it seeds the fb/name/posinfo state consumed by `case27_11`.
- active experiment:
  - `src/main.c` now keeps the first-window object count at 10 by omitting `30/7` room roles by default while keeping `30/3` room NPC.
  - the new hypothesis packet is `5/10`, `10/26`, `30/1`, `30/3`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - hypothesis: this tests the full fb-target trio plus skill/book/taskinfo in the same dispatch window without using the known-unsafe `2/10` or `6/13`.

### 2026-06-08 full fb-target trio with 30/3-only room list negative result

- confirmed from the latest operator rerun:
  - the 10-object first-window packet is accepted with `mock_group_type1_response ... sceneRoomNpc=1 sceneRoomRoles=0 ... len=558`.
  - `trace_business_dispatch_item` shows the exact order `5/10`, `10/26`, `30/1`, `30/3`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `27/12` reaches `net_handle_fb_target_dispatch()` at `0x01010BC0`.
  - `net_handle_business_followup_events()` reaches `case27_11` at `0x010106D8` and `case27_4` at `0x01010706`.
  - gate writes match the static parser expectation:
    - `0x010106F4`: `R9+0x5C67` `1 -> 0`
    - `0x0101087E`: `R9+0x5C67` `0 -> 1`
    - `0x01010882`: `R9+0x5C68` `1 -> 1`
- confirmed negative result:
  - the large scene-resource follow-up request still appears at tick 591: `WT len=49`, objects `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5`.
  - the mock standalone response is delivered at tick 592, but `net_business_response_dispatch()` sees `dispatchGate=0` and exits via `early_gate_off`.
  - the steady visible panel remains the `sub_1013C8A()` / `loading.gif` owner with `sceneGate=1,1` and `overlayCounter=0`.
- corrected interpretation:
  - the fb-target trio is confirmed structurally useful for gate transitions, but it does not by itself satisfy the client's post-enter resource wait in this 30/3-only arrangement.
  - `30/7` room roles is now a live first-window candidate because the prior successful-looking `27/12 + 27/11` run included room roles and the latest negative run omitted it.
- active experiment:
  - `src/main.c` now swaps the default room-list slot: omit `30/3` room NPC and include `30/7` room roles.
  - expected first-window order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - hypothesis: if WT49 disappears only with `30/7`, the room roles parser state is part of the readiness condition; if it persists, the remaining blockers are likely the unsafe/mismatched `6/13`/`6/14` or `2/10` encodings.

### 2026-06-08 30/7-only room list with full fb-target trio negative result and tasktypes correction

- confirmed from the latest operator rerun:
  - the 10-object first-window packet is accepted with `mock_group_type1_response ... sceneRoomNpc=0 sceneRoomRoles=1 ... len=528`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `30/7` reaches `sub_1039430()` at `0x01039430`.
  - `case27_11` and `case27_4` perform the same confirmed gate transition sequence: `R9+0x5C67` `1 -> 0 -> 1`, `R9+0x5C68` stays `1`.
  - the client still sends the large `WT len=49` request, and the standalone response is still blocked by `dispatchGate=0`.
- implication:
  - neither `30/3` nor `30/7`, individually paired with the fb-target trio, satisfies the first scene-resource wait.
  - the remaining first-window candidates move back to the requested `6/13`/`6/14` task-list pair and, later, a safe form of `2/10 otherinfo`.
- static correction for `6/13.tasktypes`:
  - `stream_reader_init_from_blob()` (`0x01033B16`) installs these read methods:
    - slot `+0x28`: `stream_read_i8_tagged()` (`0x01033AAC`), consuming `00 01 VV`
    - slot `+0x24`: `stream_read_i16_be_tagged()` (`0x01033A3A`), consuming `00 02 HH LL`
    - slot `+0x2C`: `stream_peek_i16_be()` (`0x01033A1E`), reading a raw big-endian string length without advancing
    - slot `+0x1C`: `stream_read_cstr_len16()` (`0x01033A86`), consuming `len16 + bytes`
  - case `13` at `0x01047896` therefore expects the `tasktypes` field value to begin directly with six records shaped as `tagged i8 + len16 C string`.
  - the previous mock used `vm_net_mock_put_object_blob()`, which prepended an extra raw `dataLen` word before those records. That was one wrapper mismatch.
  - the next trace then corrected the per-record scalar too: because `R6` is already `stream+0x400`, `[R6+0x28]` is `stream_read_i8_tagged()`, not `stream_read_i16_be_tagged()`. The old `00 02 00 00` scalar left the cursor at byte 3 and caused the next string length to be read from the wrong byte.
- active experiment:
  - `src/main.c` now emits `tasktypes` with `vm_net_mock_put_object_raw()` and per-record `tagged i8` tasktype ids.
  - the first-window bundle is now `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, corrected `6/13`, and `6/14`.
  - hypothesis: corrected `6/13` should no longer corrupt the next per-item callback; if WT49 persists, the next missing requested object is likely `2/10 otherinfo`.

### 2026-06-08 tasktypes tagged-i8 positive result and empty `2/10` experiment

- confirmed from the newest manual run:
  - the first-window packet is accepted: `mock_group_type1_response ... objects=10 ... len=659`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`.
  - `6/13` no longer corrupts the next per-item callback. `trace_tasktypes_case13_stream` shows the stream cursor advancing through six empty records and ending at cursor `36`; the pending `memcpy` length is `1` for each empty C string.
  - dispatch reaches `kind=6 subtype=14` at index `9`.
  - stdout has no fault in this run.
- confirmed negative result:
  - the client still sends the large `WT len=49` request at tick `147` for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the late `builtin-scene-resource-followup` is still blocked by `net_business_response_dispatch()` with `dispatchGate=0`.
  - the visible progress/fragments still come from `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- corrected interpretation:
  - `6/13.tasktypes` is now a confirmed parser match for the empty-tasktype shape: raw `tasktypes` field, six records of tagged i8 plus len16 C string.
  - `6/13 + 6/14` are not sufficient by themselves to satisfy the first-window scene-resource wait.
  - the previous non-empty same-window `2/10` crash should be attributed to the unconfirmed per-entry record shape or timing, not to the `2/10` object header itself.
- active experiment:
  - `src/main.c` now returns `1/2/10 {othernum=0, otherinfo=<empty>}` for both bundled and standalone `2/10`.
  - the first-window bundle keeps the accepted 10-object limit by defaulting `30/3 room NPC` off and preserving `30/7 room roles`.
  - expected next order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10(empty)`.
  - next verification should check whether WT49 disappears or changes, whether the adjacent protagonist-image fragments improve, and whether top-center map text returns or remains absent.

### 2026-06-08 empty `2/10` result and exact WT49 first-window experiment

- confirmed from the newest manual run:
  - the first-window packet is accepted: `mock_group_type1_response ... objects=10 ... sceneRoomNpc=0 sceneRoomRoles=1 ... len=536`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`.
  - empty `2/10` is parser-safe in this position; no stdout fault occurs and `trace_scene_current_node_draw` continues to report actor `10001` with valid callbacks.
- confirmed negative result:
  - the client still sends the large `WT len=49` request at tick `304`.
  - the late response is still blocked by `dispatchGate=0`.
  - the scene `loading.gif` widget still draws steadily from `sub_1013C8A()` with `sceneGate=1,1 overlayCounter=0`.
- implication:
  - empty `2/10` object presence is not sufficient to satisfy the first scene-resource wait.
  - after this run, the only object from the WT49 family that was not present in the open first-window dispatch was `25/5`.
  - because prior same-window `25/5` without `6/13/6/14/2/10` was also negative, the next experiment must test the exact seven-object family together rather than treating `25/5` alone as meaningful.
- active experiment:
  - defaults now omit both room-list objects (`30/3` and `30/7`) to keep the 10-object accepted limit.
  - first-window response now bundles the exact WT49 object family after the three base objects:
    - `5/10`, `10/26`, `30/1`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10(empty)`, `25/5`.
  - if WT49 still appears after this exact same-window coverage, the readiness condition is not just “all requested object headers were seen”; the next suspects become room-list side effects or non-empty semantic payloads.

### 2026-06-08 exact WT49 first-window negative result and misc-player bundle experiment

- confirmed from the newest manual run:
  - the first-window packet is accepted with `mock_group_type1_response ... objects=10 ... sceneRoomNpc=0 sceneRoomRoles=0 bundledSceneFollowup=1 ... len=424`.
  - `net_business_response_dispatch()` reaches the exact same-window WT49 family: `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, and `25/5`.
  - `6/13.tasktypes` remains parser-matched (`tagged i8 + len16 C string`, cursor ending at `36`), and empty `2/10` does not corrupt the protagonist node.
  - the current node remains valid in `scene_draw_actor_pass`: actor `10001`, grid `223,382`, `callback=drawAt=0x01045579`.
- confirmed negative result:
  - the client still emits the later `WT len=49` request.
  - the late standalone `builtin-scene-resource-followup` is still blocked by `dispatchGate=0`.
  - the steady progress strip remains the scene `loading.gif` owner path: `sub_1013C8A()` sees `sceneGate=1,1` and manager count `R9+0x9590 == 0`.
- corrected interpretation:
  - same-window presence of the exact requested object headers is not sufficient to complete the scene-resource wait.
  - two nearby scene-bootstrap misc-player replies are still not being consumed in the current sequencing: client requests `1/7/7 type=2` and `type=3` immediately after the bundled `type=1`, but their standalone responses arrive after `30/1` closes the scene object's dispatch gate.
  - static parser evidence: `net_handle_misc_player_fields()` at `0x01011C88` maps subtype `20` to `pcimg` and subtype `32` to `expcard`; both then flow through the common `kind=7` post-handler.
- active builder experiment:
  - `vm_net_mock_build_group_type1_response()` now bundles `7/20 {result=1, pcimg=0}` and `7/32 {result=1, expcard=0}` in the open first-window response before `30/1`.
  - evidence marker for the next run: `mock_group_type1_response ... bundledType2Type3=1 ... objects=12`.
  - hypothesis: if this changes the loading/fragments or suppresses WT49, then the blocked `1/7/7 type=2/3` replies are part of the normal scene-ready contract. If it does not, the remaining candidates shift back to semantic payload content and resource-manager/local descriptor readiness rather than raw object presence.

### 2026-06-08 12-object first-window unpack error and deferred scene-enter experiment

- confirmed from the newest manual run:
  - the 12-object first-window response is not parser-valid on this path: `net_business_response_dispatch()` enters with `responseLen=484`, then logs `unpack_error` with `r0=3` and `objectCount=10`.
  - because the invalid first packet never dispatches its embedded `30/1`, the scene object's dispatch gate remains open.
  - the later standalone `7/20.pcimg`, `7/32.expcard`, and the seven-object scene-resource response are then consumed successfully.
- confirmed negative result:
  - consuming those later objects still does not stop the `sub_1013C8A()` / `loading.gif` owner.
  - this run cannot prove the first-window 12-object semantic idea because the packet failed before object dispatch.
- corrected protocol constraint:
  - keep business response objects at 10 or fewer for this unpacker path unless static recovery proves a larger table is valid under different setup.
  - `30/1` remains dangerous early because it closes `sceneObj+0x164` before the client can consume the later requested scene-resource objects.
- active builder experiment:
  - first group/type1 response now stays small: `5/10`, `10/26`, `7/20`, `7/32`.
  - the scene-resource follow-up response now contains the requested seven-object WT49 family followed by trailing `30/1 scene+posinfo`.
  - hypothesis: this ordering lets the client consume group/misc fields, then requested resource fields, then close/enter the scene in a single valid follow-up window.

### 2026-06-08 valid deferred `30/1` ordering and remaining local scene wait

- confirmed from the newest manual run:
  - the small first group/type1 response is valid and dispatches exactly `5/10`, `10/26`, `7/20.pcimg`, and `7/32.expcard`.
  - the later scene-resource response is also valid and dispatches `12/1`, `7/42`, `6/1`, corrected `6/13.tasktypes`, `6/14`, empty `2/10.otherinfo`, `25/5`, then trailing `30/1.scene+posinfo`.
  - `30/1` clears `sceneObj+0x164` at `0x01039766` only after the seven requested WT49-family objects dispatch.
- packet-shape conclusion:
  - this validates the current mock's field ordering for the tested empty/minimal scene-resource family:
    - `6/13.tasktypes`: raw field payload, six records of `tagged i8 + len16 C string`.
    - `2/10.otherinfo`: parser-safe empty object with `othernum=0` and empty `otherinfo`.
    - `30/1.posinfo`: two tagged i16 coordinates.
  - no `unpack_error` or `early_gate_off` appears for this shape.
- remaining non-packet evidence:
  - the client still draws the scene loading widget from `sub_1013C8A()` because `R9+0x9590 == 0` and the two scene gate bytes remain `1`.
  - no resource-manager enqueue trace fired in this run, so the outstanding readiness condition currently looks local to scene descriptor/resource scheduling rather than a missing network response.
  - the actorinfo tail scene key remains a live hypothesis: `parse_actorinfo_response()` copies the second tail string into `R9+0x5E46`, and `scene_rebuild_runtime_nodes()` later pairs that value with hardcoded mode `10` and callback `sub_100F094`.

### 2026-06-08 actorinfo scene-key collision experiment

- static evidence:
  - `parse_actor_motion_descriptor()` calls `sub_100D534(name)` at `0x0100D6FC`.
  - if that local resource open succeeds, the function parses the returned stream and directly calls callback `a8` at `0x0100DB32`.
  - if that open fails, control branches through `0x0100D7D2` to `0x0100DB4A`, where it calls the `R9+0x9588` queue writer (`sub_10365F0`) with record type `6` and callback-choice byte `(a8 != 0)`.
- protocol/mock implication:
  - sending actorinfo's second tail string as the real local GBK `.sce` key currently makes the first descriptor pass parse portal data as a `sub_100F094` stream.
  - the active experiment changes only that c-key default to ASCII `c00PenglaiXiandao_01`, which is still c-prefixed but does not collide with an on-disk `.sce` in the current resource set.
  - the resource-update mock now serves the existing minimal motion-descriptor wrapper for that key if the client requests it.
- status:
  - hypothesis until the next runtime log proves whether the client now enters the queue/update path and whether the loading/fragments change.

### 2026-06-08 ASCII descriptor update path and WT39 resource subset

- confirmed runtime result:
  - `trace_update_request_prepare ... name=c00PenglaiXiandao_01 type=6 start=0 version=0` appears after the ASCII actorinfo scene-key experiment.
  - `mock_update_chunk_response ... chunk=44 totalsize=44` serves the minimal motion-wrapper payload, and `storage_trace.log` shows `MMORPGTempbin` renamed to `JHOnlineData/c00PenglaiXiandao_01`.
  - `parse_actor_motion_descriptor()` then opens `c00PenglaiXiandao_01`, `ui_h_war.actor`, and `h_warriorwalk2.gif`; the bytes handed to `sub_100F094()` are therefore no longer the local GBK `.sce` portal stream.
  - the minimal wrapper path currently yields `trace_actor_move_entry_table ... count=0 promptName=<null> statusText=<null>`, so it avoids the old portal body-entry but does not yet supply map-title/status semantics.
- confirmed packet consequence:
  - after the separate `12/1 + 7/42` request and the `18/7 type=6` update response, the client sends a smaller follow-up:
    - `WT len=39 hdr=6/1 objs=1/6/1,1/6/13,1/6/14,1/2/10,1/25/5 count=5`
  - older `src/main.c` only recognized the full `WT len=49` family and asserted on this subset.
- current mock behavior:
  - `builtin-scene-resource-followup` now accepts both shapes:
    - full `WT49`: `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`
    - post-update `WT39`: `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`
  - the response mirrors only the requested skill/book presence, then appends trailing `30/1 scene+posinfo` to close/enter after the resource-family objects dispatch.
- interpretation:
  - the latest loading/progress symptom cannot be classified as purely local widget residue yet, because the newest run ended on a confirmed unhandled WT request.
  - actor-adjacent fragments in the next run should be evaluated after this WT39 response is consumed; the old `.sce` portal-entry root cause is no longer confirmed on the ASCII-key path.

### 2026-06-08 ASCII descriptor rollback

- confirmed after the next manual run:
  - the previously downloaded `JHOnlineData/c00PenglaiXiandao_01` was cached, so the client skipped `18/7 type=6` and opened that 44-byte descriptor directly.
  - the visible sprite fragments disappeared, but the map viewport became black.
  - storage evidence shows no open of local `c00蓬莱仙岛_01.sce` in that run; only the cached ASCII descriptor was used for the mode-10 descriptor path.
  - packet evidence remains good: the WT49 follow-up is consumed as eight objects, with trailing `30/1` closing the scene dispatch gate after `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, and `25/5`.
- conclusion:
  - the non-colliding ASCII key proves that the portal/body-entry artifact came from the local `.sce` descriptor path, but it also proves the local `.sce` path is needed for normal map/background initialization.
  - therefore the default mock should not avoid the concrete scene descriptor. The better current policy is to keep actorinfo's scene key aligned with `30/1.scene`, then prevent only the confirmed portal-shaped body/world entry from being published while more precise descriptor semantics are recovered.
- current mock state:
  - actorinfo's scene key default is back to `vm_net_mock_default_scene_name()`; `CBE_SCENE_KEY` remains an explicit override for ASCII/update-path experiments.
  - WT49 and WT39 scene-resource follow-up handling remains parser-safe and should continue to be validated in later runs.

### 2026-06-08 WT49 fb-target seed experiment

- latest confirmed correction:
  - with the default GBK scene key restored, the map background and title return, and storage trace again opens `JHOnlineData/c00蓬莱仙岛_01.sce`.
  - the current actor-adjacent artifact is not a published body/world move-entry: the narrow portal guard skips the known portal-shaped candidate and `trace_actor_move_entry_table` reports `count=0`.
  - the remaining artifact is the scene `loading.gif` owner path from `sub_1013C8A()`, evidenced by repeated `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- active mock experiment:
  - superseded first attempt: full WT49 scene-resource follow-up response appended `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` and empty `27/11` before trailing `30/1`.
  - runtime result: the 10-object packet was accepted and `case27_11` executed, but it immediately produced the expected `sceneTickGate=0,1` branch and repeated `sub_1003568 -> sub_100337C(0)` calls.
  - corrected experiment: full WT49 scene-resource follow-up response now appends `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` only, before trailing `30/1`, so the client can request the later `27/11 + 27/4` finalize chain itself.
  - full WT49 response order is now:
    - `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`, `27/12`, `30/1`
  - WT39 subset responses remain limited to the requested task/other/info family plus trailing `30/1`.
- status:
  - `confirmed negative`: proactive `27/11` is not a safe WT49-tail object by itself; it clears gate A to 0.
  - superseded `hypothesis`: `27/12` alone would make the client drive the later `27/11 + 27/4` exchange via its own follow-up request.

### 2026-06-08 WT49 `27/12`-only negative and non-empty `27/4` experiment

- confirmed runtime result from the newest manual run:
  - the full WT49 resource response is accepted as 9 objects: `12/1`, `7/42`, `6/1`, corrected `6/13`, `6/14`, empty `2/10`, `25/5`, `27/12`, trailing `30/1`.
  - `net_handle_fb_target_dispatch()` consumes `27/12`: `trace_business_handler pc=01010bc0 packet=05001668 kind=27 subtype=12`.
  - no later `WT len=34`-class request appears, and no `mock_type27_followup_combo_response` appears.
  - the steady widget owner remains `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- static reason for the next field experiment:
  - `net_handle_business_followup_events()` case `27/4` at `0x01010706..0x01010882` reads `min`, `result`, `type`, `fb`, and `info`.
  - its simple success path writes `R9+0x5C67=1` and `R9+0x5C68=1`; when `info` is non-empty it also takes extra scene-object info side effects before those gate writes.
- current mock experiment:
  - full WT49 responses now append `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` and `27/4(result=1, type=1, fb=1, info=<scene title>)`, then trailing `30/1`.
  - this deliberately omits `27/11`, because the latest accepted `27/12 + 27/11` WT49-tail run proved it enters `sceneTickGate=0,1`.
  - evidence marker for the next run: `mock_scene_resource_followup_response ... fbSeed12Ready4Info=1`.
  - hypothesis: if the missing semantic is the non-empty `27/4.info` branch, the next run should show `case27_4` without `case27_11` and should change the `sub_1013C8A()` loading-widget condition or subsequent request sequence.

### 2026-06-08 WT49 `27/12 + 27/4(info)` result and full trio experiment

- confirmed runtime result:
  - the 10-object WT49 response with `27/12 + 27/4(info)` is parser-valid and dispatches both primary handlers:
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=12`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=4`
  - follow-up scanning reaches `case27_4` without `case27_11`.
  - the non-empty `info` branch is no longer just theoretical: the run visibly pops the map-name prompt, and trace shows the `case27_4` path allocating/copying the info string before writing `R9+0x5C67/0x5C68` to `1,1`.
- confirmed negative:
  - `case27_4` without `case27_11` does not change the `sub_1013C8A()` loading-widget condition. The tail remains `trace_loading_gif_widget_draw_entry ... sceneGate=1,1 overlayCounter=0`.
  - the actor/body table remains empty (`trace_actor_move_entry_table ... count=0`), so the visible actor-adjacent tiles are still the loading/progress skin, not body/world actor draw.
- new sequencing evidence:
  - `27/12` already calls `ui_apply_named_posinfo_target_with_fb()` and clears the scene business dispatch gate at `0x0100EA6A`.
  - in this packet shape, trailing `30/1` then only writes the same gate to `0` again at `0x01039766`.
- current mock experiment:
  - full WT49 responses now keep the 10-object limit by replacing trailing `30/1` with `27/11`.
  - full WT49 tail is now `27/12(result=1,fb=1,name,posinfo)`, `27/11`, `27/4(result=1,type=1,fb=1,info=<scene title>)`.
  - WT39 subset responses still append trailing `30/1` and do not use the fb-target trio.
  - evidence marker for the next run: `mock_scene_resource_followup_response ... fbFull12_11_4Info=1 trailingSceneEnter=0`.
  - hypothesis: the complete trio may need to run in one follow-up scan after the exact WT49 family; `27/4` should repair the `sceneGateA=0` side effect from `27/11` in the same pass.

### 2026-06-08 WT49 full trio negative and `2/1 moveinfo` upload ack

- confirmed runtime result:
  - the newest manual run hit the intended full-trio marker: `mock_scene_resource_followup_response objects=10 ... fbFull12_11_4Info=1 trailingSceneEnter=0 len=390`.
  - primary dispatch consumed all three fb-target objects:
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=12`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=11`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=4`
  - follow-up scanning reached `case27_11` and then `case27_4`; trace shows `case27_11` writing `R9+0x5C67` to `0`, then `case27_4` restoring `R9+0x5C67/0x5C68` to `1,1`.
- confirmed negative:
  - the full trio still does not satisfy the `sub_1013C8A()` loading-owner condition. Tail trace remains `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
  - without the former trailing `30/1`, `runtime_state ... sceneState=1` is observed instead of the earlier `sceneState=7`; the map still draws, but this is a live sequencing difference to preserve as evidence.
- new packet evidence:
  - after manual click/move, the client emits `WT len=32 hdr=2/1 objs=1/2/1 count=1` with a `moveinfo` field: payload hex `57540020010201001c086d6f7665696e666f000c000a01000000000000000000`.
  - the previous mock had no handler and asserted on `unhandled_packet`.
- static parser evidence:
  - IDA `net_handle_actor_move_info()` at `0x01012ADC` reads subtype from `[R0+8]`, executes `SUBS R1,#2` at `0x01012AFA`, and subtype `1` falls through the default return at `0x01012B0C`.
  - the branch that actually reads field `moveinfo` starts at `0x01012B2E` and is case subtype `2`, with comments around `0x01012B6A..0x01012B76` for `moveInfo`, `gridPosY`, `moveInfoLen`, `actorId`, and `gridPosX`.
- current mock behavior:
  - `builtin-actor-moveinfo-ack` now recognizes a one-object `1/2/1` request containing `moveinfo` and returns an empty `1/2/1` object.
  - status: `confirmed parser-safe by static branch evidence`; semantic meaning of live movement upload remains `hypothesis`.

### 2026-06-08 WT49 full trio plus restored `30/1` experiment

- confirmed from the latest screenshot/log pair:
  - map background, top title, and protagonist draw are normal.
  - no `2/1 moveinfo` upload appears in this run, so `builtin-actor-moveinfo-ack` was not exercised.
  - the actor-adjacent artifact remains exactly aligned with `trace_loading_gif_widget_draw_entry ... caller=01013cba ... args=20,188,200`.
  - IDA `sub_1013C8A()` at `0x01013C8A` draws that widget only when `s16[R9+0x9590] <= 0` and `R9+0x5C67/+0x5C68` are both nonzero; runtime logs show `overlayCounter=0 sceneGate=1,1`.
- sequencing correction:
  - the prior full-trio packet omitted trailing `30/1`, and runtime reported `sceneState=1` instead of the earlier `sceneState=7` seen when `30/1` was present.
  - because the full trio alone is now confirmed negative, the next packet-shape experiment restores `30/1` while staying within the observed 10-object limit by omitting `25/5`.
- current mock experiment:
  - full WT49 response order is now `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `27/12`, `27/11`, `27/4(info)`, trailing `30/1`.
  - evidence marker for the next run: `mock_scene_resource_followup_response ... result25_5=0 fbFull12_11_4Info=1 trailingSceneEnter=1`.
  - hypothesis: if the remaining issue is the missing scene-enter finalization after the fb-target trio, this should restore the `30/1` side effects and change `sceneState` and/or the persistent `sub_1013C8A()` widget condition.

### 2026-06-08 WT49 restored `30/1` negative and runtime moveinfo ack

- confirmed runtime result:
  - the restored full WT49 response is parser-valid: `mock_scene_resource_followup_response objects=10 ... result25_5=0 fbFull12_11_4Info=1 trailingSceneEnter=1 len=420`.
  - `27/11`, `27/4(info)`, and trailing `30/1` are all consumed; the post-data snapshot returns to `sceneState=7` with `sceneGate=0` and `loadFlags=0,0,0`.
  - after manual movement, the client sends `WT len=32 hdr=2/1 objs=1/2/1` carrying `moveinfo`, and `builtin-actor-moveinfo-ack` returns an empty `1/2/1` response.
- confirmed negative:
  - restoring trailing `30/1` after the fb-target trio does not clear the scene loading widget: repeated runtime traces remain `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
  - the runtime-exercised `2/1 moveinfo` ack does not change the widget condition or produce a new unhandled packet. The ack response is delivered after the scene business dispatch gate is closed and therefore logs `early_gate_off`; this is consistent with static `net_handle_actor_move_info()` evidence that subtype `1` falls through the default return at `0x01012B0C`.
  - no resource-manager enqueue trace or `R9+0x9590` write appears in this run, so the `R9+0x9588` queued-entry count stays `0`.
- protocol implication:
  - for the currently tested shape, the missing field is no longer likely to be simple WT49 tail ordering among `25/5`, `27/12`, `27/11`, `27/4(info)`, and `30/1`.
  - the next protocol/runtime boundary is the manager/replay contract behind `R9+0x9588` and the local descriptor path that pairs `currentMapIdText=c00蓬莱仙岛_01`, `mode=10`, and `callback=sub_100F094+1`.

### 2026-06-08 descriptor/manager trace points for next run

- trace-only instrumentation added after the above negative:
  - `trace_actor_motion_open_result` at `0x0100D702` records whether `parse_actor_motion_descriptor()` takes the local-open-success/direct-parse path or the local-open-fail/enqueue path after `sub_100D534(name)`.
  - `trace_actor_motion_enqueue_fallback` at `0x0100DB4A` / `0x0100DB74` records the fallback and final manager-writer call context if a descriptor is not opened locally.
  - `trace_manager_replay_entry` at `0x01036768` records the `R9+0x9588` current record before `scene_hud_main_panel_sync_message()` replays it into `scene_hud_main_panel_init()`.
- intended evidence split:
  - `local_open_success_direct_parse` for the GBK `.sce` key would support the hypothesis that the persistent loading/progress widget is driven by local descriptor/callback semantics rather than an outstanding server response.
  - fallback/enqueue evidence would reopen the question of a missing `18/7 type=6` resource response or wrong descriptor key policy.

### 2026-06-08 descriptor/manager trace result

- confirmed result:
  - default GBK key `c00蓬莱仙岛_01` opens locally in `parse_actor_motion_descriptor()` (`0x0100D6E2`): `trace_actor_motion_open_result ... branch=local_open_success_direct_parse`.
  - the fallback path to `get_net_manager_object()->sub_10365F0(type=6, ...)` is not taken on the default path, and `scene_hud_main_panel_sync_message()` (`0x01036768`) does not replay a `R9+0x9588` record.
  - static `sub_100D6E2` evidence matches this split: local-open success parses the `.sce` body and only then calls `a8(stream,cursor)` if the callback is non-null; local-open failure is the only branch that queues a type-6 manager record.
- implication:
  - current evidence does not support another simple `WT49` response-field mismatch as the root cause of the visible loading/progress widget.
  - the still-live boundary is local: the `.sce` tail is being handed to the hardcoded `sub_100F094` callback from `scene_rebuild_runtime_nodes()` (`0x0100F8E8..0x0100F8F6`), while `R9+0x9590` remains zero and `sub_1013C8A()` continues to draw the loading widget.

### 2026-06-08 message-box vs loading-widget trace split

- corrected evidence boundary:
  - `loading_gif_widget_draw_frame()` only performs the center-strip blit while widget byte `+0x10` is set. The newest manual run's later steady entries have `trace_loading_gif_widget_draw_entry ... flag10=0`, so those entries are no longer confirmed evidence of ongoing blitting.
  - the visible centered green `蓬莱-铜雀台` panel is now tracked as a separate message-box path, not as protocol-confirmed loading residue.
- new read-only trace for the next run:
  - `trace_scene_message_request` covers `sub_1013BDC()` call sites (`0x01013C28`, `0x01013C52`, `0x01013C84`), `sub_10110E6()` entry/branches (`0x010110E6`, `0x01011112`, `0x01011120`), and `sub_101037E()` queued-message insertion (`0x0101037E`).
  - expected interpretation: `message_box_branch` with map-title text supports a normal local prompt/modal path; `scene_widget_timeout_message` supports a still-missing local completion signal. Neither outcome should be treated as a new server response format until paired with parser evidence.

### 2026-06-08 actorinfo `previewImage` correction

- confirmed runtime/static mismatch:
  - latest runtime actorinfo snapshot: `preview=h_warriorwalk1.gif`, `actor=h_warrior.actor`, `scene=c00蓬莱仙岛_01`.
  - IDA final consumer `scene_draw_node_overhead_overlay()` (`0x01045578`) treats the `actor+0x10A` string as an optional overhead named badge/icon. It checks `sub_1000342(a1+266) > 0`, resolves the named asset through the shared actor/HUD asset pipeline at `0x010456AC`, and draws it at `0x01045762` or `0x01045834`.
- protocol implication:
  - the current mock's `previewImage` slot must not default to the actor walk GIF. That made body art eligible for overhead badge rendering, matching the visible actor-fragment artifact.
  - `actor+0xD8` remains the real `.actor` resource string used for the actor resource family, and is unchanged.
- current mock behavior:
  - `vm_net_mock_actor_preview_image_name()` now defaults to `""` and only sends a value when `CBE_ACTOR_PREVIEW_IMAGE` is explicitly set.
  - status: `confirmed parser-consumer mismatch`; visual disappearance still needs the next manual run for runtime confirmation.

### 2026-06-08 post-scene-change WT44 task subset

- observed request:
  - `WT len=44 hdr=25/5`
  - objects: `1/25/5`, `1/6/1`, `1/6/13`, `1/6/14`, `1/2/10`, `1/25/5`
  - final object carries `Type=101`.
- relationship to earlier WT49:
  - this is the already handled scene-resource follow-up family minus `12/1 learnedskill` and `7/42 booksinfo`.
  - it appears after a `2/3 maptype/mapID/exitID` scene-change exchange, so no scene key is available in the WT44 request itself.
- current mock behavior:
  - `builtin-scene-task-subset-followup` replies with parser-safe empty forms for `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - corrected after the newest run: the WT44 response itself is too late for scene completion because the scene dispatch gate is already closed. It should not carry `30/1`; scene completion belongs in the preceding scene-change combo response.
- status:
  - `confirmed unhandled packet shape`
  - `confirmed parser-safe response fields by prior IDA/runtime evidence`
  - `confirmed`: scene-aware trailing `30/1` inside WT44 reaches `early_gate_off` and is not consumed.

### 2026-06-08 scene-change loading completion

- runtime evidence:
  - after `mock_scene_change_combo_response ... scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`, the client emits the WT44 task subset and receives `builtin-scene-task-subset-followup responseLen=177`.
  - because that response had no `30/1`, the client keeps `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1 ... frame=189..300` and continues blitting the center strip through `trace_progress_strip_wrapper_tick ... caller=010460ca dst=20,188`.
  - `sub_1013BDC()` then hits the timeout path at `0x01013C84`, shown by `trace_scene_message_request label=scene_widget_timeout_message ... text=网络连接超时!`, and only that timeout clears `gate5530`.
  - a follow-up experiment moved scene-aware `30/1` into the WT44 response; runtime proved it is too late: `trace_business_dispatch_state label=early_gate_off ... dispatchGate=0 objectCount=6`.
  - a later run with `30/1` moved into `mock_scene_change_combo_response` confirms that object is consumed in the same dispatch window:
    - `mock_scene_change_combo_response objects=6 ... trailingSceneEnter=1 scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`
    - `trace_business_handler ... kind=30 subtype=1`
    - `runtime_state ... sceneGate=0 sceneState=7 loadFlags=1,0,0`
    - the subsequent WT44 response takes `early_gate_off`, then `runtime_state ... loadFlags=0,0,0`, but the visible strip still logs `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1` and paired `trace_progress_strip_wrapper_tick`.
  - the next run with `R9+0x5530` write tracing identifies the exact late-WT44 failure:
    - when the client sends the post-scene-change WT44 request, `sub_103478E()` copies request-object byte `+5` into `R9+0x5530` at `0x01034796`, opening the loading strip.
    - the WT44 response is queued, but `net_business_response_dispatch()` immediately takes `early_gate_off` at `0x01012F42` because the prior combo `30/1` has closed `sceneObj+0x164`.
    - the wrapper then clears only `R9+0x5531` at `0x010348F6`; no parser handler runs to clear `R9+0x5530`, so the strip remains active.
- static evidence:
  - IDA `net_handle_scene_channel_dispatch()` (`0x01039B8A`) dispatches subtype `1` to `parse_scene_response_entry()` and subtype `2` to `parse_scene_posinfo_field()`.
  - this matches runtime from the successful first scene-resource follow-up: trailing `kind=30 subtype=1` is followed by `runtime_state ... sceneState=7 loadFlags=0,0,0` and loading-widget entries with `gate5530=0`.
- current builder:
  - `mock_scene_change_combo_response` records the latest scene-change target.
  - `mock_scene_change_combo_response` now appends `30/1` with that scene and spawn in the same dispatch window as `30/2`, e.g. `scene=00蓬莱仙岛_02.sce pos=128,45`.
  - confirmed negative experiment: for the observed second-map request shape (`27/11`, `27/4`, `7/42` but no `12/1`), pre-bundling the WT44 task subset before `30/1` at the 10-object practical limit was consumed but did not suppress the later WT44 request.
    - runtime marker: `mock_scene_change_combo_response objects=10 ... fb11empty=0 ... taskSubset=1 ... trailingSceneEnter=1`.
    - immediately afterward, the client still sent `WT len=44`, the response reached `net_business_response_dispatch()` with `dispatchGate=0`, and `early_gate_off` at `0x01012F42` left `R9+0x5530` set until the `sub_1013BDC()` timeout path.
  - current code has reverted that negative experiment; the scene-change combo again mirrors the requested scene-change family plus scene-aware trailing `30/1`.
  - `mock_scene_task_subset_followup_response` returns only parser-safe task/other/banner objects because its event is expected after `dispatchGate=0`.
- status:
  - `confirmed`: WT44 without `30/1` is insufficient and times out.
  - `confirmed`: WT44 plus scene-aware `30/1` is also insufficient because it is delivered after dispatch gate close.
  - `confirmed partial/negative`: scene-change combo plus scene-aware `30/1` restores the normal `30/1` parser side effect (`sceneState=7`) but is not sufficient by itself to clear the second-map loading strip.
  - `confirmed`: the stuck strip is caused by a late WT44 request opening `R9+0x5530` after the scene dispatch gate has closed, then receiving a response that cannot reach the clearing handlers.
  - `confirmed negative`: pre-bundling the WT44 subset in the scene-change combo does not suppress the late WT44 request and does not leave `R9+0x5530` clear after the combo.
  - `confirmed`: first-scene normal timing uses the same `sub_1013BDC()` wait callback (`R9+0x554C == 0x0103478F`). Before the call, `waitObjFlag10=7`; after `BLX`, callback return `R0=0x31`, `waitObjFlag10=0`, and the sent WT49 response dispatches before the scene gate closes, so handlers clear `R9+0x5530`.
  - `confirmed`: `R9+0x5564` is `eventObj+0x10`, the outgoing WT object count. On the second-map scene-change path, that count is incremented during combo consumption and then sent by `sub_1013BDC()`.
  - `static evidence`: `sub_1049188()` (`0x01049188`), reached from the `30/2` scene-change handler, calls `alloc_outgoing_game_event(2, 0, 25, 5)` at `0x01049192`, matching the first object in the late WT44 request.
  - `confirmed`: the late WT44 is not exclusively caused by `30/2`. The newest alloc trace confirms `30/2`/`sub_1049188()` creates the first `25/5` object, while IDA `scene_runtime_tick()` (`0x01014E54..0x01014EEE`) queues the later one-shot sync family: direct `6/1`, `6/13`, `6/14`, then `send_game_event_type(101)` (`2/10`) and `send_default_scene_event()` (`25/5`).
  - `confirmed`: in `scene_runtime_tick()`, `R4` is loaded from `R9+0x5C64`, so the one-shot gate `[R4+1]` is `R9+0x5C65`.
  - `confirmed`: `R9+0x5C65` is set by `scene_runtime_init_and_sync()` at `0x01013010` before the second-map scene-change response is dispatched. This makes the WT44 one-shot a normal runtime-init request, not a malformed parser side effect.
  - current builder experiment:
    - confirmed negative: deferring all scene completion and later appending `30/1` causes bounce-back to the previous map.
    - confirmed negative: keeping `30/2` in the scene-change combo prevents bounce-back, but `30/2` closes the dispatch gate at `0x0103980E`, so the one-shot WT44 response still reaches `early_gate_off`.
    - confirmed negative: deferring `30/2` itself into the one-shot response also causes bounce-back to the previous map.
    - current behavior: scene-change completion is restored to the immediate combo response; the one-shot subset response is parser-safe task/other/banner only.
    - unresolved: early `30/2` commits the target map but closes the business gate before the one-shot WT44 response, leaving `R9+0x5530` set. The fix likely requires a different response contract or local completion signal, not moving scene completion into WT44.

### 2026-06-08 scene-change `2/3 mapID/exitID` response correction

- confirmed runtime mismatch from the first portal transfer:
  - the client requested `1/2/3` with `mapID=00蓬莱仙岛_02.sce` and `exitID=1`.
  - the old mock replied with a fixed `30/2.scene=c00蓬莱仙岛_01` and default `posinfo=(223,382)`.
  - after consuming that response, runtime stayed on the loading screen with `sceneState=1`, `loadFlags=1,0,0`, and repeated `trace_loading_overlay_call ... overlayState=3`.
- local resource evidence:
  - `tools/inspect_sce.py bin/JHOnlineData/00蓬莱仙岛_02.sce` shows the matching `entry_id=1` portal spawn at `(128,45)`.
  - the same `_02.sce` file is the descriptor already opened locally by `parse_actor_motion_descriptor()` before the network `2/3` request is sent.
- current mock behavior:
  - `mock_scene_change_combo_response` derives `30/2.scene` and `27/12.name/posinfo` from the requested `mapID/exitID`.
  - for the observed `00蓬莱仙岛_02.sce + exitID=1`, it returns scene `00蓬莱仙岛_02.sce` and `posinfo=(128,45)`.
  - the combo response now also mirrors the requested follow-up family as `27/12`, then `27/11`, then `27/4`, plus `7/42` when requested, instead of sending `27/11` before seeding `27/12` and omitting `27/4`.

### 2026-06-09 scene-change split `30/2` experiment

- status: `confirmed partial positive`
- static evidence:
  - `parse_scene_posinfo_field()` (`0x01039770`) reads `result`, `scene`, then optional `posinfo`.
  - only the `result==1 && posinfo present` branch applies the scene/position and closes `sceneObj+0x164` at `0x0103980E`.
  - the function still calls `sub_10491AE()` at `0x0103993C` even when `posinfo` is omitted, and `sub_10491AE()` reaches `sub_1049188()` / `alloc_outgoing_game_event(2,0,25,5)`.
  - `27/12` uses `ui_apply_named_posinfo_target()` (`0x0100E9B8`), which also closes the same dispatch gate at `0x0100EA6A`; it cannot stay in the early scene-change combo if the goal is to let the later WT44 response dispatch.
- current mock experiment:
  - immediate scene-change combo sends `30/2` with `result`, `type`, and `scene`, but no `posinfo`.
  - the same combo keeps requested non-closing objects such as `27/11`, `27/4`, and `7/42`.
  - the post-scene-change WT44 task subset response appends `27/12`, `27/11`, `27/4`, then full `30/2 result/type/scene/posinfo` using the remembered scene-change target.
- expected markers:
  - `mock_scene_change_combo_response ... sceneChangeResult=1 trailingSceneEnter=0 deferredSceneCompletion=1`
  - `mock_scene_task_subset_followup_response ... deferredSceneChangeResult=1 deferredSceneCompletion=1 lateDispatchExpected=0`
- success criteria:
  - no immediate map request back to the previous `c00蓬莱仙岛_01` target.
  - the WT44 response enters `net_business_response_dispatch()` with the dispatch gate still open.
  - `R9+0x5530` does not remain set until the `scene_widget_timeout_message` branch.

Newest runtime result:

- confirmed:
  - the immediate scene-change combo with `30/2 result/type/scene` and no `posinfo` is enough to avoid the bounce-back seen when `30/2` was omitted entirely.
  - the post-scene-change WT44 response dispatches with `dispatchGate=1`, consumes the deferred `27/12`, `27/11`, `27/4`, and full `30/2 scene+posinfo`, then clears `R9+0x5530`.
- remaining issue:
  - after WT44 completion, a short empty `25/5` default-scene request is sent. The mock's `25/12 result=4` response is parser-safe, but the emulator was delivering this one response reentrantly through `queue_data_event_immediate_flush`.
  - runtime trace shows the reentrant response first clears `R9+0x5531` at `0x010348F6`, then the still-running send wrapper writes `R9+0x5531` back to `1` at `0x010347E8`.
  - with that gate stuck, later local movement requests append `2/1 moveinfo` objects until `eventObj+0x10` reaches cap 10; a later scene init then cannot enqueue its normal `2/3`, `27/11`, `27/4`, or `7/42` requests.
- current mock timing:
  - short `25/5` responses remain enabled as `25/12 result=4`, but they are now queued like normal async network data instead of being immediately flushed in the same call stack.
  - status: `confirmed timing correction`; this is a mock network timing fix, not a client global/state patch.

Follow-up runtime confirmation:

- no `queue_data_event_immediate_flush` marker appears for short `25/5`.
- repeated scene changes now continue past the previous queue-cap blocker:
  - `c00蓬莱仙岛_03.sce exitID=0` is requested at tick `4653`.
  - `00蓬莱仙岛_02.sce exitID=0` is requested at tick `4864`.
- no `count=10 cap=10`, `scene_widget_timeout_message`, `unhandled_packet`, or `assert(0)` marker appears in the run.
- the remaining post-movement `early_gate_off` entries come from empty `2/1 moveinfo` / `2/10` acks after scene dispatch has closed; they are not confirmed blockers because later scene-change sends still succeed.

### 2026-06-09 short `7/18` item/use request

- observed request:
  - `WT len=9 hdr=7/18 objs=1/7/18 count=1`
  - runtime source: `sub_102C2E8` sends `alloc_outgoing_game_event(10, 1, 7, 18)` at `0x0102C2FE` after a HUD/touch action, then writes `R9+0x7A28+3=1`, `R9+0x7A28+4=1`, and `R9+0x7A28+2=0`.
- parser evidence:
  - the main kind-7 handler `net_handle_misc_player_fields()` (`0x01011C88`) does not handle subtype `18`.
  - the matching UI-local response parser is `sub_102C104` (`0x0102C104`), which handles kind `7` subtype `17`, `34`, and `4`.
  - for subtype `17`, it reads fields in this order: `result`, `useinfo` blob/string, `pcimg`. It then shows `useinfo` on `result == 1`; otherwise it shows the local `网络异常!` string.
- current mock:
  - `builtin-item-use18` responds to short `7/18` with `1/7/17 { result=1, useinfo="OK", pcimg=0 }`.
- status:
  - response field order is `confirmed` from IDA.
  - end-to-end consumption is `hypothesis`: in the latest failing trace the scene business dispatch gate was already closed (`sceneObj+0x164 == 0`), so the next run must verify whether this response is consumed or still reaches `early_gate_off`.

Newest runtime result:

- confirmed:
  - `builtin-item-use18` removes the previous unhandled/assert path. `bin/logs/net_packets.log` now records `source=builtin-item-use18 responseLen=48 WT len=9 hdr=7/18 objs=1/7/18 count=1`.
  - the response bytes are a parser-shaped `1/7/17` object (`result=1`, `useinfo=OK`, `pcimg=0`), matching `sub_102C104` field order.
  - the response is not consumed on this run: it enters `net_business_response_dispatch()` at tick `226` with `dispatchGate=0` and exits through `early_gate_off` at `0x01012F42`.
  - only the wrapper block gate `R9+0x5531` is cleared at `0x010348F6`; `R9+0x5530` remains `1`, and the loading strip continues with `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1`.
- implication:
  - the remaining problem is not a missing `7/18` mock response field. It is a delivery/dispatch sequencing problem for post-map HUD data while `sceneObj+0x164` is closed.
  - IDA `net_business_response_dispatch()` confirms that the scene-gate check occurs before packet unpacking/object iteration, so the UI-local `sub_102C104` parser cannot see the otherwise parser-shaped `7/17` object in this path.
- instrumentation:
  - `trace_business_dispatch_state` now also logs `R9+0x5D30` fallback callback plus the active `R9+0x5EF0` manager child/cb10, so the next run can distinguish a real fallback-channel contract from a normal object-loop response blocked by `early_gate_off`.

Newest correction after the practise-info panel run:

- confirmed:
  - the active manager child callback at the blocked response is `managerChildCb10=0x0102CB47`, i.e. `sub_102CB46`, not `sub_102C104`.
  - `sub_102CB46` handles kind `7` subtype `18` and reads fields in this order: `todaypasthour`, `todaypastmin`, `getexp`, `todaylasthour`, `todaylastmin`, `alllasthour`, `alllastmin`, `isgold`.
  - therefore the earlier `7/18 -> 7/17 { result/useinfo/pcimg }` response was a mismatch for the visible `修炼信息` panel.
- current mock:
  - `builtin-practise-info18` now responds with kind `7` subtype `18` and the confirmed practise fields, using zero current/today values and `alllasthour=2`.
  - the response remains on the normal business event `7` path. A narrow experiment queued this one response as event `6`, but the latest run proves that is not the correct transport contract.
- confirmed negative transport experiment:
  - packet log: `source=builtin-practise-info18 responseLen=151 WT len=9 hdr=7/18 objs=1/7/18 count=1`, and the response bytes contain the confirmed fields (`todaypasthour`, `todaypastmin`, `getexp`, `todaylasthour`, `todaylastmin`, `alllasthour`, `alllastmin`, `isgold`).
  - runtime trace: `queue_data_event ... event=6 len=151`, then `fire_event ... event=6`.
  - `net_business_response_dispatch()` is entered at tick `165` with `r3=6`, `dispatchGate=0`, `fallbackCb=05083027`, `managerChild=01058af8`, and `managerChildCb10=0102cb47`.
  - the dispatcher then logs only `fallback_exit`; no `trace_practise_info_parser` line appears.
  - therefore event `6` does not call the active manager-child parser `sub_102CB46` and does not clear `R9+0x5530`.
- remaining status:
  - confirmed: the `7/18` field shape is now aligned with `sub_102CB46`.
  - confirmed: fallback event `6` is not the delivery path for this response.
  - unresolved: the normal event `7` object loop is still blocked after map entry because `sceneObj+0x164 == 0`; the next investigation should target response ownership / scene dispatch-gate sequencing rather than changing the practise-info fields.

Follow-up numeric encoding correction:

- confirmed runtime evidence:
  - after the keep-gate response was fixed, the later `7/18` response entered `sub_102CB46`: `trace_practise_info_parser label=entry ... kind=7 subtype=18`.
  - the loading gate then cleared through the normal business-handler path, so the stuck progress strip disappeared.
  - however, the first `u16` encoding experiment displayed huge values; `after_fields` logged values such as `29807`, `26469`, `24940`, and `26995`, which correspond to ASCII/GBK-ish string fragments rather than the intended `0/2` minute/hour values.
  - `getexp`, which was encoded with the existing integer/u32 helper, remained sane.
- corrected mock behavior:
  - `builtin-practise-info18` now encodes all time fields with the normal integer/u32 object-value helper even though `sub_102CB46` stores them into 16-bit fields.
  - current synthetic values are:
    - `todaypasthour=0`, `todaypastmin=15`
    - `getexp=120`
    - `todaylasthour=1`, `todaylastmin=45`
    - `alllasthour=1`, `alllastmin=45`
    - `isgold=0`

Current first-scene gate experiment:

- evidence:
  - the first-scene WT49 response currently reaches the object loop while `sceneObj+0x164 == 1`.
  - in the newest trace, `27/12` closes that gate at `0x0100EA6A` before the later practise-info UI request; the trailing `30/1` then writes the already-zero gate again at `0x01039766`.
  - IDA confirms `ui_apply_named_posinfo_target()` (`0x0100E9B8`) closes the gate unconditionally, and `parse_scene_response_entry()` (`0x010396EA`) closes it when `posinfo` is present.
  - once closed, `net_business_response_dispatch()` checks the gate before packet unpacking, so the active manager-child parser cannot see later `7/18` responses.
- hypothesis under test:
  - the first-scene resource follow-up may be closing the business dispatch gate too early for post-map HUD/business responses.
- current mock experiment:
  - for the first-scene WT49 shape (`12/1 + 7/42` present), `mock_scene_resource_followup_response` defaults to `CBE_FIRST_SCENE_KEEP_BUSINESS_GATE=1`.
  - in that mode it omits the `27/12/27/11/27/4` fb-target trio and replaces the full `30/1 scene+posinfo` with a non-closing `30/2 result+scene` ack without `posinfo`.
  - this is a server-response experiment only; it does not patch the client gate or call the practise parser directly.
- next runtime markers:
  - positive: `mock_scene_resource_followup_response ... keepBusinessGate=1`, no close write at `0x0100EA6A/0x01039766` in the first-scene WT49 window, and later `7/18` reaches `trace_practise_info_parser`.
  - negative: map completion regresses, scene state/load flags stick, or the later `7/18` still reaches `early_gate_off`; in that case restore the old builder with `CBE_FIRST_SCENE_KEEP_BUSINESS_GATE=0` while continuing gate/ownership research.

### 2026-06-09 scene/menu `4/1` challenge interaction request

- observed request:
  - `WT len=60 hdr=4/1 objs=1/4/1(id=105) count=1`
  - fields: `id=105`, `index=1`, `posx=142`, `posy=310`
  - runtime trigger: map screen touch path (`activeScreen=01053f78`, `currentThis=01053f60`) after several handled `2/1 moveinfo` uploads; no loading widget gate was active (`gate5530=0` in the tail trace).
- static evidence:
  - `net_business_response_dispatch()` (`0x01012E4C`) routes kind `4` response objects to `net_handle_login_or_name_result()` (`0x0101258A`).
  - `net_handle_login_or_name_result()` handles subtype `14` by reading only `result` and mapping result values `2..7` to local duel/challenge failure messages; subtype `15`/`17` read `name`; subtype `1` is not a confirmed response parser path.
  - `sub_1037ED4()` (`0x01037ED4`) constructs the matching outgoing `1/4/1` object and writes `id`, `index`, `posx`, and `posy`. Its only direct static caller is `task_hall_activate_selected_entry()` action `13` (`0x01049370`), while the newest runtime touch also shows the scene/menu child callback family around `0x0101AA0B`.
- current mock:
  - `builtin-special-scene-interaction` still runs first for the known Taohuadao hotspot scene-transition IDs.
  - generic `builtin-challenge-interaction` recognizes the exact `1/4/1` request with `id/index/posx/posy`, plus an optional same-packet `1/2/1 moveinfo`.
  - generic challenge now returns a Battle.cbm-shaped `1/4/10 { side=1, battleinfo=<raw typed stream> }` response and appends the existing empty `1/2/1` ack when the request also carried `moveinfo`.
- status:
  - confirmed: outgoing request field names/order from runtime bytes and IDA `sub_1037ED4()`.
  - confirmed: `4/14 result` is only a local main-CBE failure/no-popup family, not a proven battle-start success path.
  - confirmed: Battle.cbm contains a kind-4 battle-start parser for subtype `10`.
  - hypothesis: the current mock's exact `side`, HP/MP fields, and one-player/one-enemy values are minimal parser-safe seeds, not yet runtime-confirmed gameplay semantics.

Battle.cbm `4/10 battleinfo` response shape:

- static evidence:
  - Battle.cbm instance `mwn9` is `bin/JHOnlineData/mmBattleMstarWqvga.cbm`.
  - `sub_17AC()` (`0x000017AC`) is the Battle.cbm event-7 dispatcher; its kind-4 branch calls `sub_7BD0()` from the `0x00001820` dispatch site.
  - `sub_7BD0()` (`0x00007BD0`) switches on response subtype. Subtypes `5` and `10` call `sub_66CC()` from the `0x00007DF0` path.
  - `sub_66CC()` (`0x000066CC`) reads object field `side`, then field `battleinfo`, initializes a raw stream over that blob, and stores the side at the Battle.cbm state slot around `R9+13488`.
  - `sub_66A4()` (`0x000066A4`) resolves each enemy id by scanning four local entries at `*(R9+13472)`, comparing the requested id against record offset `+36`.
- raw `battleinfo` stream order for subtype `10`:
  - `u8 ownCount`
  - for each own fighter: `u32 id`, four tagged `u32` stat-like fields, `string name`, `u8 visualVariant`, `u8 visualGroup`
  - `u8 enemyCount`
  - for each enemy: `u32 id`, four tagged `u32` stat-like fields
- current mock:
  - builds a one-own / one-enemy subtype-10 stream.
  - own fighter id is `10001`, name is the existing GBK role name used by actor/role mocks, visual bytes are `0,1` so the Battle.cbm `sub_23F6(second, first)` lookup does not use the `(0,0)` negative-index case.
  - enemy id is copied from the `4/1.id` request field so `sub_66A4()` can match the local encounter table when the id is present there.
  - the `battleinfo` object value is encoded as a raw object entry, not with the generic blob helper's inner `u16 dataLen` prefix. Battle.cbm `sub_66CC()` (`0x00006704`/`0x00006714`/`0x00006722`) treats the returned field pointer itself as the stream base and immediately reads `ownCount`.
  - the same response currently appends a minimal Battle.cbm status object `1/4/7` after `1/4/10`. This is a `hypothesis` for the current crash, but its field names come from confirmed parser reads in `sub_743C()` (`0x0000743C`), which is the subtype-7 branch reached from `sub_7BD0()` at `0x0000806C`.
- runtime evidence:
  - `bin/logs/storage_trace.log` from the current workstream shows `JHOnlineData/mmBattleMstarWqvga.cbm` being opened/read, followed later by battle-adjacent asset reads such as `skill.dsh`.
  - the new response logs as `mock_challenge_interaction_response response=4/10 side=1 battleinfoEncoding=raw-typed-stream battleinfoLen=... evidence=Battle.cbm:sub_7BD0/sub_66CC hypothesis=stat_semantics`.
- status:
  - confirmed: parser field names and raw stream read order from Battle.cbm IDA.
  - hypothesis pending manual runtime: whether the generic encounter should receive this packet immediately after `4/1`, and whether the seeded stat fields are sufficient to enter a stable battle screen.

Latest runtime result with generic `4/10`:

- confirmed runtime evidence:
  - `bin/logs/net_packets.log` latest session sends `WT len=60 hdr=4/1 objs=1/4/1(id=3) count=1` and receives `source=builtin-challenge-interaction responseLen=119`.
  - the response payload was `1/4/10 { side=1, battleinfo=<83 bytes> }`, but the old builder encoded that field through `vm_net_mock_put_object_blob()`, producing an extra inner `00 53` length prefix before the typed stream.
  - `bin/logs/net_trace.log` then shows `trace_business_dispatch_item ... kind=4 subtype=10` at tick `164`, so the object reaches the client business dispatcher.
  - read-only Battle.cbm state trace after the parser shows `trace_battle_module_state ... side=1 ownCount=0 enemyCount=0 subtype=10 parseOk=1 ... ownHead=00000000... enemyHead=00000000...`; this confirms the parser entered the battle-start path but did not populate either side's fighter list.
  - after that dispatch, the client emits a standalone `WT len=19 hdr=2/10 objs=1/2/10 count=1` with field `Type=100` (`send_payload ... 04547970650003000164`).
  - superseded: the old mock answered that follow-up through `builtin-actor-other-only10` with empty `1/2/10 { othernum=0, otherinfo=<empty> }`.
  - the same run reaches the battle screen, but later logs `trace_scene_message_request ... text=你已经死亡不能使用` after a manual touch and shows no player/monster sprites; this is consistent with the confirmed zero fighter counts.
- static evidence for the likely next response family:
  - Battle.cbm `sub_8996()` (`0x00008996`) handles response `25/2` by reading `result` and `type`; if `result=1,type=1`, it calls the main bridge with value `100`.
  - the same function also handles `25/3`, `25/4`, `25/8`, and `7/19`, so the `2/10 Type=100` follow-up may expect a non-`2/10` battle/status response object rather than the generic actor-other empty shell.
- current instrumentation:
  - `mock_actor_other_only10_response` logs the request `Type` field.
  - new read-only runtime trace `trace_battle_module_state` watches Battle.cbm `sub_66CC` entry/return (`0x050866CC`, `0x05086BEC`), `sub_7BD0` after the `4/10` parser (`0x05087DF4`), and `sub_8996` / its `25/2 type=1 -> Type=100` site (`0x05088996`, `0x050889F0`).
- status:
  - confirmed: `4/10` is accepted far enough to transition into Battle.cbm state, enter the battle UI, and trigger the next `Type=100` request.
  - confirmed negative: double-wrapped `battleinfo` is not accepted as a fighter stream; it leaves `ownCount=0` and `enemyCount=0`.
  - corrected mock: `battleinfo` now uses `vm_net_mock_put_object_raw()`, and `mock_actor_other_only10_response` reads request `Type` as u8.
  - superseded Type=100 experiment: a standalone `2/10 Type=100` request received `1/25/2 { result=1, type=1 }`, logged as `mock_actor_other_type100_battle_sync_response`.
  - latest runtime result: the `25/2` response reaches the main business dispatcher as `kind=25 subtype=2`, then the fallback callback enters Battle.cbm `sub_17AC()` (`battle_event7_dispatch_entry_sub_17AC`), but no `sub_8996_entry` or `sub_8996_25_2_type1_send_type100` probe fires.
  - confirmed negative: filtered `trace_battle_sub17ac_flow` shows the `25/2` packet takes the early `sub_17AC` path through `0x17F8`, then jumps to the common tail (`0x17FA` unconditional branch toward the `0x1EE4` tail); it never reaches the later kind-specific dispatch block where `kind=4` calls `sub_7BD0()` at `0x1820`.
  - superseded Type=100 experiment: the mock answered `2/10 Type=100` with `1/4/7` battle status, logged as `mock_actor_other_type100_battle_status_response`. Evidence for this experiment was that `sub_17AC()` dispatches `kind=4` responses into `sub_7BD0()`, and `sub_743C()` is the confirmed `4/7` parser for battle status plus optional `combatinfo/info/fbs/fdata`.
  - confirmed negative: the newest manual run proves the current empty `4/7` status shell is unsafe as the Type=100 follow-up. `bin/logs/net_packets.log` shows `WT len=19 hdr=2/10` with `Type=100`, followed by mock response `1/4/7` length `244`; `bin/logs/net_trace.log` then shows `trace_business_dispatch_item ... kind=4 subtype=7`, `sub_743C_status7_entry`, `sub_743C_iteminfo_read`, `sub_743C_item_stream_init`, and `sub_743C_crash_lastpc_candidate`.
  - crash evidence: `bin/logs/stdout_trace.log` reports invalid access `地址无法访问:4255de5a type:0 size:19 value:1`, with `pc=05189178`, `lastPc=05189178`, `lr=05189931`, `r9=01050bd0`; using the Battle.cbm runtime code base `0x05181F20`, that maps to offset `0x7258` after the `sub_743C()` iteminfo stream path.
  - corrected mock: `2/10 Type=100` is rolled back to parser-safe `1/2/10 { othernum=0, otherinfo=<empty> }`, logged as `mock_actor_other_type100_empty10_response`. This is only a no-crash fallback; the true Type=100 battle continuation remains unresolved.
  - trace-only instrumentation: `hook_vm_pool_code_callback()` records the Battle.cbm `sub_17AC` event-7 dispatch window (`0x17AC..0x1850`) as `trace_battle_sub17ac_flow` only when the incoming WT packet is `kind=25 subtype=2`.
  - latest manual-run evidence: the filtered flow proves the old `25/2` experiment exits before Battle.cbm's kind-specific parser, and the newer `4/7` experiment reaches `sub_743C()` but crashes in the iteminfo stream path. Future Type=100 work should look for another real battle continuation/parser field shape, not reuse the current empty `4/7` shell as default.

Raw `battleinfo` follow-up crash and subtype-7 status hypothesis:

- confirmed runtime evidence:
  - after the raw-field correction, `net_packets.log` shows the challenge response is now `WT len=117` with `battleinfo` field length `0x0053` followed immediately by typed stream bytes `00 01 01 ...`; the old inner `00 53` prefix is gone.
  - the client still crashes immediately after the battle transition. `stdout_trace.log` reports `地址无法访问:0 type:0 size:21 value:4`, with `pc=0`, `lr=0x0508760D`, and `lastPc=0x0508760A`.
  - IDA maps runtime `0x0508760A` to Battle.cbm `sub_743C()` around `0x0000760A`. `sub_743C()` is called from `sub_7BD0()` subtype `7` at `0x0000806C`, not from the subtype `10` battleinfo parser.
  - `sub_743C()` reads confirmed fields `lastexp`, `curexp`, `persentexp`, `energy`, `energymax`, `gold`, `level`, `result`, `bagstatus`, `hp`, `mp`, `itemnum`, `iteminfo`, `combatinfo`, `autorevive`, `info`, and `fbs`.
- current mock:
  - generic challenge now appends `1/4/7` after `1/4/10`.
  - minimal `4/7` fields currently provided are `lastexp=0`, `curexp=0`, `persentexp=0`, `energy=100`, `energymax=100`, `gold=0`, `level=1`, `result=1`, `bagstatus=0`, `hp=<battle role hp>`, `mp=<battle role mp>`, `itemnum=0`, empty raw `iteminfo`, and `autorevive=0`.
- status:
  - confirmed: raw `battleinfo` fixes the wire encoding but exposes a later Battle.cbm status/init contract.
  - hypothesis pending rerun: `4/10 + 4/7` in the same WT response is the missing battle-screen initialization sequence; if it still crashes, add trace around `sub_743C()` field reads before changing packet semantics further.

Follow-up crash with `4/10 + 4/7` and corrected Battle.cbm trace hook:

- confirmed runtime evidence:
  - after appending the minimal `4/7`, the latest rerun still crashes on battle entry.
  - `bin/logs/net_packets.log` shows `source=builtin-challenge-interaction responseLen=320` for `WT len=60 hdr=4/1 objs=1/4/1(id=3)`.
  - the response contains two objects: `1/4/10` with raw `battleinfo`, followed by `1/4/7` status fields.
  - `bin/logs/net_trace.log` shows the main dispatcher consumed both objects: `trace_business_dispatch_item ... kind=4 subtype=10`, then `trace_business_dispatch_item ... kind=4 subtype=7`.
  - immediately after that window, `stdout_trace.log` still reports `地址无法访问:0 type:0 size:21 value:4`, `pc=0`, `lr=0x0508760D`, `lastPc=0x0508760A`.
- corrected static/runtime address mapping:
  - Battle.cbm is executing from runtime base `0x05080000` in this run.
  - runtime `0x0508760A` maps to Battle.cbm IDA offset `0x0000760A`, inside `sub_743C()` after the `iteminfo` stream setup block.
  - runtime `0x0508440E` maps to Battle.cbm IDA offset `0x0000440E`, inside `sub_31FA()`, not to IDA `0x00008440`; earlier notes using the latter mapping should be treated as superseded.
- instrumentation correction:
  - negative evidence: no `trace_battle_module_state` lines appeared in the crashing rerun, even though the main dispatcher delivered `4/10` and `4/7`.
  - cause: dynamic CBM code is covered by `hook_vm_pool_code_callback()`, while the previous absolute `0x05087xxx` Battle.cbm probes were placed only in the normal `hookCodeCallBack()` path.
  - corrected code now records `trace_battle_pool_probe` from the VM-pool hook for Battle.cbm offsets including `sub_17AC()` (`0x000017AC`), `sub_66CC()` (`0x000066CC`), `sub_66A4()` (`0x000066A4`), `sub_7BD0()` (`0x00007BD0`), `sub_743C()` (`0x0000743C`), `sub_7794()` (`0x000078C6`), and `sub_8996()` (`0x00008996`).
- follow-up instrumentation correction:
  - latest rerun still produced no `trace_battle_pool_probe` lines.
  - runtime evidence explains the miss: `runtime_state ... module=00000000` appears immediately before dispatch, while `trace_business_dispatch_state` reports `fallbackCb=05083605`.
  - `hook_vm_pool_code_callback()` was still returning early because `g_currentScreenModuleBase` was unset. The first correction used inferred base `0x05080000` only for read-only Battle.cbm trace offsets in the observed `0x05080000..0x050A0000` range.
  - that trace-only correction exposed the R9 mismatch described below.
- confirmed Battle.cbm R9 ABI issue:
  - latest trace with the inferred-base probe shows `trace_battle_pool_probe ... battle_screen_callback_3604 pc=05083604 off=3604 ... moduleBase=05080000 ... r9=01050bd0`.
  - that means Battle.cbm code was executing with the main CBE R9, not Battle.cbm's module base.
  - later in the same run, the battle state already had `ownCount=1`, `enemyCount=1`, `subtypeState=10`, and `parseOk=1`, so the raw `4/10.battleinfo` stream is accepted as one player and one enemy before the crash.
  - crash-adjacent probes then show invalid module-local callback state while entering the `4/7`/status path, e.g. `sub_743C_iteminfo_read pc=05087592 ... R2=00000001` before the `BLX R2` callsite, followed by the unchanged `pc=0`, `lr=0508760D`, `lastPc=0508760A` crash.
  - IDA evidence: Battle.cbm `sub_31FA()` and `sub_743C()` read module-local objects and callbacks via `R9+0x2050`, `R9+0x204C`, `R9+0x3D6C`, etc. Those offsets are not valid against main-CBE `R9=01050bd0`.
- corrected platform contract:
  - the existing VM-pool hook already repairs R9 when `g_currentScreenModuleBase` is known.
  - the Battle.cbm fallback path reaches `0x05083605` before that module base is recorded. The first repair incorrectly treated `0x05080000` as both code base and R9.
  - follow-up runtime evidence disproves that simplification: after `main_r9_restore` became active, the crash moved to Battle.cbm `sub_1F24()` at runtime `pc=05081F5C` / IDA offset `0x1F5C`, with invalid access `地址无法访问:11082040` and `r0=1108203c`.
  - IDA evidence: `sub_1F24()` starts by reading module tables via `R9+0x204C` / `R9+0x2050`; using code base `0x05080000` as R9 makes `R9+0x204C` land in the code/header area and produces the bad pointer.
  - file/header evidence: `storage_trace.log` shows `JHOnlineData/mmBattleMstarWqvga.cbm` loaded before the crash. Parsing the CBM header gives `headerInt2=0x14000`, `headerInt4=0x4600`, `codeLen=0x13544`, so with code loaded at `0x05080000`, the Battle module R9/data base is `0x05094000`.
  - corrected contract: the hook now uses `codeBase=0x05080000` only for IDA offsets/tracing, and restores `R9=0x05094000` for Battle.cbm code in `0x05080000..0x05094000`.
  - this logs `pool_r9_fix_inferred_battle ... codeBase=05080000 ... evidence=Battle.cbm_headerInt2_0x14000`; it does not change packet contents, skip Battle.cbm parser code, write CBE globals, or force battle-screen state.
- follow-up dynamic-screen `this` correction:
  - runtime evidence after `R9=0x05094000` shows the next crash moved to Battle.cbm `sub_31FA()` at runtime `pc=05083DDE` / IDA offset `0x3DDE`, with `r9=05094000`, `r0=0`, and invalid access `地址无法访问:1c`.
  - this run did not reach a `4/1` encounter request; `net_packets.log` only contains role-select and startup follow-ups (`1/6`, `5/10+7/7`, `7/7 type=2`, `7/7 type=3`). The crash is therefore Battle.cbm screen initialization during scene startup, not a new battle packet parser result.
  - screen-manager evidence: `screen_manager idx=4 add r0=050973a8 ... moduleBase=05094000` and `screen_manager idx=4 module_context this=05097390 r9=05094000`, but the later scheduler line was `screen_func_table screen=050973a8 this=00000000 init=05083d7d ...`.
  - conclusion: the screen stack knew the dynamic Battle screen owner (`screen-0x18 = 05097390`), but the scheduler only computed `this` for main-CBE screen tables. It called Battle.cbm init with `R0=0`, leading to the null-object read in `sub_31FA()`.
  - corrected platform contract: when the active screen function table is present in the screen stack with a nonzero module base, the scheduler now computes `this=screenFuncPtr-0x18`, restores that module context for screen calls, and logs `screen_func_dynamic_this`.
- follow-up after dynamic-screen `this` correction:
  - latest runtime evidence confirms the scheduler now applies the dynamic owner: `net_trace.log` shows `screen_func_dynamic_this screen=050973a8 this=05097390 moduleBase=05094000`, followed by `screen_func_table screen=050973a8 this=05097390 init=05083d7d ...`.
  - the crash still occurs in the same Battle.cbm init window. `stdout_trace.log` reports `地址无法访问:1c type:0 size:19 value:4`, `pc=05083DDE`, `lr=0103498B`, `r9=05094000`.
  - latest storage evidence shows `JHOnlineData/mmBattleMstarWqvga.cbm` is opened at the beginning of this run even though `net_packets.log` contains no `4/1` encounter request. Directory/header checks show `JHOnlineData/MMORPGTempcbm` still matches `mmGameMstarWqvga.cbm` in size/header, so this is not explained by the old temp-CBM cache simply containing Battle.cbm.
  - IDA evidence: Battle.cbm `sub_31FA()` (`0x000031FA`) around `0x00003DBC..0x00003DD4` calls a main-CBE bridge through `*(R9+0x2018)->+0x18` twice, then continues at `0x00003DDC..0x00003DE4` by reading stack-derived pointers/counters. Since `0x00003DDE` itself is `SUBS R3,#0xFF`, the bad read at `0x1c` is treated as a return-side/callback-context hypothesis until the new trace confirms the exact source.
  - instrumentation: `trace_battle_pool_probe` now records the `sub_31FA()` `0x3D92..0x3E10` window, including `R9+0x2018` bridge callbacks, `R9+0x2040` draw callback, and stack slots `SP+0x65C`, `SP+0x680`, `SP+0x72C`. This is trace-only and does not patch Battle.cbm logic or packet contents.
- follow-up after the `sub_31FA` crash-window probes:
  - runtime evidence now shows the module data changes during the second bridge call. Before `0x00003DD4`, `trace_battle_pool_probe ... sub_31FA_bridge_second_r0_zero_3DD2` still reports `bridge=0a001400 cb18=0c001018`, `streamMgr=0105617c`, and sane zeroed Battle state. After returning to `0x00003DDC`, the same probe reports `bridge=00000000`, `streamMgr=01670167`, and garbage Battle-state counters (`counts=7187,7187`, `subtypeState=1469`).
  - IDA/runtime evidence: the second bridge call returns through main CBE `sub_103496C()` (`0x0103496C`, runtime return `lr=0103498B`) after `scene_runtime_init_and_sync()` (`0x01012FB4`, caller near `0x010138B4`) queues the initial `1/7/42` sync request.
  - conclusion: the remaining crash is now narrowed to a Battle.cbm module-data overwrite/reuse during the cross-module callback window, not to `4/10`/`4/7` field order.
  - instrumentation: `trace_battle_module_data_write` now watches writes to Battle.cbm data range `0x05094000..0x05098600`, including `R9+0x2018`, `R9+0x2040`, `R9+0x204C`, `R9+0x2050`, `R9+0x3450`, `R9+0x374C`, and `R9+0x4058`. This is trace-only and records the writer `pc/lr/last` for the next rerun.
- follow-up after the Battle.cbm data-write trace:
  - runtime evidence: the latest data-write trace shows main CBE code at `pc=0104D328/0104D32C` clearing VM memory from `writeAddr=05094000` upward while active screen context is still the main scene (`activeScreen=0105a814/currentThis=010561ec`). The first traced writer has registers consistent with a memset-like block clear beginning at `r0=05093ff8`, `r1=00001e40`; that block overlaps Battle.cbm's data base.
  - runtime evidence after the overwrite: `trace_battle_pool_probe ... sub_31FA_after_bridge_second_3DDC` reports the previously sane `R9+0x2018` bridge and `R9+0x204C` stream-manager slots are now zero/garbage, followed by the same `stdout_trace.log` crash at `pc=05083DDE`, `lr=0103498B`, invalid read `0x1c`.
  - allocator evidence: `src/config.h` maps the ordinary `vm_malloc` pool at `VM_Memory_Pool_ADDRESS=0x05000000` with `VM_MemoryBlock_SIZE=0x400000`, so Battle.cbm code/data (`0x05080000..0x05098600` from the CBM header evidence above) sit inside the emulator's allocatable heap unless explicitly reserved.
  - corrected platform contract: `InitVmMalloc()` now reserves `0x05080000..0x050A0000` from ordinary `vm_malloc` use. This protects the confirmed Battle.cbm code/data window without patching Battle.cbm logic, forcing battle globals, or changing packet contents.
  - instrumentation correction: `trace_battle_module_data_write` now logs only writes that overlap the named Battle.cbm data slots/ranges, so broad clears no longer exhaust the trace budget before the key slots are reached.
  - status: hypothesis pending manual rerun. The expected next evidence is no `pc=0104D328/0104D32C` zeroing of `0x05094000..0x05098600`; if the crash persists, the next trace should identify another allocator/window or a later Battle.cbm contract.
- follow-up after reserving the first Battle.cbm window:
  - runtime evidence: the latest crash no longer occurs at `pc=05083DDE`. `stdout_trace.log` now reports `pc=0`, `lr=050A76D5`, `lastPc=050A76D2`, with `r9=01050bd0`.
  - runtime evidence: the active dynamic screen is still Battle-side code. `net_trace.log` shows `screen_func_table screen=01053f78 this=01053f60 init=050A3E45 destroy=050A2E0F logic=050A2D0B render=050A251B ...`, and the screen was registered from `last=050A40E8`.
  - file/static evidence: disassembling `bin/JHOnlineData/mmBattleMstarWqvga.cbm` with code base `0x05094000` maps `0x050A3E45` to Battle.cbm offset `0x0000FE45`, inside valid code. The same base maps `0x050A76D2` to offset `0x000136D2`, just past the code payload (`codeLen=0x13544`) in zero padding / post-code area.
  - conclusion: after the memory reservation, Battle.cbm can execute from a later code window (`0x05094000..0x050A8000`) and its module R9 must shift to `0x050A8000` (`codeBase + headerInt2 0x14000`). The previous inferred R9 repair only covered the old `0x05080000..0x05094000 -> R9=0x05094000` window, so the new screen was registered with main CBE `R9=01050bd0`.
  - corrected platform contract: the VM-pool hook now recognizes both observed Battle.cbm code windows: `0x05080000..0x05094000 -> R9=0x05094000` and `0x05094000..0x050A8000 -> R9=0x050A8000`.
  - allocator correction: `InitVmMalloc()` now reserves `0x05080000..0x050C0000` so the observed Battle.cbm code/data windows, including the shifted `0x050A8000..0x050AC600` data range, are not handed out as ordinary VM heap blocks.
  - instrumentation correction: `trace_battle_module_data_write` now watches both Battle.cbm data windows (`0x05094000..0x05098600` and `0x050A8000..0x050AC600`) and logs the active data base.
  - status: hypothesis pending manual rerun. Expected evidence is `pool_r9_fix_inferred_battle ... codeBase=05094000 ... -> 050A8000`, followed by `screen_manager idx=4 ... moduleBase=050A8000` for the Battle screen.
- follow-up after the shifted-window rerun:
  - runtime evidence: the next session again moved the same failure shape by `+0x20000`. `stdout_trace.log` now reports `pc=0`, `lr=050C76D5`, `lastPc=050C76D2`, `r9=01050bd0`.
  - runtime evidence: `net_trace.log` shows the Battle screen table at `screen=01053f78` with `init=050C3E45`, `destroy=050C2E0F`, `logic=050C2D0B`, `render=050C251B`, `pause=050C23E1`, and `resume=050C2375`; `vmAddScreen` still recorded `moduleBase=01050bd0` because the fixed-window R9 repair did not recognize this shifted code base.
  - file/static evidence: disassembling `mmBattleMstarWqvga.cbm` with `codeBase=0x050B4000` maps `0x050C3E45` to the same Battle screen-init offset and maps `0x050C76D2` to the same post-code zero-padding offset as prior runs. This confirms the module moved again, while the logical Battle.cbm offsets did not.
  - corrected platform contract: the emulator now infers Battle.cbm module context from the screen function table itself. If the table entries match the recovered Battle offsets (`init +0xFE44`, `destroy +0xEE0E`, `logic +0xED0A`, `render +0xE51A`, `pause +0xE3E0`, `resume +0xE374`), it sets `codeBase=init-0xFE44` and `moduleR9=codeBase+0x14000`.
  - the inference is applied at both platform boundaries that need it: `screen_manager idx=4` uses it before recording the screen stack module base, and `hook_vm_pool_code_callback()` uses it for runtime R9 repair while executing inside the inferred code window.
  - instrumentation correction: `trace_battle_module_data_write` also uses the inferred active Battle screen table to watch the corresponding shifted data range (`moduleR9..moduleR9+0x4600`) instead of relying only on fixed addresses.
  - allocator correction: the reserved VM heap window is widened to `0x05080000..0x05180000` to keep observed shifted Battle.cbm code/data windows away from ordinary `vm_malloc` reuse. This remains a runtime memory-contract fix, not a game-state patch.
  - status: hypothesis pending manual rerun. Expected evidence is `screen_manager idx=4 infer_battle_module ... r9=01050bd0 -> <codeBase+0x14000>`, followed by `screen_manager idx=4 ... moduleBase=<codeBase+0x14000>`.
- follow-up return-side ABI correction:
  - after enabling `pool_r9_fix_inferred_battle`, the crash moved from Battle.cbm `sub_743C` to main CBE `sub_1000674()`.
  - runtime evidence: `stdout_trace.log` reports `地址无法访问:bd302018`, `pc=0100067E`, `lr=05081ED5`, `r9=05080000`.
  - IDA evidence: main CBE `sub_1000674()` (`0x01000674`) uses `R9+0x4D5C` and dereferences the loaded object's callback at `0x0100067E`. This requires main `Global_R9`, not the Battle.cbm module base.
  - corrected platform contract now restores `R9=Global_R9` when normal CBE ROM code (`0x01000000..0x02000000`) runs with a non-main R9. This logs `main_r9_restore`.
- follow-up after screen-table Battle inference:
  - runtime evidence: latest `net_trace.log` confirms the screen-table inference now fires for the current shifted module: `screen_manager idx=4 infer_battle_module screen=01053f78 codeBase=05174000 r9=01050bd0 -> 05188000 ... evidence=Battle.cbm_screen_table_offsets`, followed by `pool_r9_fix_inferred_battle` at Battle.cbm offsets `0x100F2`, `0x100F8`, `0x10100`, and `0x10108`.
  - crash evidence: latest `stdout_trace.log` reports `地址无法访问:47901c30 type:0 size:19 value:4`, with `pc=05184110`, `lr=05184109`, `r9=05188000`, `r0=47901c30`.
  - static evidence: disassembling `bin/JHOnlineData/mmBattleMstarWqvga.cbm` with `codeBase=0x05174000` maps `0x05184110` to Battle.cbm offset `0x00010110`. The instruction at that exact offset is `ldr r0, [sp,#0xc]`; nearby code has just called the module-local callback at `*(R9+0x2044)->+0x18` twice and is computing layout coordinates before loading a draw callback through a module-local object at `0x00010122..0x00010136`.
  - interpretation: because `0x00010110` itself is only a stack load, the bad address `0x47901c30` is currently treated as a return-side or callback-produced pointer hypothesis rather than proof of a new `4/10`/`4/7` field-order error.
  - instrumentation: `trace_battle_pool_probe` now records the `0x000100D8..0x00010136` window, including `SP+0x0C`, `SP+0x24`, `SP+0x3C`, `SP+0x40`, and Battle.cbm R9 slots `+0x2038`, `+0x2044`, `+0x2048`, `+0x204C`, `+0x2080`. This is trace-only and does not patch Battle.cbm logic, write Battle globals, or alter packets.
- follow-up from the new layout-window trace:
  - runtime evidence: at the first new probe (`battle_render_layout_cb18_first_call`, `pc=051840EC`), before either local callback returns, `R9+0x204C` is already `47901c30`; adjacent module slots are also instruction-shaped values (`R9+0x2018=a1456d62`, `R9+0x2040=44481c29`, `R9+0x2038=f0081c30`, `R9+0x2044=30ff6800`).
  - file/header evidence: parsing `bin/JHOnlineData/mmBattleMstarWqvga.cbm` with the local CBE header logic yields `headerInt2=0x14000`, `headerInt4=0x4600`, `codeLen=0x13544`, `BssDataLen=0x2000`, `RwDataLen=0x0A2F`.
  - file-content evidence: if `R9=codeBase+0x14000`, then `R9+0x204C` should land in the RW data region near `fileOffset=BssDataOffset+0x204C`, whose bytes do not match the runtime `30 1c 90 47` value. The runtime value instead looks like code halfwords, so the current failure is now narrowed to dynamic-CBM data loading/placement or a pre-execution overwrite, not a Battle packet parser result.
  - instrumentation: `vm_cbfs_vm_file_read()` now logs `file_read_guest_write` for `.cbm` reads, including guest buffer pointer, requested/read size, `pc/lr/last`, and call registers. The next run should show exactly where the Battle.cbm code, BSS, and RW reads are copied.
- follow-up after `.cbm` file-read guest-buffer trace:
  - runtime evidence: `storage_trace.log` now shows Battle.cbm code is read to `buffer=05181f20` with `requested=0x13544`, while the following BSS read is to `buffer=01050bd0` with `requested=0x2000`.
  - runtime evidence: the screen table still gives a virtual code base of `05174000` from `init=05183e44` / offset `0xFE44`. Therefore the code-base used for IDA offset mapping is not the same as the guest buffer used for module data.
  - conclusion: the previous `moduleR9=codeBase+0x14000` repair was wrong for this dynamic loader path; it placed R9 inside the code-read buffer (`05188000`), which exactly explains why `R9+0x204C` read instruction-shaped data.
  - corrected platform contract: Battle.cbm screen-table inference is now used only for `codeBase` / IDA offset mapping. The dynamic module R9 is preserved from the loader/screen-stack value (`01050bd0` in this run), and VM-pool R9 repair restores that loader R9 rather than `codeBase+headerInt2`.
- status:
  - confirmed negative: the current minimal `4/7` status shell is not sufficient while Battle.cbm runs with the wrong R9.
  - confirmed: raw `4/10.battleinfo` now parses far enough to produce one own fighter and one enemy.
  - confirmed: bidirectional R9 repair and dynamic-screen `this` recovery both fire in the latest run.
  - hypothesis pending rerun with the new `sub_31FA` probes: the remaining crash is a cross-module callback/stack-context contract during Battle screen initialization, before any new `4/1` encounter request in that run.

Latest rerun after preserving the dynamic loader R9:

- runtime evidence:
  - `storage_trace.log` confirms the dynamic loader copies Battle.cbm code to `buffer=05181f20` and BSS to `buffer=01050bd0`; the newest Battle probes now run with `r9=01050bd0`, not the older wrong `05188000`.
  - `net_packets.log` shows the encounter request remains `WT len=60 hdr=4/1 objs=1/4/1(id=3)`, and the mock response was the two-object `4/10 + 4/7` shape with `responseLen=320`.
  - `net_trace.log` confirms both objects were consumed in the same dispatch window: `trace_business_dispatch_item ... kind=4 subtype=10`, then `kind=4 subtype=7`.
  - after the parser window, Battle.cbm writes sane fighter entries under data base `01050bd0`; examples include own id `0x2711` and player hp/maxhp `120/120`, followed by enemy id `3` and enemy hp/maxhp `80/80`.
  - the same run opens death resources (`JHOnlineData/f_death.actor`, `siwang1.gif`) before the final crash, matching the user-visible "already dead" battle state.
  - final crash evidence from `stdout_trace.log`: `pc=0`, `lr=051876D5`, `lastPc=051876D2`, `r9=01050bd0`. With current screen-table `codeBase=05174000`, `lastPc` maps to Battle.cbm offset `0x136D2`, which is past `codeLen=0x13544` and disassembles as zero padding.
- conclusion:
  - confirmed negative: appending the current minimal `1/4/7` status object in the same WT response as the start `1/4/10` is not a safe battle-start contract. It correlates with Battle.cbm entering the death-resource path and then jumping through/into an uninitialized post-code callback area.
  - confirmed: the earlier wrong-R9 hypothesis is resolved for this run; the remaining failure happens with loader R9 `01050bd0` and after `4/7` consumption.
- corrected mock:
  - generic `builtin-challenge-interaction` now returns only `1/4/10 { side=1, battleinfo=<raw typed stream> }` plus an optional same-packet empty `1/2/1` movement ack.
  - the subtype-7 status builder remains documented as a parser shape recovered from `sub_743C()`, but it is no longer sent in the initial encounter response by default.
- status:
  - hypothesis pending rerun: without same-window `4/7`, the client should either continue with the earlier `2/10 Type=100` follow-up or expose the next Battle.cbm-specific response family, instead of immediately taking the death/status path.

Latest `4/10`-only rerun correction:

- confirmed runtime evidence:
  - the latest `4/1` encounter request (`id=3,index=3,posx=173,posy=272`) receives only `1/4/10 { side=1, battleinfo=<raw typed stream> }`; no same-window `4/7` object is present.
  - `trace_business_dispatch_item` consumes only `kind=4 subtype=10`.
  - Battle.cbm still parses the raw stream far enough to populate fighter records under loader data base `01050bd0`, including own id `10001`, own hp/maxhp `120/120`, enemy id `3`, and enemy hp/maxhp `80/80`.
  - after `4/10`, Battle.cbm allocates an outgoing game event that matches the earlier `2/10 Type=100` follow-up, but the process crashes before the packet is sent/handled.
  - final crash remains `stdout_trace.log`: `pc=0`, `lr=051876D5`, `lastPc=051876D2`, `r9=01050bd0`.
- corrected static mapping:
  - `storage_trace.log` shows the actual Battle.cbm code read buffer is `05181f20` with length `0x13544`; BSS/data is `01050bd0`.
  - disassembling with `codeBase=05181f20` maps `lastPc=051876D2` to offset `0x57B2`, an actual `BLX R3` in the fighter render loop.
  - the older mapping that used screen-table virtual `codeBase=05174000` and treated `0x051876D2` as post-code padding is superseded for this crash.
- render-loop field evidence:
  - the block at actual offsets `0x577C..0x57C0` computes `fighter = table + r7 * 0xC4`.
  - it checks an active flag through `[fighter + 0x544]`.
  - if active, it reads frame bytes from `[fighter + 0x5C7]` and signed `[fighter + 0x5C8]`.
  - it then calls callbacks loaded from `[fighter + 0x584]` at `0x57B2` and `[fighter + 0x588]` at `0x57BE`.
- conclusion:
  - confirmed negative: removing the same-window `4/7` status object does not complete battle entry; `4/10` alone still reaches an active fighter render path with an uninitialized/null callback.
  - correction: death-resource opens are no longer proven to be caused by `4/7`, because they still appear in a `4/10`-only run.
  - hypothesis: the missing contract is either a Battle.cbm actor/effect visual initializer driven by the `battleinfo` visual bytes/resource lookup, or an early Battle.cbm-specific follow-up response needed before first render, likely related to the pending `2/10 Type=100`.
- instrumentation:
  - `trace_battle_pool_probe` now records actual-code-buffer probes at offsets `0x57A6`, `0x57A8`, `0x57BE`, `0x57CE`, `0x57D2`, `0x57DC`, and `0x57DE`, plus visual-init probes around `0x68F8..0x6900`.
  - each probe logs `fighterSlot`, `active544`, `cb584/cb588/cb58c`, and frame bytes from `R6+0x5C7/+0x5C8`.
  - the generic challenge response marker now uses `runtime_negative=4_10_only_render_null_callback_pending`.

Enemy id lookup correction:

- confirmed runtime evidence:
  - own slot render setup is nonzero: `trace_battle_pool_probe ... battle_render_fighter_frame_bytes_57BE` reports `fighterSlot=010534e8 active544=1 cb584=01004e9d,01004e63,01004e11`.
  - enemy slot setup is missing: the next slot logs `fighterSlot=010535ac active544=1 cb584=00000000,00000000,00000000` immediately before the `pc=0` crash.
  - `sub_66A4_enemy_lookup_compare` shows the `4/10.battleinfo` enemy id `3` being compared with local table ids `10001,0,0,0`, then `sub_66A4_enemy_lookup_miss` returns `-1`.
- static evidence:
  - Battle.cbm `sub_66A4()` at actual-code-buffer offset `0x66A4` loops four entries at `battleState+0x50`.
  - each entry is `0x4c` bytes, and the compared id is `[enemyTable + i*0x4c + 0x24]`.
  - `sub_66CC()` branches to the failed path at offset `0x6B26` if `sub_66A4()` returns negative; that path sets parse state but does not populate the enemy visual/callback fields that the render loop later calls.
- corrected mock:
  - when building generic `4/1 -> 4/10` challenge responses, the mock now resolves the enemy id through the current Battle.cbm enemy template table.
  - if the requested id is already present, it is preserved.
  - if not, the mock writes the first nonzero table id into the enemy record and logs `requestEnemyId`, `enemyWireId`, `enemyTable`, and `enemyTableIds`.
- evidence status:
  - confirmed: `4/10.battleinfo.enemy.id` must be resolvable by `sub_66A4()` for a stable enemy render slot.
  - hypothesis: using the first nonzero client table id is a parser-safe placeholder until the upstream response/resource contract that fills the real encounter enemy table is recovered.

Negative result from the first resolvable-id fallback:

- confirmed runtime evidence:
  - the fallback wrote `enemyWireId=10001` while the request/touch id was `3`: `mock_challenge_interaction_response ... requestEnemyId=3 enemyWireId=10001 enemyTableIds=10001,0,0,0`.
  - this makes the enemy render slot stable, but the opponent is visibly the player character. The enemy table entry being reused is the local player template, not the touched monster.
  - newest runtime trace keeps the same shape after the Type=100 rollback: `mock_challenge_interaction_response ... requestEnemyId=3 enemyWireId=10001 enemyTable=0500c718 enemyTableIds=10001,0,0,0`, then Battle.cbm `sub_66CC_entry` sees the same `enemyTable=0500c718`.
  - `trace_battle_module_data_write` shows Battle.cbm stores that table pointer into `battleState+0x50` at `pc=051884ce`; using code base `0x05181F20`, this is Battle.cbm offset `0x65AE`.
  - the same pointer appears earlier in main-CBE scene setup: `trace_sub_1010228_callsite ... srcObj=05400000 dstObj=0500c718`, so the Battle enemy-template table is currently the scene/copied encounter-object table produced by main CBE `sub_1010228()`.
- conclusion:
  - confirmed negative: `firstNonzero(enemyTableIds)` is not a valid semantic fix when the first entry is the player id.
  - current unresolved contract: before `4/10.battleinfo`, some upstream scene/otherinfo/resource response should populate the Battle.cbm enemy template table with the touched monster's id/visual entry.
  - the mock currently logs this case as `runtime_negative=enemy_template_fallback_is_player_character`; do not treat it as a confirmed real server mapping.
- instrumentation:
  - `trace_encounter_table_records` now dumps the first four `0x4c` records from the main-CBE encounter table at `sub_1010228` and again when `vm_net_mock_resolve_battle_enemy_id()` reads Battle.cbm's `battleState+0x50`.
  - each dump includes the ids at record offset `+0x24` (the field compared by Battle.cbm `sub_66A4()`), plus raw fields at `+0x00`, `+0x20`, `+0x28`, `+0x36`, and `+0x44`.

Latest source-table correction:

- confirmed runtime evidence:
  - latest `trace_encounter_table_records` from the main-CBE `sub_1010228` source pointer logs `table=05400000 ids=88098216,1701734764,120,0` with raw words that decode partly as the active scene/player object, not as a normal four-record monster-template table.
  - the later Battle-side table remains `table=0500c718 ids=10001,0,0,0`, and generic `4/1 id=3` still resolves to `enemyWireId=10001`.
- conclusion:
  - confirmed negative: the `sub_1010228` source pointer being copied into `0500c718` is not the source of the touched monster id `3`; treating it as a four-entry monster list was an over-broad interpretation of the Battle.cbm `0x4c` record shape.
  - confirmed: the touched `4/1.id=3` comes from a different scene/menu interaction record than Battle.cbm's current enemy-template table.
- new trace-only instrumentation:
  - first attempt `trace_challenge_request_source` watched `0x01037ECC/0x01037ECE/0x01037ED4`, but the next run produced no marker while still sending `4/1 id=3`. This is confirmed negative evidence for that exact PC set as the active touch path.
  - static correction: the literal pool near `0x01038240..0x010382AC` shows that the `0x01037C9C` / `0x01037F6E` family writes several fields (`taskid`, `state`, `page`, `roomid`, `type`, `index`, `posx`, `posy`, etc.); individual callsites must be identified by runtime field-name pointers rather than by assuming one PC is the `id` writer.
  - `trace_outgoing_field_callsite` now watches the field-writer `BLX` callsites in `sub_1037C9C()` and `sub_1037F6E()`, logging `fieldPtr`, decoded field name, value, packet object, and current event object. This is trace-only and does not change packets, guest registers, Battle.cbm globals, or scene state.
  - newest manual run confirms another negative: no `trace_outgoing_field_callsite` marker appears, while the packet log still shows the live request `WT len=88 hdr=4/1 objs=1/4/1(id=3),1/2/1 count=2` with fields `index=3,posx=173,posy=272` and a `moveinfo` blob. Therefore the active touch/encounter producer is still outside the watched `sub_1037C9C()` / `sub_1037F6E()` field-writer set.
  - new send-boundary trace-only instrumentation `trace_outgoing_wt_send_context` runs inside `vm_net_mock_on_send()` for outgoing `4/1`, `4/2`, and `2/10` WT packets. It records the first object family, object count, current PC/LR/lastAddress, R0-R7, `g_netCurrentObject`, the `R9+0x5540` event object, and decoded fields such as `id/index/posx/posy/Operate`. This does not change packet bytes, guest registers, CBE globals, Battle.cbm globals, parser flow, or renderer state.
  - newest send-boundary evidence: the live simple request is `trace_outgoing_wt_send_context first=4/1 objectCount=1 ... id=3 index=3 pos=173,272 ... lr=010347e5 last=010347e2 netFlush=0103478f eventObj=01056124 eventBase=05000000 uiName=01桃花岛_02.sce`.
  - paired allocation trace identifies the real producer: immediately before that send, `trace_alloc_outgoing_game_event ... pc=0100e2e4 lr=0518243b caller=0518243a regs=00000005,00000001,00000004,00000001 r5=00000003` allocates the `1/4/1` object from Battle.cbm, not from main-CBE `sub_1037C9C()` / `sub_1037F6E()`.
  - static Battle.cbm evidence: at actual address `0x0518241E` (offset `0x04FE` from code base `0x05181F20`), the function allocates a `1/4/1` object via the main-CBE outbound-event API, then writes field literals `id`, `index`, `posx`, and `posy` (`0x0518244C..0x0518247C`, literal pool around `0x05182740`). Its entry arguments are `r0=id` and `r1=index`.
  - trace-only instrumentation now adds `trace_battle_outgoing_request_source` at Battle.cbm offsets `0x04FE` (`battle_send_challenge_4_1_entry`) and `0x2B50` (`battle_send_operate_4_2_entry`) to record the dynamic caller and battle state at the request-builder entry. This is read-only and does not alter packet bytes or guest state.

Latest Battle.cbm challenge producer evidence:

- confirmed runtime evidence:
  - the newest manual run logs `trace_battle_outgoing_request_source label=battle_send_challenge_4_1_entry pc=0518241e off=04fe lr=05182961 caller=05182960 ... regs=00000003,00000003,... enemyTable=0500c718 enemyIds=10001,0,0,0`.
  - therefore Battle.cbm enters the `1/4/1` builder with `r0=3,r1=3`, and the selected monster id/index are already known before the mock builds `4/10.battleinfo`.
  - static Battle.cbm disassembly around actual `0x05182940..0x0518295C` shows the latest caller is the fallback branch: it calls the method at `[global+0x6c]` with stack out-params, then loads `[sp+0x90]` and `[sp+0x8c]` into `r0/r1` before calling `0x0518241E`.
  - at that same runtime point, the Battle.cbm enemy template table is still `0500c718` with ids `10001,0,0,0`; the `4/10` mock therefore still falls back from request id `3` to `enemyWireId=10001`, which is parser-safe but visibly the wrong opponent.
- conclusion:
  - confirmed: the `4/1` selected monster id/index and the enemy visual/template table are separate client-side data sources in the current state.
  - confirmed negative: changing `4/10.battleinfo.enemy.id` to the first resolvable table id fixes the previous render crash, but it does not represent the touched monster when that table contains only the player id.
- trace-only instrumentation:
  - added `trace_battle_challenge_source_branch` at Battle.cbm offsets `0x0A04`, `0x0A0C`, `0x0A20`, `0x0A2A`, `0x0A38`, and `0x0A3C` (actual `0x05182924/0x0518292C/0x05182940/0x0518294A/0x05182958/0x0518295C` for code base `0x05181F20`).
  - it records `[global+0x6c]`, stack out-params `[sp+0x90]/[sp+0x8c]`, the direct-branch candidate fields at `global+0x3B4/0x3B6`, nearby record fields derived from the returned index, and the current enemy-template table ids. It does not alter registers, packets, CBE globals, Battle.cbm globals, parser flow, or rendering.

Follow-up selector evidence for `0x010183A0`:

- confirmed runtime evidence:
  - latest `trace_battle_challenge_source_branch` shows `[global+0x6c]=010183a1`.
  - the method returns false for several ticks, then at tick `184` writes stack out-params `[sp+0x90]=3` and `[sp+0x8c]=3`; immediately after, `challenge_fallback_call_4_1` enters the Battle.cbm `4/1` builder with `r0=3,r1=3`.
  - after the packet is emitted, the sent flag at `global+r7` flips from `0` to `1`, preventing repeated challenge sends.
- static evidence:
  - main CBE `sub_10183A0()` (`0x010183A0`) is the prompt/hotspot selector already documented as depending on `R9+0x5CD8`.
  - its loop at `0x010184A8..0x0101850C` scans up to 25 records of size `0x154` from `[R9+0x5C64+0x4C]`.
  - the candidate gate checks record bytes around `record+0x13B` and `record+0x13F`, calls a rect/collision callback through `record+0x40`, requires `[R9+0x5C64+0x74]` (`R9+0x5CD8`) to be non-null, then writes `outId = [record+0x64]` and `outIndex = loopIndex` at `0x010184FA..0x01018502`.
- conclusion:
  - confirmed: the touched monster request id/index come from the main-CBE prompt/hotspot record table, not from Battle.cbm's enemy-template lookup table.
  - unresolved: the server/resource contract that should copy or materialize a matching monster visual/template entry into Battle.cbm `battleState+0x50` before `4/10.battleinfo` is still missing.
- trace-only instrumentation:
  - added `trace_prompt_hotspot_candidate` at `0x010183A0`, `0x010184A8`, `0x010184BE`, `0x010184D8`, `0x010184F8`, `0x010184FA`, `0x01018502`, and `0x01018504`.
  - it records the prompt/hotspot table pointer, first eight record ids and gate bytes, current candidate record fields including `record+0x64`, and the `R9+0x5CD8` prompt label. It does not alter packets or guest state.

Battle.cbm enemy-template table source:

- confirmed static evidence:
  - `BattleScene_CreateCharList()` in `mmBattleMstarWqvga.cbm` at offset `0x642A` binds several main-CBE data sources before any `4/10.battleinfo` parse.
  - offsets `0x6462..0x646A` call the method at `[*[R9+0x2050] + 0x100 + 0x1C]` and store its return into `battleState+0x54`; `CollectAttackableTargets()` at offset `0x28A4` later treats this as the 25-record scene/hotspot table with stride `0x154`.
  - offsets `0x65A8..0x65AE` call the direct method at `[*[R9+0x2050] + 0x28]` and store its return into `battleState+0x50`; `sub_66A4()` at offset `0x66A4` later treats this as the four-record enemy-template table with stride `0x4c` and id field `+0x24`.
  - main-CBE `GetSceneData23796()` at `0x01018512` returns `Global_R9+23796`; current runtime evidence shows that buffer as `0500c718` with ids `10001,0,0,0`.
  - main-CBE `CopyRoleDataToBuffer()` at `0x01010228` zeroes the `0x130`-byte compact buffer and fills only row 0 from the current player scene node; this explains the player-template fallback but does not explain real monster population.
- confirmed runtime evidence:
  - after the challenge request `4/1 id=105`, the current enemy-template table still logs `enemyIds=10001,0,0,0`.
  - forcing `battleinfo.enemyId=105` reaches `sub_66CC_enemy_lookup_failed_path` and later a zero callback crash, so `4/10` cannot safely name an id absent from the compact table.
  - the `2/10 Type=100` follow-up appears after `4/10.battleinfo` has already been parsed, so it cannot be the only source for the first enemy-template lookup.
- trace-only instrumentation:
  - `trace_battle_create_charlist_source` now probes offsets `0x6462/0x6464/0x646A/0x646C` and `0x65A8/0x65AA/0x65AC/0x65AE/0x65B0`.
  - each trace records the main object callback table, `battleState` source pointers, current/prospective `0x4c` compact rows, the first six `0x154` hotspot rows, and the corresponding main-CBE scene/compact globals.
  - this does not alter packets, guest registers, CBE globals, Battle.cbm globals, parser flow, or render state.
- current fill-state:
  - static evidence links `net_handle_group_info()` subtype `10` (`0x01011F3A`) to `AddRoleToList()` (`0x01011E1E`), which appends decoded `groupinfo` records into the same main-CBE compact table returned by `GetSceneData23796()` (`0x01018512`).
  - that path is now a confirmed negative for monsters: it is a party/member table path with visible map UI and battle slot side effects.
  - default mock behavior is back to an empty `groupinfo` (`CBE_GROUPINFO_TEMPLATE_SEED=0`), preserving map and battle-slot sanity while the true non-party enemy-template source remains open.
- current prefill experiment:
  - static evidence: `net_handle_group_info()` subtype `5` starts at `0x0101208C`, reads a single `groupinfo` blob, then calls `AddRoleToList()` at `0x01012116`.
  - recovered subtype-5 blob grammar from IDA: `u32 id, u8, string name, u8, u8, u32, u32, u32, u32`.
  - the mock now optionally prepends `1/5/5 { groupinfo=<one subtype-5 record> }` before `1/4/10 battleinfo` in the same `4/1` response when the requested monster id is absent from the compact table.
  - default experiment switch: `CBE_BATTLE_PREFILL_ENEMY_TEMPLATE=1`. Setting it to `0` restores the crash-safe fallback `enemyWireId=10001`.
  - confirmed negative: encoding all subtype-5 `u8` fields as tagged `00 01 xx` values misaligns the record. Runtime evidence from the failed run shows `trace_add_role_to_list` entry with `nameArg=0x00140000`, then a malformed row1 and a pre-battle crash at main CBE `SetAnimFrameIndex` (`0x01004E1C`) with a null animation/resource pointer; Battle LR was `0x05188A93`.
  - confirmed negative: encoding all three subtype-5 `u8` fields as raw bytes lets `sub_66A4()` match row1 id `105`, but the row is still malformed (`raw20=746f0000`, `raw28=00010000`, `raw36=00011401`) and the client crashes before battle entry (`stdout_trace.log`: `pc=0x00270000`, `lr=0x0101438B`, `lastPc=0x01014388`).
  - confirmed negative: raw first byte plus tagged post-name bytes (`u8Encoding=raw_then_tagged`) is still malformed. Runtime evidence from the newest run shows the same-packet prefill is delivered (`net_packets.log` responseLen `174`, objects `1/5/5 + 1/4/10`) and Battle.cbm lookup hits row1 id `105`, but `trace_add_role_to_list` records stack values `116,111` (`'t','o'`) flowing into the post-name byte slots and compact row bytes `raw20=746f0000`, followed by a pre-battle crash at `stdout_trace.log` `pc=0x00270000`, `lr=0x0101438B`, `lastPc=0x01014388`.
  - confirmed partial: same-packet object ordering lets main-CBE parse `5/5` and fill row1 through `AddRoleToList()` before Battle.cbm consumes `4/10`; the remaining bug is the subtype-5 record grammar / stream cursor contract, not packet order.
  - trace-only follow-up: `trace_groupinfo_case5_reader` now instruments main-CBE `net_handle_group_info()` subtype-5 read sites `0x0101209E..0x01012116` and dumps reader state, callback slots, cursor, blob pointer/length, next bytes, registers, and local stack slots around the static reader order `+0x20, +0x2C, +0x1C, +0x28, +0x28, +0x20*4`.
  - confirmed from reader trace: the `groupinfo` object blob begins with a 2-byte inner length before the record. `trace_groupinfo_case5_reader` at `0x0101209E` shows `next=00 25 00 00 00 69...`; the `+0x20` id reader consumes 6 bytes and reads the id from cursor+2, so the inner length acts as the first scalar's two-byte prefix.
  - confirmed root cause for the latest crash: after the id read, cursor is `6`; the previous mock placed an extra raw `ignoredByte=00` before the string, so `+0x2C` and `+0x1C` read a zero string length from bytes `00 00`. `after_name_ptr` then returns `05001868` and leaves cursor `8`, so subsequent `+0x28` reads consume bytes inside `Monster`.
  - corrected current trial: subtype-5 prefill now uses `raw id` followed immediately by `seq string`, `seq u8`, `seq u8`, and `seq u32*4`. The first id is raw because the blob inner length already supplies the reader's two-byte prefix; the later scalar fields use normal seq prefixes because their readers advance by 3 (`+0x28`) or 6 (`+0x20`).
  - expected positive evidence: `mock_battle_enemy_template_prefill response=5/5`, `trace_add_role_to_list` showing row1 id `105`, `trace_battle_create_charlist_source` compact ids `10001,105,0,0`, and no `sub_66CC_enemy_lookup_failed_path`.
  - expected negative evidence: offline teammate UI, malformed row bytes after `trace_add_role_to_list`, subtype-5 parser failure, `trace_groupinfo_case5_reader` showing cursor still stuck inside the name bytes, or the same zero-callback crash seen when forcing `enemyWireId=105` without a compact-table row.
  - latest routing correction: a later run did not enter battle because the generic special-scene handler intercepted `4/1 id=105,index=4,pos=108,448` and returned `1/30/1 scene/posinfo`. Runtime evidence: `net_packets.log` tick `297` source `builtin-special-scene-interaction`; `net_trace.log` `mock_special_scene_interaction_response request=4/1 id=105 index=4 pos=108,448 ... target=01..._02.sce`.
  - corrected special-scene contract: the Taohuadao scene-entry handler now only answers known portal rectangles recovered from `trace_same_class_scene_table_entry` (for example `208,432-256,448 -> 01..._02.sce`). Monster `id=105` outside that rectangle must fall through to the battle challenge response.

Battle operate request `4/2`:

- confirmed runtime evidence:
  - once battle entry no longer crashes, Battle.cbm emits `WT len=36 hdr=4/2 objs=1/4/2`.
  - decoded request fields are `index=1` and `Operate=0`.
  - newest send-boundary evidence confirms this request is also built inside Battle.cbm: `trace_alloc_outgoing_game_event ... lr=05184a95 caller=05184a94 regs=0000000a,00000000,00000004,00000002` immediately precedes `trace_outgoing_wt_send_context first=4/2 ... index=1 operate=0`.
  - newest builder-entry trace logs `trace_battle_outgoing_request_source label=battle_send_operate_4_2_entry pc=05184a70 off=2b50 lr=051882d9 caller=051882d8 ... counts=1,1 subtype=10 parseOk=1 enemyTable=0500c718 enemyIds=10001,0,0,0`.
- static evidence:
  - corrected: Battle.cbm `sub_7BD0()` reaches the real subtype-2 response switch at actual code-buffer address `0x05189BD2`.
  - the first field is read through the u8 getter at `0x05189BD6..0x05189BDA`, then dispatched by a jump table at `0x05189BDC..0x05189BE6`.
  - latest runtime evidence shows `result=1` reaches the subtype-2 switch but then takes a no-visible-action path; none of the previously marked `0x05189D32` `info` probes fire for `result=1`.
  - corrected hypothesis: the `0x05189D16..0x05189DBC` block is a later subtype-2 result-table case, not the default success path for `result=1`. It still reads field `info` twice (`0x05189D32..0x05189D46`, literal `info` at `0x05189F4C`) if that case is reached.
  - Battle.cbm request-builder evidence: actual address `0x05184A70` (offset `0x2B50`) allocates the outgoing `1/4/2` object through the same main-CBE outbound-event API and has nearby literals `index` and `seq` around `0x05184B08..0x05184B18`.
  - static Battle.cbm disassembly around actual `0x051882BE..0x051882D8` shows the current caller path reads an operation mode from `[r4+6]`, passes `r1=4`, `r2=2`, `r3=0`, and calls the method at `[r4+0x18]`, which reaches the `0x05184A70` builder and returns at `0x051882D8`.
- newest dispatcher regression and mitigation:
  - latest crash-on-attack session still sends the same correct wire request, but no mock reply is emitted before the generic assert path:
    - `bin/logs/net_trace.log` tail: `trace_outgoing_wt_send_context first=4/2 ... index=1 operate=0`
    - immediately followed by `unhandled_packet WT len=36 hdr=4/2 objs=1/4/2 count=1`
    - `bin/logs/net_packets.log` tail then records `assert(0)`
  - emulator-side mitigation now in source:
    - `src/main.c` keeps the strict detector requiring both `index` and `Operate`, but adds a relaxed top-level `1/4/2` fallback detector.
    - if the strict `builtin-battle-operate` builder returns `0`, dispatcher now emits the same parser-safe `1/4/6 { actionnum=2, actioninfo=<raw tagged-seq> }` family through a fallback builder instead of letting `4/2` fall into `unhandled_packet`.
  - newer confirmation from source/binary cross-check:
    - current `bin/main.exe` already contains the fallback marker strings, but the newest logs still show no `builtin-battle-operate-fallback` line before `unhandled_packet`.
    - therefore the earlier battle-operate claimers are not sufficient as a practical safety net for this request shape.
    - current source now adds one more final guard: immediately before `vm_net_log_unhandled_packet()`, any remaining top-level WT header `4/2` is claimed by `builtin-battle-operate-lastchance-fallback`, which emits the same parser-safe `1/4/6` reply family.
  - corrected final detail from the newest source audit:
    - the first last-chance implementation was still insufficient because it delegated into `vm_net_mock_build_battle_operate_response_fallback()`, and that fallback itself still required the relaxed `1/4/2` object detector.
    - current source now lets the fallback builder accept header-level `WT kind/subtype = 4/2` directly when the relaxed detector fails, so the final unhandled exit no longer depends on the object parser succeeding twice.
  - build-artifact correction:
    - earlier "make passed" results were not enough by themselves because the old `bin/main.exe` sometimes remained in place even while `obj/main.o` updated.
    - confirmed working procedure for this turn:
      - rename the existing `bin/main.exe` away
      - rerun `make`
      - verify the new `bin/main.exe` actually contains the expected marker strings
    - the current rebuilt executable now contains:
      - `builtin-battle-operate-lastchance-fallback`
      - `builtin-battle-operate-raw82`
      - `mock_battle_operate_response_raw82`
  - status:
    - regression signature: `confirmed`
    - whether the next rerun reaches the strict path, the earlier fallback path, or the final last-chance fallback path: pending runtime confirmation
- current mock:
  - superseded negative: `builtin-battle-operate` answered `1/4/2` with `1/4/2 { result=2, info="" }`.
  - evidence status: `4/2.result=1` and `4/2.result=2` are both superseded as response experiments because the `kind=4` top-level subtype table maps response subtype `2` to the empty-return target `0x05189B34`.
  - logging correction: the request `index` field is encoded as u8 in the latest packet (`index=1`), while `Operate` is u32 (`Operate=0`); the mock now accepts either width for trace logging.
  - superseded experiment: `builtin-battle-operate` answered the `4/2` request with `1/4/9 { result=1 }`, logged as `mock_battle_operate_response response=4/9 result=1 ...`.
  - evidence for that experiment: Battle.cbm `sub_7BD0()` top-level kind-4 subtype table maps response subtypes `1` and `9` to the result parser at actual `0x05189BD2`, while subtype `2` maps to the no-op return target.
  - newest runtime evidence: `1/4/9 { result=1 }` is parser-valid and does reach the intended Battle.cbm branch. `bin/logs/net_trace.log` records `trace_business_dispatch_item ... kind=4 subtype=9`, then `battle_event7_kind4_call_sub_7BD0`, `sub_7BD0_subtype_switch`, and `sub_7BD0_4_2_result1_gate` at actual `0x05189BF0`.
  - confirmed negative: despite reaching the result=1 gate, no `sub_actioninfo_parser_*` marker and no `sub_4B70_battle_apply_entry` marker fires afterwards, and the user-visible attack still does not animate. Therefore `4/9 result=1` is only a partial parser hit, not the full operate/action response contract.
  - static follow-up target: after `0x05189BF0`, Battle.cbm checks state bytes at `[battleObj+0x470+4]` and `[battleObj+0x470]` before branching to `0x05189EA4 -> sub_51844BA`; if neither byte is `1`, the branch returns quietly to `0x05189B18`.
  - latest focused flow confirms the exact return reason. At tick `303`, `trace_battle_kind4_subtype9_flow` enters the subtype-9 result parser, reaches `0x05189BF0`, then reads `gateBytes=2,0,0,0,0` at `mainObj=0500b210 gateBase=0500b680`; `[+4]` is `0` and `[+0]` is `2`, so the branch at `0x05189C02` returns to `0x05189B18` instead of reaching `0x05189C04/0x05189EA4`.
  - latest gate-write evidence: `trace_battle_main_gate_write` shows main CBE `0x0101345E..0x01013466` clears `mainObj+0x474/+0x472/+0x473` and initializes `mainObj+0x470=2`; later Battle.cbm writes at `0x05188418/0x0518841A/0x0518913C` only clear `+2/+3`. No current run writes either `+0` or `+4` to `1` before the attack ack.
  - superseded negative experiment: `builtin-battle-operate` answered `4/2 index/Operate` with `1/4/8 { result=1, info=<raw empty> }`, logged as `mock_battle_operate_response response=4/8 result=1 infoLen=0 ...`.
  - evidence for that experiment: Battle.cbm `sub_7BD0()` maps response subtype `8` to actual `0x05189D16`; when the current mode value in `r0` is `2`, this branch reads `result`, then for `result=1` reads raw `info` at `0x05189D32..0x05189D46`.
  - latest runtime evidence: empty `info` reaches `trace_business_dispatch_item ... kind=4 subtype=8` and then `sub_4B70_battle_apply_entry`, but the caller is `lr=05189E25`, i.e. the fallback path `0x05189DBC -> 0x05189E20`, not the intended action path `0x05189D82 -> 0x05189DB4`. User-visible behavior is player escape/flee after pressing attack.
  - static explanation: `0x05189D7C` checks Battle.cbm data byte `R9+0x3450+0x0B` after copying raw `info`; empty `info` leaves it not equal to `1`, so the parser branches to the fallback/escape path.
  - latest traced negative build temporarily routed default `builtin-battle-operate` to `1/4/8 { result=1, info=<empty> }` so repeated local runs could exercise the subtype-8 parser path without extra environment setup.
  - newest evidence from that temporary build is the exact "玩家已逃跑" regression:
    - `bin/logs/net_packets.log` latest subtype-8 session shows `tick=957 source=builtin-battle-operate responseLen=30`, `hex=5754001e0101040800001906726573756c74000300010104696e666f0000`, i.e. `1/4/8 { result=1, info=<empty> }`.
    - matching `bin/logs/net_trace.log` at `tick=958` shows `trace_battle_kind4_subtype8_flow ... pc=05189D2E`, then `trace_battle_module_data_write ... writeAddr=0105402b raw=00000000`, then fallback `pc=05189DBC`, and finally `sub_4B70_battle_apply_entry ... lr=05189E25`.
    - static + runtime evidence therefore reconfirms that empty `4/8.info` leaves `R9+0x3450+0x0B == 0`, so Battle.cbm takes fallback `0x05189DBC -> 0x05189E20` and the visible result is flee/escape.
  - newest compiled experiment replaces that safe default with a minimal subtype-8 object: `1/4/8 { result=1, autorevive=1, info=<12 raw bytes> }`.
  - evidence for that experiment:
    - latest runtime trace disproves the earlier assumption that `0x05189D1A` reads `result`: at tick `272`, `trace_battle_kind4_subtype8_flow` shows `r1=0x05189AA8` before the callback, and module bytes at `0x05189AA8` resolve to string `autorevive`.
    - the same trace shows callback return `r0=0` at `0x05189D24`, followed by `0x05189D2E` writing `0` into `R9+0x3450+0x0B` and immediate fallback to `0x05189DBC`.
    - newest successful rerun (`bin/logs/net_packets.log` tick `167`, `bin/logs/net_trace.log` tick `168`) shows `mock_battle_operate_response response=4/8 result=1 autorevive=1 ...`, then `trace_battle_operate_subtype8_detail label=apply_gate_check pc=05189d82`, then `trace_battle_pool_probe label=sub_7BD0_subtype8_apply_call pc=05189db4`, and finally `sub_4B70_battle_apply_entry ... lr=05189DB9`.
    - this proves the client now reaches the intended success path `0x05189D82 -> 0x05189DB4 -> sub_5184B70`, rather than fallback `0x05189DBC -> 0x05189E20 -> sub_5184B70`.
  - hypothesis:
    - subtype-8 success requires at least `autorevive=1`; `info` may still need richer semantics for visible animation/state progression, but it is no longer needed merely to enter the apply branch.
    - the next missing contract is likely downstream of `sub_5184B70`, or in the semantic contents of the copied `info`, not in the top-level subtype-8 branch selection.
  - newer delayed-status7 negative:
    - after the successful subtype-8 apply path, Battle.cbm emits short `WT len=9 hdr=25/5`; one experiment answered that follow-up with `1/4/7` instead of same-subtype `1/25/5`.
    - packet evidence: `bin/logs/net_packets.log` tick `204` shows `source=builtin-scene-default-event responseLen=244` for the short `25/5`, and the response body begins with `1/4/7`.
    - runtime evidence: `bin/logs/net_trace.log` tick `205` shows `trace_business_dispatch_item ... kind=4 subtype=7`, then `sub_7BD0_status7_result_check pc=05189F7C` with registers `r0=0`, `r1=7`.
    - static evidence: `0x05189F7C..0x05189F90` begins `cmp r0,#2 ; bne 0x05189EA8 ; ... ; bl 0x0518935C`, so this delayed subtype-7 only enters `sub_743C` when the current mode value is `2`.
    - confirmed negative result: in the delayed `25/5 -> 4/7` path, `r0` is already `0`, so Battle.cbm returns through `0x05189EA8 -> 0x05189B18` without entering `sub_743C`, and runtime immediately falls back to `0x05182940` challenge/prompt selection.
    - conclusion:
      - delayed scene-default `4/7` is parser-valid but battle-wrong for attack continuation.
      - subtype-7 must be sent inside the original attack-response window if we want to study the real `sub_743C` importer.
  - current compiled default after that finding:
    - `vm_net_mock_build_scene_default_event_response()` is restored to safe `1/25/5 { result=4 }`.
    - a later experiment temporarily changed `vm_net_mock_build_battle_operate_response()` to attack-window composite `1/4/7 + 1/4/8 { result=1, autorevive=1, info=<12 bytes> }`.
    - that experiment is now confirmed unsafe as the default build:
      - runtime evidence: `bin/logs/net_trace.log` tick `342` shows `sub_7BD0_status7_result_check pc=05189F7C` with `r0=2`, then `sub_743C_status7_entry pc=0518935C`, `sub_743C_itemnum_read pc=05189498`, `sub_743C_iteminfo_read pc=051894B2`, `sub_743C_item_stream_init pc=051894C2`, and `sub_743C_crash_lastpc_candidate pc=0518952A`.
      - crash evidence: `bin/logs/stdout_trace.log` records invalid access `地址无法访问:4255de5a` with `lr=5189931 pc=5189178 lastPc=5189178`. With Battle.cbm base `0x05181F20`, this maps to runtime crash inside the same subtype-7 `iteminfo` importer family.
      - IDA evidence: `sub_743C` at `0x758C..0x75A2` always resolves field `"iteminfo"` and immediately runs the stream-init callback before any later `combatinfo/info/fbs/fdata` processing. The branch at `0x760A` only prepares stack-local pointers and jumps into `0x78C6`.
      - stronger IDA narrowing for the exact fault site:
        - helper `sub_7228` at `0x7228` is only called from the subtype-7 item/combat path at `sub_7794` / `0x7A0C`.
        - its guard at `0x7248..0x725C` only avoids the risky third check when one of these is already nonzero:
          - `[slot+0x08] > 0`
          - `[slot+0x0C] > 0`
          - `*((slotIndex << 6) + slot + 0x9E) != 0`
        - the live crash PC `0x05189178 -> offset 0x7258` therefore matches the third guard-byte read exactly, which means the current subtype-7 shell still leaves all three liveness fields empty before `sub_7228` runs.
    - current default is therefore reverted again to the confirmed non-crashing subtype-8 baseline:
      - `1/4/8 { result=1, autorevive=1, info=<12 bytes> }`
    - rationale:
      - attack-window subtype-7 is still the correct future research path because it preserves `mode==2`.
      - but until a parser-surviving `iteminfo` wrapper is recovered, it should not be the compiled default.
    - newer static narrowing on the same crash path now suggests one smaller experiment before full `iteminfo` recovery:
      - `sub_743C` computes `[R9+0x3D6C+0x08]` and `[R9+0x3D6C+0x0C]` from top-level status7 state deltas before the later item loop reaches `sub_7228`.
      - `sub_7228` only falls into the bad `0x7258` branch when both of those fields are non-positive.
      - therefore the next compiled experiment is to keep attack-window `4/7 + 4/8`, but change only the top-level status7 delta candidates (`lastexp/curexp/persentexp/gold/level`) to small positive values and see whether Battle.cbm bypasses `0x7258`.
      - status of this experiment is still `hypothesis` pending rerun; no runtime evidence exists yet beyond the safe-baseline run that stayed on single-object `4/8`.
  - trace-only instrumentation: `trace_battle_kind4_subtype9_flow` records the `sub_7BD0()` window for `kind=4 subtype=9` only, including actual PC/off, registers, `[r5]` as the pointer to the main object pointer, and bytes at `mainObj+0x470..0x474`. This does not alter guest memory, packet bytes, parser flow, or Battle.cbm globals.
  - new trace-only instrumentation: `trace_battle_main_gate_write` watches writes to the currently loaded Battle.cbm main object gate bytes `mainObj+0x470..0x474`. This is meant to identify which parser/state path sets the observed `2,0,0,0,0` gate and whether any real server response should transition either byte to `1` before an operate ack is accepted.
- latest runtime result:
  - confirmed: the `result=2` response is emitted on wire as `WT len=32 ... result=2, info=<empty>` and is parser-safe; no crash/assert follows in `stdout_trace.log`.
  - confirmed negative: `result=2` does not visibly execute an attack. Runtime dispatch reaches `trace_business_dispatch_item ... kind=4 subtype=2` and `sub_7BD0_subtype_switch` with `regs r0=2`, then returns to the data-event tail; no `sub_7BD0_4_2_success_*`, `sub_4B70_battle_apply_entry`, or battle-state action marker fires.
  - confirmed correction: the `result=1` no-op did not reach the `0x05189D32` `info` read probes; the earlier statement that `result=1` copied `info` is superseded.
  - newest manual attack run repeats the same result: tick `4384` dispatches `kind=4 subtype=2`, `sub_7BD0_subtype_switch` reads `result=2`, and render state remains `counts=1,1 subtypeState=10 parseOk=1` with `enemy0=...00002711...`; no action/apply probe fires.
  - newest packet/trace rerun shows the same no-op at a shorter timeline: `bin/logs/net_packets.log` records `WT len=36 hdr=4/2 objs=1/4/2` with `index=1,Operate=0`, followed by `1/4/2 { result=2, info=<empty> }`; `bin/logs/net_trace.log` records `mock_battle_operate_response ... result=2 ... index=1 operate=0` and dispatch `kind=4 subtype=2`, with no action/apply probe afterwards.
  - corrected trace mapping for the subtype-2 `result` jump table at actual `0x05189BE8`: bytes `3f 03 0f 2a 18 21 3f 33` map `result=1` to actual `0x05189BF0`, `result=2` to `0x05189C08`, `result=3` to `0x05189C3E`, `result=4` to `0x05189C1A`, `result=5` to `0x05189C2C`, `result=7` to `0x05189C50`, and `result=0/6/ge8` to the cleanup/default area. This explains why the previous `result=2` branch labels did not fire at the expected address.
  - newest manual rerun confirms the `4/2` response still reaches Battle.cbm: `trace_business_dispatch_item ... kind=4 subtype=2`, then `battle_event7_kind4_call_sub_7BD0`, `sub_7BD0_kind4_dispatch_entry`, and `sub_7BD0_subtype_switch` with `regs=00000002,00000002,...` at tick `1224`.
  - trace limitation found in that rerun: render-loop probes later consumed the shared `trace_battle_pool_probe` cap (`count=1200`), and the single-point result-branch marker still did not prove the exact post-jump PC. A focused `trace_battle_kind4_subtype2_flow` window now records actual PCs in `sub_7BD0` offsets `0x7BD0..0x7D60` only when the packet object is `kind=4 subtype=2`.
  - focused flow evidence from the next rerun: `trace_battle_kind4_subtype2_flow` records `subtype=2` entering `0x05189AF0`, reading subtype `2`, jumping from `0x05189B16` to actual `0x05189B34` (`off=0x7C14`), then returning via `0x05189B18/0x05189B1A`. Thus response subtype `4/2` is confirmed no-op in this Battle.cbm state.
  - static table map for the top-level kind-4 switch: subtype `0->0x05189B34`, `1->0x05189BD2`, `2->0x05189B34`, `3->0x05189B34`, `4->0x05189E26`, `5->0x05189C86`, `6->0x05189E84`, `7->0x05189F7C`, `8->0x05189D16`, `9->0x05189BD2`, `10->0x05189C86`, `11->0x05189B36`.
  - corrected next-step narrowing from the newest IDA pass:
    - `confirmed`: the real `actioninfo/actionnum` importer is subtype `6`, not subtype `8`.
    - static evidence:
      - `sub_7BD0` case `6` at `0x05189E84..0x05189F84` (`off=0x7F64..0x7F84`) only runs when `mode==2`, clears the scratch pointer at `R9+0x2868`, then calls `sub_6EB0(a1+0x0C, packetObj, parserObj, fighterIndex)` and finishes with `sub_259A(5)`.
      - `sub_6EB0` at `0x05188DD0..0x05189038` resolves outer fields `actioninfo` (`0x05188DF8`) and `actionnum` (`0x05188E1A`) before it touches any per-record bytes.
    - practical consequence:
      - subtype-8 remains a neighboring staging/apply path, but it is not the importer that materializes the real local action table.
      - the safest next wire experiment is therefore a minimal `4/6` response rather than another `25/*` continuation variant.
- related action/combat parser evidence:
  - Battle.cbm strings/xrefs identify `actioninfo` at `0x051891BC` and `actionnum` at `0x051891CC`, referenced by parser code at `0x05188DF8` and `0x05188E1A`.
  - corrected static disassembly base for Battle.cbm file work is `tools/disasm_thumb.py --file bin/JHOnlineData/mmBattleMstarWqvga.cbm 0xA1 0x05181F20 ...`; the old `0x9A` file offset produces garbage and should not be reused for this module.
  - static evidence from `0x05188DD0..0x05189024` shows the action parser is not a plain linear byte script reader. It first fetches named field `actioninfo` (`0x05188DF8..0x05188E00`) and then named field `actionnum` (`0x05188E16..0x05188E1C`) before iterating records.
  - recovered partial grammar from that parser:
    - `actionnum` is a `u8` loop bound stored at `sp+0x30`.
    - each parsed action claims one `0x64`-byte slot from a 13-entry table (`0x05188E30..0x05188E52`).
    - the per-action header begins with bytes at `slot+1`, `slot+2`, and usually `slot+3`; when `slot+1` is not `3/4`, `slot+3` is a subrecord count constrained to `1..6`.
    - each subrecord is 12 bytes wide and reads `{u8,u8,u32,u32}` into `slot+0x14 + 0x0C*i` (`0x05188E88..0x05188EB8`).
    - action type `1` and type `2` then take distinct extra payload branches (`0x05188ED0..0x05188FF8`), which is strong evidence that `4/8.info` must eventually materialize a structured inner object stream, not just a non-empty blob.
  - newer full-IDB decompile of `sub_6EB0` sharpens the minimum parser-valid subtype-6 shape:
    - `confirmed`: each action record first reads `type=u8` into `slot+1` and `actorSlot=u8` into `slot+2`.
    - when `type` is `3` or `4`, the parser skips the deeper child-count and variable-size payload branches entirely.
    - when `type` is not `3/4`, the parser next requires `subCount=u8` in range `1..6`, then `subCount` child records `{u8, u8, u32, u32}`, plus extra tails for `type==1` or `type==2`.
    - therefore the narrowest parser-valid subtype-6 experiment is:
      - outer field `actionnum = 1`
      - outer field `actioninfo = [ type, actorSlot ]`
      - no `teaminfo` / `iteminfo` required
    - current compiled experiment uses `{ type=3, actorSlot=request.index }` because that path avoids the unresolved type-1/type-2 tails while still entering the confirmed importer.
  - newer subtype-6 semantic correction:
    - `confirmed`: the old `type=3` experiment is importer-valid but battle-wrong for real attack flow.
    - static evidence:
      - `0x05188F4A..0x05188F52` compares the parsed `type` against `3` and `4`.
      - `type==3/4` branches directly to `0x0518902C`, skipping the deeper path at `0x05188F54..0x0518909A`.
      - only the non-`3/4` path reaches the action-materialization callback at `[R9+0x3450+0x24]+0x14` (`0x05189028` / `0x0518909A`).
    - runtime cross-check:
      - the old trace line `trace_battle_actioninfo_parser_detail label=type2_branch ... record=1,3,1,0` was misleadingly named.
      - the probe PC there is `0x05188E6E` (`off=0x6F4E`), which is exactly the `CMP type,#3` site, so the wire `type=3` did survive unchanged.
    - implication:
      - reaching `sub_actioninfo_parser_*` is not enough.
      - if the record uses `type=3/4`, Battle.cbm never takes the same deeper callback path that should build turn-action state.
  - newest runtime confirmation on that experiment:
    - packet evidence: `bin/logs/net_packets.log` tick `215` shows `1/4/6 { actionnum=1, actioninfo=[0x03,0x01] }`.
    - runtime evidence at tick `216`:
      - `trace_business_dispatch_item ... kind=4 subtype=6`
      - `sub_actioninfo_parser_entry` at `0x05188DD0`
      - `sub_actioninfo_parser_actioninfo_read` at `0x05188DF8`
      - `sub_actioninfo_parser_actionnum_read` at `0x05188E1A`
      - `trace_battle_actioninfo_parser_detail label=type2_branch ... record=1,3,1,0`
      - `sub_4B70_battle_apply_entry` and `sub_4B70_send25_call`
    - conclusion:
      - `confirmed`: subtype-6 is the first live operate response family that truly reaches the Battle action importer and the later apply/send window.
      - remaining issue is semantic: the current minimal record still leads into a death/revive prompt rather than a visible normal attack.
  - current compiled subtype-6 round experiment:
    - default mock has now moved from one minimal `type=3` record to a two-record `type=2` round candidate:
      - `1/4/6 { actionnum=2, actioninfo=<raw stream> }`
      - record 0: `type=2`, `actorSlot=request.index`, `subCount=1`, one zeroed child tuple, `effectId=0`, zero-length blob, zero tails
      - record 1: `type=2`, `actorSlot=0`, `subCount=1`, one zeroed child tuple targeting the player slot, `effectId=0`, zero-length blob, zero tails
    - status: `hypothesis`.
    - rationale:
      - this is the narrowest current candidate that matches a normal “player turn then enemy turn” round shape while also forcing `sub_6EB0` down the only statically confirmed action-builder path (`type 1/2` rather than `3/4`).
  - newer static subtype-6 materialization detail from IDA:
    - type-1 branch (`0x05188E9C..0x05188F0A`) and type-2 branch (`0x05188F0E..0x05188F7A`) both do more than just store targets:
      - they read `effectId` from record `+0x5C`
      - multiply it by battle-state stride `[R9+0x3450+0x18]`
      - copy one effect/template entry from `[R9+0x3450+0x40]` into a 0x20-byte scratch buffer via `sub_10A10`
      - then call the action-materialization callback at `[ *(R9+0x3450+0x24) + 0x14 ]` with:
        - `R1 = *(R9+0x2878)`
        - `R2 = scratch effect/template buffer`
        - `R3 = record + 0x10` (subtarget tuple region)
    - evidence:
      - IDA `mmBattleMstarWqvga.cbm` `sub_6EB0` at `0x05188EF6..0x05188F2A` and `0x05188F2E..0x05188F9A`
      - exact sites: template copy/load windows `0x05188F06/0x05188F76`, callback windows `0x05188F1A/0x05188F8A`
    - implication:
      - current all-zero `effectId/blob/tail` records are parser-valid but very likely action-semantic wrong, because they hand the callback an empty or default template plus an all-zero subtarget tuple block.
      - next successful mock packet will likely need a nonzero valid `effectId` and/or meaningful tuple bytes, not merely `type=2`.
  - newer runtime correction from the subtype-6 bridge/importer trace:
    - `confirmed`: current `type=2` round shell still returns early before the real type-2 materialize path.
    - runtime evidence from `tick=199`:
      - `trace_battle_kind4_subtype6_bridge` reaches `0x05189E96 -> 0x05188DD0`.
      - inside `sub_6EB0`, the importer advances through `0x05188E74..0x05188E7E`.
      - at `0x05188E7A` (`off=0x6F5A`), the just-read `subCount` is `R0=0`, so `0x05188E7C/0x05188E7E` takes the `BLE loc_6FC8` early-return path.
      - Battle then returns to outer case-6 `0x05189E9A` without ever reaching `0x05188F0E..0x0518909A`.
    - implication:
      - the current blocker is even earlier than `effectId` / callback semantics.
      - the encoded `actioninfo` field is still misaligned with the client stream reader, because the mock-side third logical byte (`subCount=1`) does not survive as Battle-visible `slot+3`.
    - strongest next hypothesis:
      - `actioninfo` should use `raw field` encoding, not `blob field` encoding with an extra nested `len16`.
      - evidence for that hypothesis:
        - repository already has another confirmed nested-stream case (`6/13.tasktypes`) where an extra blob-wrapper length breaks the client parser and `raw field` is required.
        - current runtime also shows Battle-visible `actioninfo` callback state inconsistent with the original 42-byte payload shape before the `subCount` read.
  - newest positive result after switching `4/6.actioninfo` to `raw field`:
    - user-visible result:
      - pressing attack now produces a real attack animation.
    - packet/runtime evidence:
      - `bin/logs/net_packets.log` latest session shows battle-operate responses shrink from `len=83` to `len=81`, matching removal of the inner blob `len16` wrapper while keeping the same 42-byte action payload.
      - latest importer trace at `tick=249` shows the first local action record is now `record=1,1,1,0` instead of the earlier `record=1,2,1,0`.
      - corresponding local table dump is `010534f4: 01 01 01 00 ...`, i.e. Battle is now materializing a different action-record shape from the same semantic payload once the outer field wrapper changes.
    - cautious conclusion:
      - `confirmed`: `actioninfo` outer encoding was part of the blocker, and `raw field` is closer to the client contract than `blob field`.
      - still `hypothesis`: the exact inner type semantics are not fully settled yet, because the current trace window still does not show the later `trace_battle_actioninfo_materialize_detail` callback markers.
    - newer runtime correction from the latest attack-animation rerun:
      - `confirmed negative`: visible attack animation does not mean the current inner record grammar is correct enough for full round progression.
      - runtime evidence from `bin/logs/net_trace.log` latest `tick=218`:
        - `trace_battle_actioninfo_parser_detail label=type2_branch ... record=1,1,1,0`
        - importer still returns through `0x05188EE8` (`off=0x6FC8`) immediately after the `0x05188F5A` (`subCount`) gate, without reaching `0x05188F76/0x05188F8A/0x05188F9C`.
      - implication:
        - under the current `response=4/6 actionnum=2` mock, Battle-visible record bytes are still `used=1, type=1, arg=1, subCount=0`.
        - therefore the remaining gap is now specifically the inner `actioninfo` header semantics or a still-missing inner wrapper, not the outer `raw` field choice.
      - newer stream-read detail from the next rerun:
      - `trace_battle_actioninfo_stream_read` at `0x05188E58`, `0x05188E64`, and `0x05188E7A` logs first-byte reads `1 / 0 / 0` while the wire payload still begins `02 01 01 00 ...`.
      - strongest current hypothesis: the real inner stream has a one-byte leading prefix/wrapper before the first action record, so Battle is consuming the mock stream starting at byte 1 rather than byte 0.
      - corrected after the one-byte-pad experiment:
        - adding a leading `0x00` changes the wire payload to `00 02 01 01 ...`, but Battle still reads `1 / 0 / 0`.
        - therefore the mismatch is not a simple one-byte shift in the raw payload. The `actioninfo` field value is being converted by earlier wrapper/reader helpers before `sub_6EB0` consumes it.
      - newer correction on the still-live `raw82` tailpath:
        - latest packet logs show the active `4/2` claimer is still `builtin-battle-operate-raw82`, whose historical head bytes were `00 02 01 01 00 00 ...`.
        - main CBE `stream_reader_init_from_blob()` (`0x01033B16`) plus `stream_read_i8_tagged()` (`0x01033AAD`) now make that old mismatch concrete:
          - the reader consumes `*(blob+4) + cursor + 2` with `cursor += 3`.
          - so a correct tagged `u8` cell must be `00 01 VV`.
          - the old bare `raw82` bytes can only produce the already observed wrong header values `1 / 0 / 0`.
        - implementation correction:
          - the tail `raw82` fallback must not keep emitting its frozen historical payload.
          - current source now forwards that tailpath into the same tagged-sequence `4/6.actioninfo` builder used by the newer dynamic experiments, so the active fallback no longer reintroduces the obsolete bare stream.
        - newest source/runtime correction after the next rerun:
          - the first tagged-seq-forwarder build still crashed before reply dispatch, but the failure is now confirmed to be mock-side buffer sizing, not client parsing.
          - runtime evidence: latest `net_trace.log` shows `mock_battle_operate_response_raw82 reject=fallback_builder_failed requestLen=36`, immediately followed by `unhandled_packet WT len=36 hdr=4/2`.
          - source evidence: both battle-operate builders still used `actionInfo[64]`, while two fully tagged action records exceed that capacity.
          - current source therefore enlarges the local `actionInfo` buffers to `128` bytes and adds a dedicated `reject=actioninfo_build_failed` trace.
        - newer positive/negative result after that capacity fix:
          - `confirmed`: dispatch now survives and Battle reaches the real subtype-6 type-2 materialize path.
          - runtime evidence:
            - `net_packets.log` shows handled `source=builtin-battle-operate responseLen=129`.
            - `net_trace.log` reaches `0x05188F96`, `0x05188FAA`, and `0x05188FBC`.
          - `confirmed negative`: a two-record `actionnum=2` reply still crashes client-side at `0x05188FD0`.
          - static/runtime evidence for that deeper crash:
            - Battle `0x05188FC8..0x05188FD0` dereferences a battle-global owner pointer after the type-2 callback path.
            - the first materialized record is `record=2,0,1`; the second is `record=2,1,1`.
            - the second record is the one that enters the null-owner branch and crashes with `stdout_trace.log pc=0x05188FD0 address=0`.
          - current mitigation:
            - the mock now sends only one type-2 attack record (`actionnum=1`) and omits the second counterattack record.
            - this keeps the proven real-attack importer path while avoiding the confirmed null-owner branch.
          - newer negative single-record slot-swap experiment:
            - the mock temporarily swapped the raw top actor/child-target bytes to test whether Battle.cbm's slot mapper was inverting player/enemy roles.
            - runtime then materialized the single record as `record=2,1,1` at `0x05188FAA` / `0x05188FBC`, instead of the previous no-crash `record=2,0,1`.
            - static Battle.cbm `0x05188F9C..0x05188FD0` shows subtype-6 then compares `[record+2]` against active local slot `[R9+0x2918+2]`; when equal, it enters the local-owner branch and dereferences `*(R9+0x40B8)` through `0x05188FCA..0x05188FD0`.
            - on the current mock contract that owner is still null, so the slot-swapped build crashes again with `stdout_trace.log pc=0x05188FD0 address=0`.
            - the same rerun also confirms the newly traced semantic fields stayed zero in that branch: `trace_battle_actioninfo_materialize_detail ... record=2,1,1 valueA=0 valueB=0 blobId=0 tail=0,0,0`.
            - `confirmed negative`: `record.actor == activeSlot` is not a safe default battle-operate shape yet, even if it may be semantically closer to the true local-player action branch.
          - newer current no-crash branch clarification from static + runtime:
            - runtime on the latest stable one-record build still materializes `record=2,0,1` with `valueA=0 valueB=0` at `0x05188F96/0x05188FAA/0x05188FBC`.
            - Battle `sub_6CE8()` at `0x05188C08` (`off=0x6CE8`) explains why the wire bytes and Battle-local slot ids disagree when `side==1`:
              - if `[R9+0x3450+0x60] == 1`, the helper remaps each incoming slot id by splitting at `[R9+0x3450+0x1C]` and offsetting by `[R9+0x3450+0x1B]`.
              - with current `4/10 side=1` and current one-vs-one `battleinfo`, that remap swaps `0 <-> 1`.
            - this exactly matches the latest wire/runtime pair:
              - mock still sends `wireMapped=1_to_0` in `mock_battle_operate_response`.
              - Battle later reads `record=2,0,1` from the same packet.
            - the latest local action-state trace also narrows the visible zero-damage symptom:
              - `trace_battle_local_action_state ... tick=212 ... activeSlot=1 activeBlock=010535ac activeActionBlock=01053acc activeAction=17,39,0,0 valueAB=0,80 targetId=1`
              - raw 64-byte dump at the same time is `hex=00000000112700000001000100000000500000005000000014000000140000000000000001000000...`
            - static Battle render consumer `sub_4582()` at `0x05184582` confirms:
              - `type==2` uses `fighter+0x5C5/+0x5C6` plus `sub_44CA()` for effect placement only.
              - `type==1` is the branch that reads `fighter+0x5CC/+0x5D0` (`+0x0C/+0x10` inside the active action block) for popup-number style output.
            - `hypothesis`: the current visible `0` is still downstream of the mock's zeroed child `u32` fields, because Battle's own reader traces at `0x05188EAE/0x05188EB6` and the materialized record both still show `valueA=0 valueB=0`.
            - current server-side experiment:
              - keep the only confirmed no-crash one-record shape (`record=2,0,1` after Battle remap).
              - change only the first child `u32` in `4/6.actioninfo` from `0` to a small non-zero trial damage value (`12`) in both the primary and fallback builders.
              - goal: test whether the next rerun turns the current zero popup into a real damage number without reintroducing the `0x05188FD0` owner crash.
            - newest runtime confirmation after that value-only experiment:
              - `confirmed`: the first child `u32` really does drive the visible damage amount. Latest logs show `mock_battle_operate_response ... damageValue=12`, Battle reads `child_valueA_read r0=0x0c`, and the materialized record becomes `valueA=12 valueB=0`.
              - `confirmed negative`: target ownership is still wrong on the same build. Runtime still reports `record=2,0,1`, then `trace_battle_local_action_state ... activeAction=17,39,0,0 valueAB=0,80 targetId=1`, and the visible result is self-hit for 12.
            - next narrow experiment:
              - keep the same parser-safe `4/6` one-record shape and the confirmed `damageValue=12`.
              - vary only `4/10.side` from `1` to `0`.
              - rationale: `sub_6CE8()` remaps subtype-6 slot ids only when `side==1`, and current one-vs-one `side==1` evidence already matches the observed `0 <-> 1` swap. If target ownership changes on the next rerun, the remaining bug boundary is the battle-start side contract rather than the subtype-6 value grammar.
            - newest runtime confirmation after the side-only experiment:
              - `confirmed`: changing only `4/10.side` from `1` to `0` fixes the first attack target ownership. The user-visible result is now monster-hit for 12 instead of self-hit.
              - `confirmed`: after that first attack, Battle sends another real operate request by itself: `trace_outgoing_wt_send_context first=4/2 ... index=0 operate=0` at ticks `187`, `219`, and `240`.
              - `hypothesis`: that second `4/2 index=0` is the monster auto-turn request window.
              - `confirmed negative`: current mock still returns the same target semantics for both `index=1` and `index=0`, so Battle-side local action traces remain stuck on the same target identity (`targetId=1`) for both turns.
              - newer runtime narrowing from the current `side=0` single-record build:
                - player turn (`index=1`) raw payload starts `00 01 02 00 01 01 00 01 01 ...`, while Battle later materializes record bytes `01 02 00 01 ... 00 01 00 00 0c 00 00 00 ...`.
                - auto monster-turn (`index=0`) raw payload starts `00 01 02 00 01 00 00 01 01 ...`, while Battle later materializes `01 02 00 01 ... 00 00 00 00 0c 00 00 00 ...`.
                - therefore the current mock is already changing the child target byte between the two turns, but Battle still does not enter a real opposing-actor round.
                - strongest next hypothesis: the missing discriminator for monster turn is now the materialized actor byte (`record+2`), not the already-varied child target byte (`record+0x15`).
                - evidence:
                  - `mock_battle_operate_actioninfo_payload` at `bin/logs/net_trace.log:13060`, `17859`, `18705`
                  - `trace_battle_actioninfo_materialize_record` at `bin/logs/net_trace.log:172` and `290`
                  - static parser layout `sub_6EB0` / `0x05188DD0..0x05189024`
            - current one-vs-one target experiment:
              - keep `side=0` and `damageValue=12`.
              - keep the same parser-safe one-record `4/6` family.
              - vary only the child target slot inside `actioninfo`: for `index=0`, target the other fighter slot (`1`) instead of mirroring the previous default.
              - goal: verify whether the second auto-issued `4/2 index=0` finally produces a visible monster attack onto the player.
            - newest negative result after that target-slot-only experiment:
              - runtime does show the altered wire marker on the second request: `mock_battle_operate_response ... index=0 wireMapped=0_to_1`.
              - but Battle still materializes the same local record/result shape:
                - `trace_battle_actioninfo_stream_read ... child_actor_map ... record=2,0,1`
                - `trace_battle_local_action_state ... targetId=1`
              - user-visible result also regresses: battle falls back to self-hit only, so that target-slot tweak is not the true monster-turn semantic.
            - corrected policy:
              - revert the `index=0 -> targetSlot=1` experiment.
              - keep the last known good baseline: `side=0` plus the old safe `wireMapped=%u_to_0` single-record operate reply, which preserves player-hit-on-monster for the first turn.
              - newest narrow experiment:
                - keep the same parser-safe one-record `4/6` family, same `damageValue=12`, and same raw target byte for `index=0`.
                - vary only the raw header actor byte for the auto monster-turn window:
                  - `index=1` keeps `mappedActorWireSlot = 1`
                  - `index=0` now tries `mappedActorWireSlot = 2`
                - rationale:
                  - current evidence shows `index=1` does not survive as materialized actor byte `1`; Battle still lands on semantic actor byte `0`.
                  - so the next parser-faithful test is whether the opposing semantic actor requires a one-step higher wire actor id in this window.
              - next refinement after the first `wireMapped=2_to_0` rerun:
                - `confirmed`: the actor-byte-only experiment really changes Battle-side semantics.
                  - auto turn now materializes `record=2,2,1` instead of the old `record=2,0,1`.
                  - local action state also changes from trailing `...,0,0` to `...,0,2`.
                - `confirmed negative`: that actor-only change is still not enough to produce visible monster retaliation.
                - newest negative rerun after that follow-up target tweak:
                  - mock does emit the intended wire change on the auto turn:
                    - `mock_battle_operate_response ... index=0 wireMapped=2_to_1`
                    - `mock_battle_operate_actioninfo_payload` tail changes from `...01000100...` to `...01000101...`
                  - Battle still keeps the useful actor-side semantic shift:
                    - `trace_battle_actioninfo_materialize_detail ... record=2,2,1 valueA=12 valueB=0`
                    - `trace_battle_local_action_state ... localState=...,0,2`
                  - `confirmed negative`: changing only that child target byte regresses user-visible behavior back to self-hit, so it is not the true monster-counterattack discriminator.
                - corrected policy:
                  - keep `index=0 -> mappedActorWireSlot=2`.
                  - revert the child target tweak and restore the old safe target byte (`wireMapped=2_to_0`) for the auto turn.
                - newest narrow experiment after that revert:
                  - keep the same one-record `4/6` family, `index=0 -> mappedActorWireSlot=2`, and safe target byte `wireMapped=2_to_0`.
                  - vary only the record `type` on the auto-turn window:
                    - player turn (`index=1`) stays `type=2`
                    - auto turn (`index=0`) now tries `type=1`
                  - rationale:
                    - static Battle.cbm `sub_4582()` at `0x4582` distinguishes the two local action kinds after importer materialization.
                    - `type==2` only goes through `sub_44CA()` effect placement (`0x45AC..0x45E4`).
                    - `type==1` is the branch that reads fighter-side value fields at `+1484/+1488` and emits popup-number style output (`0x45EE..0x4684`).
                  - hypothesis:
                    - current auto-turn `record=2,2,1 valueA=12` may already be importer-valid, but still too close to an effect-only semantic.
                    - if the missing counterattack symptom is really “same attack effect path, no opposing damage/apply semantic”, then switching only the auto-turn record to `type=1` is the narrowest parser-faithful next test.
                - newest rerun result on that `index=0 -> type=1` build:
                  - `confirmed`: importer does take the type-1 materialization arm.
                    - runtime: `trace_battle_actioninfo_materialize_detail ... label=type1_before_template_copy ... record=1,2,1 valueA=12 valueB=0`
                    - runtime: `trace_battle_actioninfo_materialize_detail ... label=type1_before_callback ... record=1,2,1 valueA=12 valueB=0`
                  - `confirmed negative`: the auto-turn type-1 branch is crash-unsafe in the current contract.
                    - at `0x05188F4C` (`type1_after_callback`), traced `recordBase` no longer stays on the local action table (`010534f4`) and instead drifts into callback/code-adjacent memory:
                      - `trace_battle_actioninfo_materialize_detail ... recordBase=01044ef7 record=4,28,29 valueA=2314360 valueB=2453889897`
                      - paired dump shows code bytes, not a valid 0x64-byte action record:
                        - `trace_battle_actioninfo_materialize_record ptr=01044ef7 ... hex=b5041c1d1cfff750...`
                    - the same run later crashes with invalid access at address `0`:
                      - `bin/logs/stdout_trace.log`: `地址无法访问:0 type:0 size:21 value:4`
                      - registers: `lr=0x05186C9B`, `lastPc=0x05186C98`
                  - corrected policy:
                    - revert `index=0 -> type=1`.
                    - keep the non-crashing auto-turn baseline `type=2`, `mappedActorWireSlot=2`, `wireMapped=2_to_0`.
                - newest narrow experiment on that restored type-2 baseline:
                  - keep `type=2`, `mappedActorWireSlot=2`, `wireMapped=2_to_0`, `damageValue=12`, and the current child tuple unchanged.
                  - vary only auto-turn second child `u32` seed:
                    - player turn (`index=1`) stays `valueBSeed=0`
                    - auto turn (`index=0`) now tries `valueBSeed=1`
                  - latest rerun disproves the old `effectId` interpretation:
                    - `mock_battle_operate_response ... valueBSeed=1`
                    - `trace_battle_actioninfo_stream_read ... child_valueB_read ... r0=00000001`
                    - `trace_battle_actioninfo_materialize_detail ... record=2,2,1 valueA=12 valueB=1 blobId=0`
                    - `trace_battle_actioninfo_materialize_effect_template ... ascii=f_blood1.actor......f_blood2.act`
                    - `trace_battle_actioninfo_materialize_scratch ... ascii=f_blood1.actor..................`
                    - `trace_resource_open_helper ... name=f_blood1.actor`
                  - conclusion:
                    - `confirmed negative`: the changed field is the current mock's second child `u32` (`valueB` after import), not the type-2 effect-template selector.
                    - even with `valueB=1`, the callback scratch/template remains `f_blood1.actor`, and the user-visible result is still “player hits monster, no monster counterattack”.
                  - corrected policy:
                    - restore that child `u32` to `0` in the default build.
                    - keep the actor-side discovery `index=0 -> mappedActorWireSlot=2`.
                  - trace-only next step now compiled:
                    - new Battle probes at `0x05186C94/0x05186C98` and `0x05186CE6/0x05186CEA` log `trace_battle_damage_dispatch_detail`.
                    - next rerun should answer whether the auto-turn ever reaches the player-hurt dispatch callback window, or whether the missing contract is still earlier than that.
                  - newest rerun already narrows that answer:
                    - `confirmed negative`: none of the new damage-dispatch markers fire on the current stable build.
                    - runtime instead repeatedly reaches `sub_45BA_action_state_tick_entry` with caller `0x05186C58`, after stable type-2 auto-turn materialization `record=2,2,1 valueA=12 valueB=0`.
                    - static `0x05186C3A..0x05186C58` shows that this is still the pre-dispatch countdown/state branch: it decrements `[r6+0x0D]`, calls `sub_45BA()` while the result stays positive, and only later can flow to `0x05186C74..0x05186CEA`.
                  - corrected next target:
                    - stop treating `0x05186C74..0x05186CEA` as the immediate next boundary.
                    - first recover the state-machine path in `sub_6A58()` / `0x05186A58..0x05186B06`, which can call `sub_4B70` directly for state `4` before the later hurt-dispatch logic is even considered.
                    - current trace-only build therefore adds probes at `0x05186A58`, `0x05186A6C`, `0x05186ACC`, `0x05186BAC`, and `0x05186BEC`.
                  - newest static/runtime narrowing after the next stable rerun:
                    - `confirmed`: the auto monster-turn request is still real `4/2 index=0 operate=0`, and the current reply still imports as `record=2,2,1 valueA=12 valueB=0`.
                    - `confirmed negative`: no `trace_battle_damage_dispatch_detail` markers fire, so the client still never reaches `0x05186C94/0x05186CE6` hurt-dispatch.
                    - runtime instead loops at `0x05186B0C` (`battle_action_state_machine_state4_store_target`) with stable local state `255,0,1,0,0,0,2`.
                    - static `0x05186B44..0x05186C08` now gives the exact branch candidates:
                      - if `currentBlock+0x4CC == 0`, Battle decrements local record byte `[r4+0x0E]`; when still positive it calls callback `[* (currentBlock+0x52C)](..., 8)` at `0x05186B7C..0x05186B80`, otherwise it stores mode `7`.
                      - if `currentBlock+0x4CC == 2`, Battle checks `currentBlock+0x52A/+0x52B` plus `currentBlock+0x544`, then copies target/coord fields at `0x05186BDC..0x05186C06` and exits.
                    - therefore the immediate missing contract is no longer “damage callback args”, but whichever action-table fields drive this state-4 branch away from pure target-store and into a real opposing-turn progression.
                  - trace-only follow-up now added:
                    - new probes at `0x05186B5C`, `0x05186B72`, `0x05186B7E`, `0x05186BB0`, `0x05186BBC`, `0x05186BCC`, `0x05186BD4`, `0x05186BE0`, and `0x05186BF0`.
                    - helper `trace_battle_state4_detail` logs local record bytes `[r4+0x0D]/[r4+0x0E]`, current block `+0x4CC`, action-block flags `+0x52A/+0x52B/+0x52C`, and the local record `+0x44` target source.
                  - newest comparison from that build:
                    - the same current `type=2` subtype-6 shell that gives visible player damage also immediately enters `state4_store_target -> target_copy_store`.
                    - therefore the present one-record `4/6` family is still acting like an effect/target-sync semantic, not a full turn-resolution semantic.
                  - narrowed server-side experiment now applied:
                    - keep `4/6 actionnum=1`, `type=2`, `mappedActorWireSlot`, `damageValue=12`, and all trailing fields unchanged.
                    - vary only the child flag byte consumed at Battle.cbm `0x05188EA6`:
                      - `index=1` remains `0`
                      - `index=0` now tries `1`
                    - this is the smallest parser-visible field change still inside the confirmed safe single-record grammar, and avoids reopening the earlier `type=1` and `actionnum=2` crash families.
                  - newest static correction:
                    - `confirmed`: subtype-6 outer record loop is driven by `actionnum`.
                    - static evidence:
                      - Battle.cbm `0x05189018..0x05189022` stores the incremented loop index to `[sp+0x2c]`, compares it against `[sp+0x30]`, and branches back to `0x05188E30`.
                      - command: `python tools/disasm_thumb.py --file bin/JHOnlineData/mmBattleMstarWqvga.cbm 0xA1 0x05181F20 0x05189018 40`
                    - implication:
                      - a multi-record `1/4/6 { actionnum=2, actioninfo=<tagged_seq> }` is parser-valid if each inner record stays on the already proven safe type-2 import path.
                  - current round-bundle experiment now applied:
                    - only `index=1` player attack replies now send `actionnum=2`.
                    - record 0 remains the current visible player-hit safe type-2 shape.
                    - record 1 reuses the current standalone auto-turn safe wire shape:
                      - raw actor byte `2`
                      - raw child target byte `0`
                      - child flag `1`
                      - `valueA=12`
                      - `valueB=0`
                    - `index=0` requests remain on the prior single-record baseline until runtime proves whether the bundled round already satisfies or suppresses the later auto-turn request.
                  - newest runtime correction after that experiment:
                    - `confirmed`: on the current stable battle path, the live client no longer emits an `index=1` operate request at all.
                    - evidence:
                      - `bin/logs/net_packets.log` latest fresh sessions show every battle operate request as `send_payload len=36 ... index=0 Operate=0`.
                      - paired server replies remain `responseLen=84` with `actionnum=1`, proving the old `index==1 -> actionnum=2` branch never executed.
                      - examples: `tick=170/189/208/247/265` in `bin/logs/net_packets.log`; `trace_outgoing_wt_send_context first=4/2 ... index=0 operate=0` in `bin/logs/net_trace.log`.
                    - corrected server-side policy:
                      - battle round bundling now keys off mock-side session phase rather than the request field `index`.
                      - `1/4/10` battle-start response arms a mock battle-operate session.
                      - the first live `4/2` request of that session now emits `1/4/6 { actionnum=2 }`.
                    - newest crash evidence on the first phase-tracked bundle build:
                      - `confirmed`: phase tracking did activate the intended two-record wire path.
                        - `bin/logs/net_packets.log`: first battle-operate reply becomes `responseLen=129`.
                        - `bin/logs/net_trace.log`: `mock_battle_operate_response ... actionnum=2 ... bundleWholeRound=1`.
                      - `confirmed negative`: using the older `wireMapped=1_to_0` shape as bundled record 0 is crash-unsafe on the live `index=0` path.
                        - runtime: record 0 materializes as `record=2,1,1 valueA=12 valueB=0`.
                        - Battle then enters the already known null-owner branch at `0x05188FCA..0x05188FD0`.
                        - crash evidence: `stdout_trace.log` reports `地址无法访问:0`, `pc=0x05188FD0`, `lr=0x0100DE83`.
                      - corrected bundle policy:
                        - bundled record 0 must stay on the currently verified no-crash live baseline, i.e. the same shape that already materializes as `record=2,2,1`.
                        - bundled record 1 remains the separate second-record experiment target.
                    - newest stable two-record rerun after restoring bundled record 0:
                      - `confirmed`: the first live `4/2` now stays non-crashing and really imports two records from one packet.
                        - packet evidence: `bin/logs/net_packets.log` first battle-operate reply `responseLen=129`.
                        - parser evidence: record 0 materializes at `recordBase=010534f4`; record 1 materializes at `recordBase=01053558`.
                      - `confirmed negative`: both bundled records are currently the same semantic shape.
                        - runtime: record 0 = `record=2,2,1 valueA=12 valueB=0`
                        - runtime: record 1 = `record=2,2,1 valueA=12 valueB=0`
                        - after import, Battle still loops only in `0x05186B0C -> 0x05186B10` (`state4_store_target -> state4_target_copy_store`) with no `trace_battle_damage_dispatch_detail`.
                      - next narrowed server experiment:
                        - keep record 0 on the stable `record=2,2,1` monster-hit baseline.
                        - change only bundled record 1 target-side wire byte to the old `wireMapped=2_to_1` branch, because earlier single-record experiments showed that shape regresses to self-hit but does not crash.
                        - hypothesis: if used only as bundled record 1, that old self-hit-safe shape may approximate the missing “monster hits player” second-half ownership without destabilizing record 0.
                      - later `4/2` requests in the same session fall back to the old single-record baseline.
                    - status:
                      - `hypothesis`: this remains the first build family where the multi-record subtype-6 path is actually reachable on the live `index=0` battle flow, but record 0 must stay on the `record=2,2,1` baseline to keep Battle out of `0x05188FD0`.
                - evidence:
                  - runtime:
                    - `bin/logs/net_trace.log`
                    - `mock_battle_operate_response ... index=0 wireMapped=2_to_0`
                    - `mock_battle_operate_response ... index=0 wireMapped=2_to_1`
                    - `trace_battle_actioninfo_materialize_detail ... record=2,2,1 valueA=12 valueB=0`
                    - `trace_battle_actioninfo_stream_read ... child_valueB_read ... r0=00000001`
                    - `trace_battle_actioninfo_materialize_detail ... record=2,2,1 valueA=12 valueB=1`
                    - `trace_battle_actioninfo_materialize_detail ... label=type1_before_template_copy ... record=1,2,1`
                    - `trace_battle_actioninfo_materialize_detail ... label=type1_after_callback ... recordBase=01044ef7`
                    - `bin/logs/stdout_trace.log` invalid access `地址无法访问:0`
                    - `trace_battle_actioninfo_materialize_effect_template ... ascii=f_blood1.actor......f_blood2.act`
                    - `trace_resource_open_helper ... name=f_blood1.actor`
                    - `trace_resource_open_helper ... name=f_blood2.actor`
                    - `trace_battle_local_action_state label=sub_45ba_action_state_tick_entry ... caller=05186c58`
                  - static/IDA:
                    - Battle.cbm `sub_6EB0` at `0x05188F68..0x05188F86` (child target write to `record+0x15`)
                    - Battle.cbm `sub_4582` at `0x05184582`
                    - Battle.cbm `sub_6EB0` type-2 effect-template selector `0x05188F76..0x05188F9A`
                    - Battle.cbm `0x05186C3A..0x05186C58`
        - status:
          - `changed`, pending rerun confirmation that the single-record build keeps attack animation without flash crash.
      - corrected getter boundary from the latest static/runtime review:
        - the older probes at `0x05188ED8` / `0x05188EFA` were pre-call markers, not post-call field results.
        - the continuous subtype-6 flow trace already shows the true return state:
          - at `0x05188EE2`, right after `BLX [slot+0x28]("actioninfo")`, registers are `r0=05001C18`, `r1=0000000C`.
          - at `0x05188EFE`, right after `BLX [packet+0x4C]("actionnum")`, register `r0=00000002`.
        - implication:
          - Battle.cbm receives `actionnum=2` correctly.
          - but `actioninfo` reaches `sub_6EB0` as a non-null pointer-layer object/value pair, not as the original outer raw field bytes directly in `r0`.
          - the remaining grammar mismatch therefore lives in that returned `actioninfo` object/stream-wrapper contract, not in `actionnum` and not in a trivial one-byte wire pad.
        - newer wrapper-head detail from the latest rerun:
          - the returned object at `0x05001C18` includes word `0x0000002B`, which matches the current mock `actioninfoLen=43`.
          - the same head also contains readable strings `action` and `task`.
          - recursive dumps now show two distinct internal pointers:
            - `head0_ptr=0x05001CF4` starts with `actioninfo\0`, then one byte `0x01`, then the payload bytes.
            - `head4_ptr=0x05001D00` starts directly at payload byte `0x00`.
          - inference:
            - `actioninfo` is very likely exposed to Battle.cbm as a named nested-reader object with its own callback table and child descriptors, while also retaining a direct payload-start pointer.
            - newest runtime resolves that ambiguity:
              - at `0x05188E58/0x05188E64/0x05188E7A`, the live reader object `r5=0x020FFB0C` has:
                - first dword progressing `3 -> 6 -> 9`
                - word `+0x08 = 0x05001C18`
                - callback tuple `+0x0C/+0x10/+0x14 = 0x01033993 / 0x01033941 / 0x01033901`
              - therefore Battle is walking the wrapper-root object `0x05001C18`, not the direct payload-start pointer `0x05001D00`.
              - the same reader dump also exposes its later callback slots, which match previously recovered generic tagged-stream helpers:
                - `+0x1C = 0x01033A87 -> stream_read_cstr_len16`
                - `+0x20 = 0x01033A5D -> stream_read_i32_be_tagged`
                - `+0x24 = 0x01033A3B -> stream_read_i16_be_tagged`
                - `+0x28 = 0x01033AAD -> stream_read_i8_tagged`
                - `+0x2C = 0x01033A1F -> stream_peek_i16_be`
            - stronger conclusion:
              - the current `1 / 0 / 0` header values come from the wrapper-reader contract itself.
              - subtype-6 is not expecting a bare packed byte array. It is expecting a tagged sequence stream whose field primitives line up with those reader callbacks.
            - the next recovery target is those wrapper-internal pointer blocks, not the outer `4/6` field count or `actionnum`.
          - compiled next experiment after this recovery:
            - `vm_net_mock_build_battle_operate_response()` now emits the same two-record player/enemy type-2 round shell using the repository's tagged sequence helpers (`vm_net_mock_seq_put_u8/u32/string`) instead of the old hand-written raw 43-byte array.
            - the intended effect is not yet “full correct combat”, but simply to align `actioninfo` with the reader contract Battle is verifiably executing.
    - next focus:
      - stop treating “attack animation appears” as the final target.
      - recover what the client expects next for the enemy turn / HP change / death-or-kill resolution path, using the now-working attack round as the new baseline.
  - newer static follow-up narrows the post-parse consumer boundary:
    - `0x0518455E` operates on a parsed `0x64`-byte action table in place, not on a network stream. It compacts occupied slots upward in 0x64-byte steps, then copies the last subrecord byte `slot+0x15` into a small local state byte at `sb-0x0f+1` (`0x05184598..0x051845B8`).
    - `0x051845BA` immediately calls that helper on `sb+0x0C`, then resets local battle-state bytes (`+0x0E`, `+0x05`, `+0x06`), sets `sb+0x9B0+8 = 1`, calls `0x05184304`, conditionally mirrors current gate state into `sb+3/+4`, and decrements per-fighter countdown bytes at `fighterBase+0x560+8` for all 6 fighter slots.
    - `0x05184B70` consumes that same local state: it reads `sb+0x0D`, checks the active fighter slot at `fighterBase+0x520+0x0B`, and then updates per-fighter coordinate/state fields in the `+0x500` region before later render work.
    - together, these functions look like a local “parsed action table -> active battle animation state” pipeline, not the original `4/8.info` parser itself.
  - practical consequence:
    - `4/8.info` most likely still has to be transformed into the `13 * 0x64` action-table format before `0x0518455E/0x051845BA/0x05184B70` become useful.
    - this makes the current absence of any `trace_battle_actioninfo_parser_detail` in safe `4/9` runs more meaningful: the client is not merely missing a later apply call; it is never entering the importer that would populate the action table in the first place.
  - newer trace-only instrumentation:
    - `src/main.c` now probes the actual effect-template / callback windows, not the earlier blob-copy stage:
      - type 1: `0x7006`, `0x701A`, `0x702C`
      - type 2: `0x7076`, `0x708A`, `0x709C`
      - all offsets are Battle-code-base relative to `0x05181F20`
    - trace correction: the older `fallback_branch` probe was also offset-wrong; the real type-not-1/2 fallback body is at Battle offset `0x70DA`.
    - to remove remaining ambiguity, `src/main.c` now also records a continuous subtype-6 flow window for Battle offsets `0x6EB0..0x7104` whenever the current packet is `kind=4 subtype=6`.
    - newer trace correction:
      - the first implementation only logged `trace_battle_kind4_subtype6_flow_start` in runtime, but not later flow lines.
      - the most likely reason is trace lifetime, not parser reachability: static case-6 bridge code stays in `sub_7BD0` at `0x7F64..0x7F84` and then branches into `sub_6EB0`, while runtime already separately proves the importer front-half fires (`0x6ED8`, `0x6EFA`, `0x6F4E`).
      - trace-only fix:
        - subtype-6 flow state is now kept alive until the next `0x7BD0` packet-family restart instead of being cleared by intermediate helper/module callbacks.
        - a new bridge trace records the outer case-6 window `0x7F64..0x7F84` as `trace_battle_kind4_subtype6_bridge`.
      - next evidence target:
        - one rerun should now show either the full bridge/importer chain (`0x7F64 -> 0x7F76 -> 0x6EB0 ...`) or the exact last Battle PC before the action table is later compacted away.
    - expected use:
      - prove whether runtime really reaches the later `0x7076/0x708A/0x709C` effect-template/callback sites, or exits earlier despite the parser seeing `type=2`.
    - new trace `trace_battle_actioninfo_materialize_detail` records:
      - current record `type/actor/subCount/blobId/tails`
      - battle effect-table stride/base from `R9+0x3450+0x18/+0x40`
      - computed effect-template pointer
      - callback owner/function at `[R9+0x3450+0x24]` / `+0x14`
      - 0x20-byte scratch template buffer and the full 0x64-byte parsed record
    - expected use on next rerun:
      - distinguish “effect template already empty” from “callback received a non-empty template but still left active action bytes zero”.
  - corrected subtype-8 copy/apply path from static + runtime evidence:
    - after entering the subtype-8 mode-2 branch, Battle.cbm first queries field `autorevive` via `[r7+0x4C]` with string target at `0x05189AA8`, not `result`.
    - only when that returned byte equals `1` does control continue to raw field `info` via `[r7+0x40]("info")` and `[r7+0x54]("info")`, storing `srcPtr` at `sp+8` and `srcLen` in `r7` (`0x05189D32..0x05189D46`).
    - it then calls an object method at `[(*r5 + 0x400) + 0x30]`-family and a second helper at `+0x2C`, followed by `blx 0x5192930` (`0x05189D48..0x05189D78`).
    - corrected destination ownership from new static main-CBE disassembly:
      - `0x05189D50..0x05189D58` calls callback `0x0100E255`, which clears the pointer slot passed in `r0 = R9+0x3450+0x30` (`bin/CBE/江湖OL.cbe:0x0100E254`).
      - `0x05189D62..0x05189D6C` calls callback `0x0100EA9D`, which allocates a new `len+1` buffer into that same slot when it is null (`bin/CBE/江湖OL.cbe:0x0100EA9C`).
      - `0x05189D76..0x05189D78` then calls `0x05192930` with destination `*(R9+0x3450+0x30)`, source `info`, and length `srcLen`.
      - therefore the earlier fixed trace value `parserObj+0x30 = 0x01018089` is a different object field and not the true subtype-8 `info` copy destination.
    - the later gate at `0x05189D7C` is now better understood from runtime:
      - successful rerun evidence shows `actionBytes=0,1,0,0` throughout `0x05189D40..0x05189D82`.
      - `trace_battle_operate_subtype8_detail label=apply_gate_check pc=05189d82` reports `parserFlags=0,1,0,0`.
      - `trace_battle_module_state label=sub_4B70_battle_apply_entry` simultaneously reports `autoRevive=1`.
    - therefore the proven top-level success precondition is `autorevive == 1`; the old claim that the branch required `sb+0x30+0x0B == 1` is superseded by the successful rerun evidence.
    - current status of `info`:
      - `hypothesis`: `info` still influences later battle action semantics, but not the initial `0x05189D82 -> 0x05189DB4` branch decision.
      - evidence: the current minimal 12-byte blob is enough to reach `sub_5184B70`, yet no `trace_battle_actioninfo_parser_detail` has been observed in this success-path rerun.
      - newer repeat evidence from the latest clean session (`bin/logs/net_packets.log` tick `184`, `bin/logs/net_trace.log` tick `185`) shows the same success path again: `trace_battle_operate_subtype8_detail label=apply_gate_check pc=05189d82`, then `label=apply_call pc=05189db4`, then `sub_4B70_battle_apply_entry ... lr=05189DB9`.
      - the same session also proves the top-level copied-byte gate is now satisfied in practice: `trace_battle_module_data_write ... pc=05189d2e ... writeAddr=0105402b raw=00000001`.
      - despite that success-path repeat, there is still no `trace_battle_actioninfo_parser_detail` anywhere after the apply call, and the only follow-up wire traffic is the ordinary `25/5` scene-default event plus later `2/1 moveinfo` ack.
      - therefore the strongest current reading is unchanged: the next missing contract is downstream of `sub_5184B70`, or in the semantic contents of `info`, not in the top-level subtype-8 branch gate.
  - adjacent static helpers strengthen the “staging buffer” interpretation of `sb+0x30`:
    - `0x05188CDC` performs an earlier callback-driven pass over the same parser family and writes matched values into per-fighter blocks in the `+0x500` region, rather than treating the copied bytes as the final local table directly.
    - later local helpers such as `0x051892B0` / `0x0518935C` merge and move fixed-size local records (`0x40`-byte stepping, comparisons on `[record+0x64]`, accumulation at `[+0x0E]`), which look like already-materialized battle/action state.
    - therefore the strongest current model is: `4/8.info` first populates a staging/object buffer at `sb+0x30`; only after an additional importer step do the final local action/fighter tables appear.
  - another response/status parser reads `combatinfo` at `0x05189A94`, then optional `info`, `fbs`, and `fdata` fields around `0x05189958..0x05189A2E`.
  - stronger static tie-in for the missing importer boundary:
    - the `combatinfo` parser family at `0x05189810..0x05189A84` is part of subtype-7 Battle.cbm handler `sub_743C`, not subtype-8 `sub_7BD0`.
    - string/xref evidence: `combatinfo` at `0x05189A94`, `autorevive` at `0x05189AA8`, `info` at `0x05189AB8`, `fbs` at `0x05189AE4`, and `fdata` at `0x05189AE8`.
    - static control-flow evidence: `0x05189930..0x05189956` reads `autorevive`, writes `R9+0x3450+0x0B`, and calls the same `[battleState+0x400]+0x30` object method family used by subtype-8 staging.
    - the same handler then separately reads `info` pointer/len (`0x05189958..0x05189982`), `fbs` pointer/len (`0x051899C8..0x05189A10`), and `fdata` pointer/len (`0x05189A20..0x05189A46`).
    - `hypothesis`: real battle action materialization may require subtype-7 `combatinfo` side data, or a composite `4/8 + 4/7` response family, rather than subtype-8 alone.
    - current negative runtime support: in the successful subtype-8-only attack sessions, no `hdr=4/7` packet or `sub_743C_*` trace fires, and the local `0x64` action table still stays empty at `sub_4B70`.
  - corrected trace-only instrumentation: the old labels `sub_7BD0_4_2_success_*` on offsets `0x7E12/0x7E1A/0x7E1C/0x7E28/0x7E58/0x7E62/0x7E94` were misleading. Those offsets are inside the confirmed subtype-8 `result=1 info` branch (`0x05189D32..0x05189DB4`), so the probes are now renamed to `sub_7BD0_subtype8_*`.
  - new trace-only detail probes now log the subtype-8 copy/apply window with `srcPtr/srcLen`, parser object state, destination buffer pointer, and post-copy destination bytes, plus action-parser stack state (`slotTable`, `actionCount`, `loopIndex`, current `0x64`-byte record) at `0x6EB0`, `0x6E02`, `0x6E1E`, `0x6ED8`, and `0x6EFA`.
  - newest trace-only correction:
    - subtype-8 logs now separately dump the real `info` destination buffer from `*(R9+0x3450+0x30)` as `trace_battle_operate_subtype8_info_dst` / `trace_battle_kind4_subtype8_flow_info_dst`.
    - this avoids reusing the misleading `parserObj+0x30` dump, which static evidence now shows is not the raw `4/8.info` copy target.
  - latest default-build status:
    - the newest safe-baseline rerun still answered attack with `4/9 result=1`, so the new real-destination subtype-8 trace did not fire in that session.
    - to continue recovering the `4/8.info` grammar, the compiled default is now restored to the known parser-valid subtype-8 tracing variant `1/4/8 { result=1, autorevive=1, info=<12 bytes, byte11=1> }`.
    - subtype-7 remains disabled; this default change is only to exercise the corrected `infoDst` trace on the real subtype-8 path.
  - newest subtype-8 rerun with the corrected default:
    - confirmed runtime evidence at `tick=527`: Battle.cbm now allocates the real `4/8.info` destination at `R9+0x3450+0x30 = 0x05010CD8`.
    - evidence chain:
      - `trace_battle_module_data_write ... writeAddr=01054050 raw=05010cd8 pc=0100eab6` shows main CBE callback `0x0100EA9D` storing the new pointer.
      - immediately after, subtype-8 flow logs carry `infoDstBuf=05010cd8` through `0x05189D6E..0x05189DB4`.
      - `apply_gate_check` and `apply_call` still succeed, reaching `sub_4B70_battle_apply_entry ... lr=05189DB9`.
    - confirmed negative in the same rerun:
      - there is still no `sub_actioninfo_parser_*`.
      - the shared local action table at `010534f4` remains all zero bytes at `sub_4B70`.
    - trace limitation discovered:
      - the first corrected build still failed to dump `05010cd8` contents because the old dump condition wrongly depended on register `r7`, which had already been overwritten with `R9+0x3450`.
    - corrected trace-only follow-up:
      - subtype-8 traces now dump `trace_battle_operate_subtype8_info_src_wrapper` / `trace_battle_kind4_subtype8_flow_info_src_wrapper` from `sp+8`.
      - subtype-8 traces now always dump `trace_battle_operate_subtype8_info_dst` / `trace_battle_kind4_subtype8_flow_info_dst` from `infoDstBuf`, using a fixed short length instead of the clobbered `r7`.
    - newest follow-up from the latest manual run (`bin/logs/net_packets.log` tick `563`, `bin/logs/net_trace.log` tick `564`):
      - the same subtype-8 success path still occurs: `0x05189D82 -> 0x05189DB4 -> sub_4B70_battle_apply_entry`.
      - immediately after that apply, the only observed follow-up wire traffic is the ordinary `25/5` scene-default event; there is still no `sub_actioninfo_parser_*`.
      - the newest destination dump remains `trace_battle_kind4_subtype8_flow_info_dst ptr=05010cd8 len=16 ... hex=00400000652e6d69666c6f7765725374`, while the source-side wrapper dump is `trace_battle_kind4_subtype8_flow_info_src_wrapper ptr=05001cf2 ... hex=000000000000000000016d3c030135410301634103017369646500d154d10001`.
      - because those destination bytes still do not resemble the raw 12-byte mock `info`, the strongest current model is that `0x05192930` performs wrapper/object staging into `R9+0x3450+0x30`, not a plain byte copy.
      - stronger corrected runtime evidence from the newest attack run (`bin/logs/net_packets.log` tick `383`, `bin/logs/net_trace.log` tick `384`) narrows the failure before the copy itself:
        - `0x05189D3C` still reaches the `info` length callback setup with `regs=05001cf2,00000018,...`, so the `info` pointer side remains non-null.
        - but after `blx [packet+0x54]("info")`, `0x05189D44` and `0x05189D46` show `r0=0`, so the length getter returns zero and Battle.cbm writes `r7 = 0`.
        - the same run then reaches `0x05189D78` with `r0=05010cd8`, `r1=05001cf2`, `r2=00000000`; `0x05192930` is called as an ARM-mode memcpy/memmove helper, but with `len=0`.
        - this matches the new negative watch evidence: `trace_battle_subtype8_info_dst_watch_arm ... tick=384` appears, but there is no overlapping `trace_battle_subtype8_info_dst_write` at all in that tick.
      - conclusion:
        - the current subtype-8 blocker is no longer “memcpy target unknown”.
        - the sharper current hypothesis is: our mock `info` field wrapper is parser-visible enough for the pointer getter `[+0x40]`, but not for the length getter `[+0x54]`, which currently returns `0`.
    - newest trace-only instrumentation:
      - `src/main.c` / `src/hookRam.c` now arm a subtype-8 staging-buffer write watcher on the real `R9+0x3450+0x30` destination and emit `trace_battle_subtype8_info_dst_write` for overlapping writes in the same attack tick.
      - purpose: identify the actual writer PC/LR and staged byte evolution around `0x05010CD8` without writing guest memory or changing control flow.
    - current protocol experiment build:
      - `src/main.c::vm_net_mock_build_battle_operate_response()` now encodes subtype-8 `info` with `vm_net_mock_put_object_blob()` instead of the old raw-entry wrapper.
      - rationale: the new runtime evidence shows `[packet+0x54]("info")` returns zero length on the old raw wrapper, so the next minimal server-side experiment is to switch only the field encoding and test whether Battle.cbm now reports non-zero `info` length.
  - newest trace-only instrumentation extends the same coverage to subtype-7 `combatinfo`:
    - Battle.cbm offsets `0x78F0`, `0x7908`, `0x7A10`, `0x7A38`, `0x7A42`, `0x7A56`, `0x7AA8`, `0x7AB2`, `0x7B00`, and `0x7B0A` now emit `trace_battle_status7_combatinfo_detail`.
    - these probes log parser object state plus the three Battle.cbm staging destinations at `R9+0x3450+0x30` (`info`), `+0x34` (`fbs`), and `+0x38` (`fdata`), without altering guest memory or control flow.
  - newest runtime narrowing from the latest attack rerun:
    - confirmed negative: even on the stable subtype-8 success path (`mock_battle_operate_response ... response=4/8 ... len=58`, `sub_4B70_battle_apply_entry ... lr=05189DB9` at tick `1094`), there is still no `sub_743C_*` trace and no `trace_battle_status7_combatinfo_detail`.
    - confirmed packet shape: the latest attack request/response pair is still only `WT len=36 hdr=4/2 objs=1/4/2` -> single-object `1/4/8`.
    - therefore the current client is not seeing subtype-7 at all during attack resolution; the missing importer cannot come from an already-sent hidden `4/7` object in this session.
  - current experiment build:
    - attack response is now compiled as a two-object WT packet: existing parser-safe `4/7` status7 object followed by the already-validated `4/8 { result=1, autorevive=1, info=<12 bytes> }`.
    - status: `hypothesis`.
    - rationale: the strongest remaining low-risk server-side experiment is to test whether Battle.cbm expects a composite `4/7 + 4/8` family in one dispatch window before it will populate the local action importer path.
  - newest composite rerun disproves that empty-shell subtype-7 is safe on the attack path:
    - packet evidence: `bin/logs/net_packets.log` now shows `tick=195 source=builtin-battle-operate responseLen=297` for the attack request `WT len=36 hdr=4/2 objs=1/4/2`, and `bin/logs/net_trace.log` records `mock_battle_operate_response response=4/7+4/8 status7=appended ... len=297`.
    - runtime evidence: the same dispatch reaches `trace_battle_pool_probe label=sub_743C_status7_entry pc=0518935c`, then `sub_743C_result_read`, `sub_743C_itemnum_read`, `sub_743C_iteminfo_read`, `sub_743C_item_stream_init`, and finally `sub_743C_crash_lastpc_candidate pc=0518952a`.
    - stdout crash evidence: `地址无法访问:4255de5a type:0 size:19 value:1`, `pc=05189178`, `lastPc=05189178`, `lr=05189931`, `r9=01050bd0`.
    - IDA address evidence: with Battle.cbm base `0x05181F20`, crash PC `0x05189178` maps to offset `0x7258`, still inside the subtype-7 `iteminfo` stream path, before the later `combatinfo/info/fbs/fdata` staging window at `0x05189930..0x05189A46`.
    - static evidence from corrected disassembly of `0x05189470..0x051894C2` shows subtype-7 reads `itemnum`, then immediately fetches `iteminfo` pointer/length and initializes an item stream; the current empty shell therefore remains unsafe even with `itemnum=0`.
    - conclusion:
      - confirmed negative: composite `4/7 + 4/8` is not a safe default attack response with the current empty subtype-7 shell.
      - `hypothesis`: any future subtype-7 retry must first recover a valid minimal `iteminfo` wrapper/stream contract rather than only `combatinfo/info/fbs/fdata`.
  - corrected default after that crash:
    - `src/main.c::vm_net_mock_build_battle_operate_response()` is reverted to safe `1/4/9 { result=1 }` as the compiled default.
    - this keeps manual battle sessions alive while subtype-7 `iteminfo` is recovered offline.
  - corrected ownership of the earlier wrapper helper:
    - corrected control-flow evidence from `0x05188DD0..0x05188E1C` shows helper `0x05188C32..0x05188CD4` is called from the `actioninfo/actionnum` importer front-half, not from subtype-7 `iteminfo`.
    - evidence:
      - `0x05188DE8` calls `0x05188C32`
      - `0x05188DF2` calls `0x05188CDC`
      - only after those calls does the same function fetch named fields `actioninfo` at `0x05188DF8..0x05188E00` and `actionnum` at `0x05188E16..0x05188E1C`
    - therefore these helpers should be treated as `actioninfo` wrapper/importer helpers, not subtype-7 `iteminfo` wrapper helpers.
  - newer static narrowing on the real `actioninfo` record parser (`0x05188E30..0x05189024`):
    - `confirmed`: `actionnum` from `0x05188E16..0x05188E2C` is the outer loop bound, and each decoded record occupies a local `0x64`-byte slot in the table pointed to by `sp+0x34`.
    - per-record front-half evidence:
      - `0x05188E52..0x05188E68` reads two tagged `u8` values via getter slot `+0x28`; the second passes through helper `0x05188C08` before being stored at local byte `+2`.
      - `0x05188E74..0x05188ECC` reads another tagged `u8` count into local byte `+3`, then loops `count` times over fixed `0x0C`-byte child entries at local `+0x14 + i*0x0C`, each with:
        - tagged `u8` through `0x05188C08` -> child byte `+1`
        - tagged `u8` -> child byte `+2`
        - tagged `u32` -> child dword `+4`
        - tagged `u32` -> child dword `+8`
    - type-specific evidence:
      - `0x05188ED0..0x05188F4C`: when local byte `+1 == 1`, the parser allocates a temp buffer, reads one tagged `u32` into local dword `+0x5C`, reads a `len16 + bytes` blob via getter pair `+0x2C/+0x1C` into local `+4`, then reads three tagged `u8` bytes into local `+0x60/+0x61/+0x62` before copying a lookup payload and calling another callback.
      - `0x05188F4E..0x05188FF8`: when local byte `+1 == 2`, the parser reads the same dword `+0x5C`, blob `+4`, and tail bytes `+0x60..+0x62`, then conditionally decrements a countdown at `[global+0xE0+0x12]` and may call `[global+0x78]`.
      - `0x05188FFA..0x0518900E`: all other types zero local `+4..+0x0F`, set dword `+0x5C = 0xFFFFFFFF`, set bytes `+0x60=0xFF`, `+0x61=0`, `+0x62=0xFF`, and still keep the slot marked used.
    - conclusion:
      - stronger `hypothesis`: a future parser-valid `4/8.info` payload will need to materialize an inner `actioninfo/actionnum` stream whose record grammar matches this structured slot format, not merely a single gate byte or an unstructured raw opcode list.
  - newest static narrowing on subtype-7 `iteminfo` loop:
    - recovered helper shape:
      - `+0x28` returns a loop/count value.
      - inside the loop, `+0x24` and `+0x20` are consumed as a `ptr/len` pair and passed into another callback at `[global+0x54]`.
      - status: this helper shape is now attributed to the `actioninfo` importer path, not to subtype-7 `iteminfo`.
    - corrected disassembly of `0x05189534..0x051897DC` shows `sub_743C` does not stop at the top-level `itemnum` count. It iterates `itemnum` records and writes each decoded record into a local per-index table near `R9+0x2090 + slot*0x40`.
    - recovered record flow:
      - `0x05189534..0x051895A8`: compare a tagged `u32` from stream getter slot `+0x20` against `[global+0x64]`, then read:
        - tagged `u8` from slot `+0x28` -> current item-slot field near `+0x9E`
        - tagged `u32` from slot `+0x20` -> current item-slot field `+0x64`
        - `len16 + bytes` via slot pair `+0x2C` / `+0x1C` -> copied into current item-slot blob area near `+0x6C`
        - tagged `u8` from slot `+0x28` -> current item-slot field near `+0x9F`
      - this is now a stronger `hypothesis` than the earlier generic “u8/u32/blob/u8-style” summary, because it matches the repository's already-confirmed stream reader slot map from `stream_reader_init_from_blob()`:
        - `+0x20` = tagged `i32/u32`
        - `+0x28` = tagged `i8/u8`
        - `+0x2C` = peek/read `len16`
        - `+0x1C` = `len16 + bytes` / C-string body
      - `0x051895AA..0x0518971A`: when the later type byte equals `1`, the parser takes a richer branch that allocates a temporary buffer, reads an additional blob/string field, clamps/checks slot value `+0x64` against threshold `0x3E8`, and calls another module callback at `[global+0x980+0x2C]`.
      - `0x0518971C..0x05189776`: non-type-1 branch still reads another getter from slot `+0x24` first; if prior type byte equals `3`, it then reads another tagged `u32` from slot `+0x20` and accumulates that into slot field `+0x68` before advancing slot index byte `[*sp+0x170 + 0x0A]`.
      - `0x05189778..0x051897DC`: when the first tagged `u32` does not match `[global+0x64]`, the parser takes a non-match branch that still consumes `u8/u32/blob/u8/i16/u32`-style getters and may call another callback if the second `u8` equals `1`.
    - conclusion:
      - stronger `hypothesis`: a safe minimal subtype-7 shell will need at least one structurally valid `iteminfo` outer field whose per-record common prefix is close to:
        - `tagged u32 matchId`
        - `tagged u8 slotKind`
        - `tagged u32 value`
        - `len16 + bytes blob`
        - `tagged u8 type`
      - and then a valid continuation branch for `type==1` or non-`1`, not merely `itemnum=0` plus empty blob.
      - newer static narrowing from `sub_7228` means the valid continuation must also materialize at least one slot-liveness field before the later `combatinfo` branch:
        - `[slotBase+0x08]`
        - `[slotBase+0x0C]`
        - or `*((slotIndex << 6) + slotBase + 0x9E)`
      - otherwise subtype-7 falls through to the exact crash-site check at `0x7258`.
  - added trace-only coverage for that new boundary:
    - Battle.cbm offsets `0x6D12`, `0x6D20`, `0x6D4C`, `0x6D58`, and `0x6D6A` now emit `trace_battle_pool_probe` labels for the `actioninfo` wrapper helper before the later importer loop.
    - Battle.cbm offsets `0x6E6A`, `0x6E74`, `0x6ED4`, `0x6F4E`, and `0x6FFA` now emit `trace_battle_actioninfo_parser_detail` labels for the record type selector, child-count read, `type==1` branch, `type==2` branch, and fallback branch.
    - `trace_battle_actioninfo_parser_detail` now also dumps local record bytes `+0x14/+0x15/+0x16`, dwords `+0x18/+0x1C/+0x5C`, and tail bytes `+0x60/+0x61/+0x62` so the next rerun can directly distinguish parser-surviving type-1/type-2/fallback records.
    - Battle.cbm offsets `0x7614`, `0x7638`, `0x766E`, `0x7676`, `0x76DE`, `0x771C`, and `0x7778` now emit `trace_battle_pool_probe` labels for the item-record loop.
    - the same probe now logs extra stack slots `sp+0x14`, `sp+0x160`, `sp+0x170`, current item-slot index byte `[*sp+0x170 + 0x0A]`, and current per-slot values near `R9+0x2090 + slot*0x40 + {0x64,0x68,0x6C,0x9E,0x9F}`.
    - newest trace-only guard probes:
      - `trace_battle_status7_item_record_detail` now records `recordDesc`, slot index, slot base, and slot fields near `+0x08/+0x0C/+0x64/+0x68/+0x9E` around `0x0518952A..0x051898C6`.
      - `trace_battle_status7_sub7228_detail` now records the exact `sub_7228(slotBase, slotIndex)` inputs and guard-triplet values at caller site `0x0518992C` and inside `sub_7228` at `0x05189148`, `0x05189178`, and `0x05189264`.
  - latest runtime reconfirms the empty `4/8` experiment is a negative result, not a transport failure:
    - `bin/logs/net_packets.log` shows `tick=171` response `hex=5754001e0101040800001906726573756c74000300010104696e666f0000`, i.e. `1/4/8 { result=1, info=<empty> }`.
    - `bin/logs/net_trace.log` shows `trace_business_dispatch_item ... kind=4 subtype=8`, then Battle writes `R9+0x3450+0x0B` at `0x05189D2E` and `R9+0x3450+0x0A` at `0x05189DC6`, then reaches `sub_4B70_battle_apply_entry ... lr=05189E25`.
    - static/runtme combined evidence therefore places the actual path on fallback `0x05189DBC -> 0x05189E20 -> sub_5184B70`, not on the desired action branch `0x05189D82 -> 0x05189DB4`.
  - because the single-point subtype-8 probes did not emit during that real fallback run, tracing is now strengthened with a dedicated `trace_battle_kind4_subtype8_flow` instruction-window tracer over `0x05189AF0..0x05189E40` (`traceOff 0x7BD0..0x7F20`).
    - it logs candidate `info` source pointers/lengths, parser object/buffer/state, and `R9+0x3450+0x0A..0x0D` action bytes every step.
    - it is trace-only and does not write guest memory or force Battle globals.
  - newest trace-only follow-up widens that same subtype-8 capture one layer further:
    - it now also dumps the action-state/header object that owns the checked `+0x0A..+0x0D` bytes, plus `[header+0x30]` destination pointer and `[header+0x34]` capacity.
    - purpose: on the next controlled `4/8` rerun, determine whether the copied `info` bytes already resemble a structured inner object stream or whether the decisive `+0x0B == 1` gate is expected to be synthesized by another wrapper step before `sub_4B70()`.
  - newest trace-only follow-up adds one more local-consumer layer without altering guest logic:
    - Battle.cbm offsets `0x05184304`, `0x0518455E`, `0x051845BA`, and `0x05184B70` now emit `trace_battle_local_action_state`.
    - these probes dump `sb+0x2918` head bytes, shared action-table base `sb+0x2918+0x0C`, the currently selected fighter block `sb+0x2918 + 0xC4*activeSlot`, and the per-fighter action block near `+0x520`.
    - purpose: decide whether the subtype-8 success path is failing because the local `0x64`-byte action table never materializes, or because later consumers mis-handle an already-populated table.
  - newest local-state evidence now answers that narrower question directly:
    - in the latest success-path rerun (`bin/logs/net_packets.log` tick `180`, `bin/logs/net_trace.log` tick `181`), `trace_battle_local_action_state label=sub_4304_clear_action_bytes_entry` fires inside the subtype-8 success branch immediately before `sub_4B70`.
    - at that point, `trace_battle_local_action_table ptr=010534f4 len=128` is entirely zero bytes.
    - the same remains true at `trace_battle_local_action_state label=sub_4B70_battle_apply_entry`: the shared local action table at `sb+0x2918+0x0C` is still all zero bytes even though `actionBytes=1,1,0,0,0,0`, `autoRevive=1`, and the caller is the real success path `lr=05189DB9`.
    - `trace_battle_local_action_active_block` is also all zeroes there, while `trace_battle_local_action_active_action_block` holds only the already-known per-fighter battle payload shape, not parsed `0x64`-byte action records.
    - no `trace_battle_local_action_state label=sub_45ba_*` or `label=sub_455e_*` appears in that rerun at all.
  - conclusion from those traces:
    - confirmed: the current `4/8.info` candidate is enough to satisfy the top-level subtype-8 gate and enter `sub_4B70`, but it still does not trigger the importer/front-half that should populate the local `0x64`-byte action table.
    - therefore the missing contract is upstream of `sub_45BA/sub_455E/sub_4B70`, not inside those later consumers themselves.
  - latest safe-default rerun must be read as a separate session, not mixed with the older subtype-8 experiment:
    - `bin/logs/net_packets.log` last session starts at line `698`, and its battle-operate request at `tick=372` receives `responseLen=23`, `hex=575400170101040900001206726573756c740003000101`, i.e. safe default `1/4/9 { result=1 }`.
    - matching `bin/logs/net_trace.log` evidence at `tick=373` shows `trace_battle_kind4_subtype9_flow` reaching actual `0x05189BF0` and returning with `gateBytes=2,0,0,0,0` through `0x05189C02 -> 0x05189B18`.
    - no `trace_battle_kind4_subtype8_flow_*` line appears in that latest session, so the subtype-8 tracer was not bypassed; it simply was not selected because the run stayed on the parser-safe `4/9` default.
  - newest stable battle-flow rerun on the current branch still reconfirms the same no-op gate:
    - `bin/logs/net_packets.log` latest session reaches `tick=222 source=builtin-challenge-interaction` and `tick=272 source=builtin-battle-operate responseLen=23`.
    - matching `bin/logs/net_trace.log` shows `mock_battle_operate_response response=4/9 result=1 ... confirmed_negative=no_action_but_safe_no_escape`, then `trace_battle_kind4_subtype9_flow ... pc=05189BF0 ... gateBytes=2,0,0,0,0`.
    - this means the "进入战斗" path is stable again, but no new subtype-8 evidence is produced until the compiled default or a controlled override selects subtype `4/8`.
  - latest rerun on the restored compiled default reconfirms the exact same stable baseline with a second independent timestamp:
    - `bin/logs/net_packets.log` shows `tick=273 source=builtin-challenge-interaction`, then `tick=283 source=builtin-battle-operate responseLen=23`, again `hex=575400170101040900001206726573756c740003000101`.
    - matching `bin/logs/net_trace.log` shows dispatch `kind=4 subtype=9` at `tick=284`, then `trace_battle_kind4_subtype9_flow` reaches `0x05189BF0` and returns through `0x05189C02 -> 0x05189B18` with unchanged `gateBytes=2,0,0,0,0`.
    - no `sub_4B70_battle_apply_entry`, no subtype-8 flow marker, and no post-attack follow-up WT request appear after that ack.
  - another independent rerun now reconfirms the same baseline a third time:
    - `bin/logs/net_packets.log` shows `tick=228 source=builtin-challenge-interaction`, then `tick=247 source=builtin-battle-operate responseLen=23`, still `hex=575400170101040900001206726573756c740003000101`.
    - matching `bin/logs/net_trace.log` shows dispatch `kind=4 subtype=9` at `tick=248`, then `trace_battle_kind4_subtype9_flow` again reaches `0x05189BF0` and returns through `0x05189C02 -> 0x05189B18` with unchanged `gateBytes=2,0,0,0,0`.
    - no subtype-8 trace, no `trace_battle_actioninfo_parser_detail`, no `sub_4B70_battle_apply_entry`, and no post-attack follow-up WT request appear in that session either.
  - purpose of the new traces: the next manual `4/8` experiment should be able to distinguish “`info` is just missing bytes” from “`info` is the wrong container shape”, without writing Battle globals or patching the client parser.

Runtime result with `result=4`:

- confirmed:
  - the unhandled/assert path is gone. Packet log shows `source=builtin-challenge-interaction responseLen=23 WT len=60 hdr=4/1 objs=1/4/1(id=105) count=1`.
  - response bytes are the intended `1/4/14 { result=4 }`: `mock_challenge_interaction_response response=4/14 result=4 len=23`.
  - no `unhandled_packet`, `assert(0)`, `scene_widget_timeout_message`, `count=10 cap=10`, or immediate-flush marker appears in this run.
- correction:
  - the user-visible "玩家不存在" prompt is explained by IDA `net_handle_login_or_name_result()` (`0x0101258A`): subtype `14` subtracts `2` from `result`; `result=4` lands on the `gbk_msg_player_not_found` branch at `0x01012632`.
  - the mock now returns `result=1`, which stays in the confirmed `4/14` parser family but falls through the default no-popup branch.
- caveat:
  - this still does not implement the real success/battle-start response. It only removes a misleading local failure prompt while preserving packet-layer coverage.

Composite `4/1 + 2/1` request correction:

- confirmed runtime evidence:
  - the client can batch a hotspot/challenge object and a movement upload in the same WT packet.
  - newest unhandled packet: `WT len=88 hdr=4/1 objs=1/4/1(id=106),1/2/1 count=2`.
  - decoded fields include `id=106`, `index=4`, `posx=327`, `posy=352`, followed by the usual `moveinfo` blob.
- corrected mock:
  - `builtin-challenge-interaction` now accepts one `1/4/1` object plus an optional `1/2/1 moveinfo` object in the same request.
  - unknown `4/1` IDs still return the parser-safe placeholder `1/4/14 { result=3 }`.
  - when `moveinfo` is present in the same request, the response also appends an empty `1/2/1` ack and snapshots the current player position through the normal movement persistence path.
- status:
  - confirmed: the assert was caused by WT object batching, not a new `4/1` field grammar.
  - hypothesis: `id=106,index=4` success semantics remain unknown; do not map it to scene entry until runtime/static evidence identifies a target.

Generic split-safe combo fallback:

- current mock now has a conservative fallback after all dedicated combo handlers have failed.
- behavior:
  - split only whitelisted objects that are already known to be parser-safe as standalone requests.
  - build a temporary one-object WT request for each whitelisted object.
  - pass each temporary request through the existing response builder.
  - merge all child response objects into one WT response.
- current split-safe whitelist:
  - `2/1 moveinfo`
  - `2/10 otherinfo refresh`
  - `4/1 challenge/hotspot`
  - `7/7 type`
  - `7/18 practise-info`
  - `25/5 scene-default`
  - latest follow-up evidence after the blob-wrapped subtype-8 success path:
    - `confirmed`: the immediate post-attack short `25/5` is emitted by Battle.cbm itself, not by a generic scene-only tick. Runtime shows `trace_battle_outgoing_request_source label=battle_send_operate_4_2_entry pc=05184a70 ... regs=...,00000019,00000005` right after `sub_4B70_battle_apply_entry`.
    - `confirmed negative`: the old generic fallback `1/25/12 { result=4 }` is battle-wrong. Runtime shows the main dispatcher consumes it as `kind=25 subtype=12`, and main runtime state is already `sceneResult=4` before fallback re-enters Battle.cbm `sub_17AC()` (`tick=231` evidence in `bin/logs/net_trace.log`).
    - `confirmed`: after that dispatch, fallback callback `051836CD` still re-enters Battle.cbm event-7 dispatcher `sub_17AC()` at `0x051836CC`, but local flow only reaches `0x05183718`; no later `0x05183E02/0x05183E34/0x051839BE` runtime marker fires before control returns to scene/runtime ticks.
    - `confirmed negative`: the blob-wrapped subtype-8 response is enough to reach `0x05189D82 -> 0x05189DB4 -> sub_4B70`, but local action state still stays at `actionBytes=1,1,0,0,0,0`, there is still no `sub_actioninfo_parser_*`, and the client remains in the loading/widget loop after the old generic `25/12`.
    - `changed trace-only`: `src/main.c` now widens `trace_battle_sub17ac_flow` from only `kind=25 subtype=2` to the currently observed `kind=25` family, including `subtype=12`, so the next rerun can prove whether battle `25/12` exits through the early clear tail or reaches a later battle-local handler.
    - `confirmed static`: Battle.cbm `sub_17AC()` also has a later common-worker block at `0x05183E02..0x05183E42`. It compares `[sb+0x2c]->0x10` with loop counter `[sp+0x538]`, jumps back to record-loop body `0x0518371A` if more entries exist, and otherwise falls through `0x05183E18 -> 0x05183E34 -> 0x051839BE`. Evidence: `python tools/disasm_thumb.py --file bin/JHOnlineData/mmBattleMstarWqvga.cbm 0xA1 0x05181F20 0x05183DE0 120`.
    - `changed trace-only`: the same flow trace now also covers offsets `0x05183E02..0x05183E43` plus tail `0x051839BE`, with explicit probes at `0x05183E02`, `0x05183E1A`, `0x05183E34`, and `0x051839BE`, so the next rerun can distinguish empty-list cleanup from real record iteration.
    - `changed default response`: `src/main.c` now answers the generic short scene-default fallback with same-subtype `1/25/5 { result=4 }` instead of `1/25/12 { result=4 }`, because repo evidence already marked `1/25/5 { result=4 }` as parser-safe while the old subtype-12 shell now has confirmed battle-local negative side effects.
    - `confirmed improvement`: with the new default, the immediate post-attack continuation is now dispatched as `kind=25 subtype=5` while `sceneResult` remains `0` before and after dispatch (`tick=178` runtime evidence in `bin/logs/net_trace.log`).
    - `confirmed remaining gap`: the same `25/5` reply is still not sufficient to advance battle locally. Main dispatcher falls back into Battle.cbm `sub_17AC()` after `kind=25 subtype=5`, then returns to the loading/widget loop with no visible attack action.
    - `changed trace-only`: `trace_battle_sub17ac_flow` activation now also includes `kind=25 subtype=5`, so the next rerun can recover Battle.cbm's actual local branch for the safer same-subtype response.
    - `confirmed negative`: the recovered subtype-5 branch is the same early skeleton as the old subtype-12 fallback. Runtime trace at `tick=174` runs only through `0x051836CC..0x05183718`; no later `0x05183E02+` or `0x051839BE` marker appears.
    - `confirmed static`: Battle.cbm `sub_17AC()` gate at `0x051836EE..0x05183718` is controlled by the callback return in `r0`. Static disassembly shows `cmp r0,#0`; zero takes the setup path `0x051836F6..0x05183718`, while nonzero would branch to `0x051837DA -> 0x05183E1A`.
    - `changed trace-only`: `src/main.c` now also logs `trace_battle_sub17ac_gate` at `0x17EE/0x17F0/0x37DA/0x37DC` so the next rerun can identify the callback/context that decides whether subtype-5 remains on the zero-return no-op path.
    - `confirmed runtime`: on the latest safe rerun, that gate reports `trace_battle_sub17ac_gate ... pc=0518370e/05183710 ... r4=010346e1 r6=01053408 ctx2c=01056150`, and by `0x05183710` the compare input is already `r0=0`. Evidence: `bin/logs/net_trace.log` tick `256`.
    - `confirmed runtime`: the new `ctx2c` dump disproves the earlier “empty container” guess. Battle receives a parsed shared-event container with `totalLen=23`, `objectCount=1`, `entryCount=1`, `base=05001860`, and the first entry is exactly `major=1 kind=25 subtype=5 fieldCount=1`. Evidence: `trace_shared_event_container` / `trace_shared_event_container_entry` at tick `327` in `bin/logs/net_trace.log`.
    - `corrected runtime interpretation`: `trace_battle_sub17ac_next_pc` now proves the live path does continue from `0x05183718` into `0x05183E02`. The earlier missing common-loop probes were caused by using stale relative offsets (`0x3E02`/`0x39BE`) instead of real offsets from loaded Battle base `0x05181F20` (`0x1EE2`/`0x1A9E`). Evidence: `trace_battle_sub17ac_next_pc from=05183718 next=05183e02 off=1ee2` in `bin/logs/net_trace.log`.
    - `confirmed static`: `0x051836D8..0x051836EE` loads the callback from `[globalObj+0x14]` and calls it as `callback(r0=[sb-0xCC+0x2C], r1=packetPtr, r2=0x0A, r3=0x12)`. The later loop at `0x0518371A..0x0518372E` walks `0x58`-byte records from `[sb+0x2C]->0x18` and dispatches on `entry+0x04`. Evidence: `python tools/disasm_thumb.py --file bin/JHOnlineData/mmBattleMstarWqvga.cbm 0xA1 0x05181F20 0x051836C0 80`.
    - `confirmed static`: main CBE `0x01034714..0x01034778` is the shared `WT` parser, `0x0103477A..0x010347D2` initializes the parser container, and `0x010346B4..0x01034712` serializes the same container back out. The battle-side `ctx2c=01056150` therefore looks like a shared parsed-event container, not a battle-only gate flag block. Evidence: `python tools/disasm_thumb.py --file bin/CBE/江湖OL.cbe 0x0 0x01000000 0x01034580 220` and `0x01034770 140`.
    - `confirmed static + runtime`: the first-record dispatch now recovers the concrete battle-negative branch for same-subtype `25/5`. Static `0x05183730..0x0518377E` handles only record kinds `4`, `0x1E`, `0x1C`, `0x1B`, `0x0E`, and `0x14`. The latest runtime trace shows the one parsed post-attack record is `kind=0x19` (`25` decimal), so it falls through that handled set and reaches the generic branch at `0x0518382C -> 0x05183C10`. Evidence: `trace_battle_sub17ac_flow` at tick `166` (`pc=0518372E ... r1=00000019`, then `05183746/48/52/54/5E/60/6A/6C`) plus `python tools/disasm_thumb.py --file bin/JHOnlineData/mmBattleMstarWqvga.cbm 0xA1 0x05181F20 0x05183718 180`.
    - `confirmed runtime`: that generic branch is battle-wrong for the current continuation. Immediately after `kind=25` falls through, Battle enters `0x05182940` challenge/prompt fallback instead of any local action consumer: `trace_battle_challenge_source_branch label=challenge_fallback_method_before/after pc=05182940/0518294A`, followed by `trace_prompt_hotspot_candidate` and `trace_same_class_scene_table` on the current outdoor scene. This matches the user-visible “退战/空弹窗/回场景循环” symptom rather than an attack animation path. Evidence: `bin/logs/net_trace.log` tick `166`.
    - `confirmed static`: subtype-8 success still does not materialize any action records before that wrong continuation. `sub_4B70` (`0x05184B70`) merges per-slot state around `activeBlock+0x500/+0x520`, and only later emits `alloc_outgoing_game_event(25,5)` from `0x05184DAA..0x05184DC0`. The latest runtime state at `sub_4B70_battle_apply_entry` is only `actionBytes=1,1,0,0,0,0` while `trace_battle_local_action_table ptr=010534f4` remains all zero bytes. Evidence: `trace_battle_local_action_state label=sub_4B70_battle_apply_entry` at tick `165`, plus `python tools/disasm_thumb.py --file bin/JHOnlineData/mmBattleMstarWqvga.cbm 0xA1 0x05181F20 0x05184B70 220`.
    - `working hypothesis`: the next missing contract is upstream of post-attack `25/5`. Same-subtype `25/5 { result=4 }` is still only a parser-safe shell; real progress now depends on recovering a `4/8.info` payload that populates the `sub_4B70` merge windows or otherwise causes the hidden `actioninfo/actionnum` importer path to run before Battle falls back into the generic `kind=25` scene/challenge branch.
    - `changed trace-only`: `src/main.c` now logs `trace_battle_apply_detail` at `sub_4B70` entry and right before the internal `alloc_outgoing_game_event(25,5)` call (`0x05184DB4/0x05184DC0`). It dumps `localBase+0x9B0`, `activeBlock+0x500`, and `activeBlock+0x520` so the next rerun can show whether any action-stage structure exists before the wrong `25/5` continuation is sent.
    - `new runtime narrowing`: the latest rerun disproves the earlier “action windows are still empty” hypothesis. At `sub_4B70_battle_apply_entry`, `trace_battle_apply_detail` shows `stateWindow=01053e98 flagsState=1,1`, and both `activeStageBlock=01053aac` and `activeActionBlock=01053acc` already carry nonzero structured bytes before the later `25/5` send. By `sub_4B70_send25_prepare/call`, those same windows remain nonzero while `flagsState` has dropped to `0,0`. Evidence: `bin/logs/net_trace.log` tick `2840`.
    - `corrected implication`: the current `4/8.info` blob is not merely failing to import. It is materializing some battle-local state, but Battle still converts that state into a short follow-up event which the current mock answers with the wrong semantic family.
    - `battle-only response experiment`: `src/main.c` now treats short empty `25/5` differently when the active screen is confirmed Battle.cbm via `vm_infer_battle_module_from_screen()`. Instead of returning same-subtype `1/25/5 { result=4 }`, it returns a single-object `1/4/7` built by the existing battle status helper.
    - `rationale`: Battle `sub_17AC()` explicitly handles first-record `kind=4`, while the current same-subtype `kind=25` continuation has confirmed static/runtime negative evidence: it falls through the handled set and enters `0x05182940` challenge/prompt fallback.
    - `new static correction`:
      - main CBE business dispatch jump table at `0x01012F4C` routes `kind=25` to handler `0x01010C5A`.
      - `0x01010C5A` only has explicit subtype branches for `3`, `6`, and `12`; there is no direct main-CBE local branch for `kind=25 subtype=5`.
      - this matches the latest runtime: after the post-attack short reply is dispatched as `kind=25 subtype=5`, the trace immediately reaches `trace_followup_scan_enter` and then Battle fallback `sub_17AC()`, where first-record `kind=25` again falls through the handled set into `0x05182940`.
      - evidence:
    - `confirmed negative follow-up`:
      - the later battle-only `25/6 { result=1, count=1, msg="attack" }` experiment is wire-valid but still battle-wrong.
      - runtime evidence: latest runs dispatch `kind=25 subtype=6` at `tick=179/243`, but Battle still immediately re-enters `trace_battle_challenge_source_branch ... pc=05182940/0518294A`.
      - corrected policy: remove battle-only `25/6` from the compiled build and restore short `25/5` to the older parser-safe same-subtype shell while shifting the next live experiment back into the operate-window `4/6` path.
  - new post-death follow-up request:
    - after the subtype-6 action/apply path, the client can display prompt text `您已经死亡，是否进入商城购买复活石?`.
    - when the user clicks that prompt, the client emits a short one-object request `WT len=21 hdr=7/14 objs=1/7/14(result=2)`.
    - static evidence for the prompt source:
      - main CBE `sub_103838A()` at `0x0103838A` handles `kind=27 subtype=13`.
      - its `result==2` branch at `0x010384B4..0x010384CE` calls `sub_10110E6()` with callback `sub_10108F4()`, which is the exact prompt-button handler seen in runtime.
    - current mock policy:
      - the real server-side mall/revive contract after `7/14(result=2)` is still unknown.
      - the mock now returns a same-packet echo for short `7/14` so manual runs no longer die on server-side `assert(0)` while this follow-up branch is being recovered.
        - static: `江湖OL.CBE` `0x01012F4C`, `0x01010C5A..0x01010CCC`.
        - runtime: `bin/logs/net_trace.log` ticks `161`, `170`, `171`.
    - second-stage follow-up now visible after that prompt click:
      - latest runtime can continue into `WT len=25 hdr=1/14 objs=1/1/14(actorId=0)`.
      - runtime evidence:
        - `bin/logs/net_packets.log` shows `send_payload len=25 ... hdr=1/14 ... field actorId=0`, then `unhandled_packet WT len=25 hdr=1/14 objs=1/1/14 count=1`.
        - `bin/logs/net_trace.log` shows the send immediately after the scene-loading callback chain (`trace_scene_loading_callback_gate` around `0x01013BDC..0x01013C05`), not from Battle.cbm's own `4/*` parser window.
      - raw binary evidence:
        - `rg -a "actorId"` finds camel-case `actorId` in `bin/JHOnlineData/mmShopMstarWqvga.cbm`; this differs from the main-CBE title/login family field spelling `actorID`.
      - current mock policy:
        - the compiled build now handles this exact one-object `1/14(actorId=...)` request as a same-packet echo only.
        - status: `hypothesis`.
        - rationale: remove the mock-side `assert(0)` without inventing shop payload fields before the real `1/14` consumer is recovered.
    - `new compiled experiment`:
      - battle-only short `25/5` requests are now answered with `1/25/6 { result=1, count=1, msg="attack" }`.
      - status: `hypothesis`.
      - rationale: `25/6` is the narrowest `kind=25` subtype with a confirmed main-CBE local handler, and the non-empty `msg` avoids repeating the old blank-popup family while we test whether the missing continuation is an info/message contract rather than a same-subtype no-op.
    - `latest runtime correction`:
      - the `25/6` experiment is now confirmed wire-active but still battle-wrong.
      - packet evidence: latest session replies to the battle short request with `response=25/6 result=1 count=1 msg=attack` at `tick=242`, and the next queued event is dispatched as `kind=25 subtype=6` at `tick=243`.
      - runtime negative: even after that `kind=25 subtype=6` dispatch, main flow still reaches `trace_followup_scan_enter`, `fallback_exit`, Battle fallback entry `0x051836CC`, and then `0x05182940` challenge/prompt fallback on the same tick.
      - therefore `25/6` is not by itself the missing attack-continuation contract; it is at best another parser-valid global `kind=25` family object.
      - evidence:
        - `bin/logs/net_packets.log` ticks `242..243`
        - `bin/logs/net_trace.log` ticks `242..243`
    - `next trace-only narrowing`:
      - `src/main.c` now also traces main-CBE `kind=25` handler entry/sub-branches at `0x010109EA`, `0x01010BE6`, `0x01010C5A`, and `0x01010CC6`.
      - goal: next rerun should prove whether the `kind=25 subtype=6` packet actually executes the local subtype-6 branch or is skipped before handler-local work, so we stop guessing among `25/*` subtypes blindly.
    - `status`: this is still `hypothesis` until the next rerun confirms whether battle-only short `25/5 -> 4/7` reaches `sub_17AC` first-record kind-4 dispatch and whether it avoids the current prompt-fallback loop.
  - `99/1 short control echo`
- explicit boundary:
  - this is a last-resort unhandled-packet fallback; it does not run before dedicated handlers such as scene-change, type-27 follow-up, scene-resource follow-up, or actor-other portal handling.
  - do not add scene-change families such as `2/3 mapID/exitID`, `12/1+7/42`, or `27/*` to the split whitelist without fresh evidence, because those packets often have same-window ordering semantics.

Latest stuck-scene correction:

- confirmed runtime evidence:
  - on the stuck run, no `2/3 mapID/exitID` scene-change request was emitted at the failure point. The packet tail instead shows periodic `2/10 Type=1` refreshes and the earlier `4/1 id=105,index=1,posx=142,posy=310` interaction.
  - the local scene is `01桃花岛_01.sce`; `trace_actor_move_entry_table` reports two portal move entries and `statusText=桃花岛-北面桃林`, so the base `.sce` portal table was parsed.
  - `tools/inspect_sce.py bin/JHOnlineData/01桃花岛_01.sce` parses the bottom edge portal as target `01桃花岛_02.sce`, `target_entry_id=0`, trigger rect `(208,432,256,448)`. The persisted/current player position in the same run reaches that portal area.
- static evidence:
  - `send_game_event_type()` (`0x0100EDA0`) constructs the standalone `2/10 Type=<u8>` request. `sub_1012958()` (`0x01012958`) confirms the current empty `othernum=0, otherinfo=<empty>` response is parser-safe, but it does not synthesize portal map changes.
  - `net_handle_login_or_name_result()` (`0x0101258A`) treats `4/14 result=1` as the default no-message branch. The real success/battle-start/special-scene continuation is still unknown.
- corrected mock:
  - `vm_net_mock_get_scene_change_target()` now includes桃花岛 scene-change spawn points recovered from local `.sce` portals. Confirmed examples include `01桃花岛_02.sce exitID=0 -> (80,60)` and the adjacent `01桃花岛_01/03/04` edge-entry coordinates.
  - `builtin-challenge-interaction` no longer returns success-shaped `result=1`; it returns `4/14 result=3`, which lands in a confirmed local failure branch instead of entering an unimplemented success continuation.
- status:
  - confirmed: empty `2/10` was not itself a map-enter trigger in this stuck run.
  - confirmed: `result=1` is not enough evidence for a safe `4/1` success flow.
  - hypothesis: some桃花岛 `target_entry_id` values that do not directly match a parsed `entry_id` are approximated to the nearest scene edge spawn until a live request/response trace proves the exact mapping.

Follow-up correction from the next stuck run:

- confirmed runtime evidence:
  - the live `4/1` request is `id=105,index=4,posx=108,posy=448`, still with no later `2/3 mapID/exitID`.
  - responding with `4/14 result=3` dispatches through the kind-4 parser (`trace_business_dispatch_item ... kind=4 subtype=14`), but only the wrapper block gate clears; `loadingGate_R9_5530` remains `1` and the center loading strip continues.
  - therefore `4/14` failure values are parser-shaped but not a correct completion response for this hotspot.
- static evidence:
  - IDA `parse_scene_response_entry()` (`0x010396D6`) consumes `kind=30 subtype=1` as `scene + posinfo` and updates scene/position through the normal scene-enter path.
  - `net_business_response_dispatch()` routes kind `30` objects to `net_handle_scene_channel_dispatch`, so a `30/1` response can be carried on the same normal business event.
- corrected mock:
  - the known桃花岛 hotspot `4/1 id=105,index=4,posy>=400` while current scene is `01桃花岛_01.sce` now returns a single `1/30/1 { scene=01桃花岛_02.sce, posinfo=(80,60) }`.
  - this is a narrow server-side interpretation of the hotspot request. Unknown `4/1` requests still fall back to the temporary `4/14 result=3` failure placeholder.

Latest no-trigger rerun:

- confirmed runtime evidence:
  - this run did not emit the previous `4/1 id=105` hotspot packet and did not emit `2/3 mapID/exitID`.
  - the final movement upload snapshot is `scene=01桃花岛_01.sce pos=229,410`.
  - `tools/inspect_sce.py bin/JHOnlineData/01桃花岛_01.sce` parses the bottom edge portal trigger rect as `(208,432)-(256,448)` with spawn point `(230,425)` and target `01桃花岛_02.sce`.
- conclusion:
  - confirmed: the client stopped above the actual trigger rectangle on this run, so the lack of a later scene-change request is explained by local trigger geometry rather than a missing `4/1` response.
  - hypothesis: the server/live path may complete this edge portal from the movement upload when the avatar reaches the portal approach band, or the current emulator pathing/coordinate restore leaves the actor slightly short of the rectangle.
- current mock:
  - `2/1 moveinfo` still returns the confirmed empty subtype-1 ack by default.
  - only when current scene is `01桃花岛_01.sce` and the uploaded/snapshotted actor position is inside the narrow bottom-portal approach band `x=208..256, y=408..448`, it returns `1/30/1 { scene=01桃花岛_02.sce, posinfo=(80,60) }` and saves that scene/position.
  - this does not patch client movement, draw, parser, or scene globals; it is a server-response fallback for a single evidenced portal.

Correction after actor-motion trace:

- confirmed runtime evidence:
  - `trace_current_actor_motion_state` shows the actor moves from `grid=229,410` to `grid=229,426` and then `grid=229,434`.
  - `grid=229,434` is inside the local bottom portal trigger rect `(208,432)-(256,448)`.
  - after entering the rect, the client still emits only periodic `2/10 Type=1` refreshes; it does not emit `2/1 moveinfo`, `2/3 mapID/exitID`, or `4/1 id=105`.
- corrected mock:
  - the same single-portal fallback is now also checked on `2/10 Type=1`.
  - by default `2/10` still returns empty `othernum=0`; only when current scene is `01桃花岛_01.sce` and the snapshotted actor position is inside `x=208..256,y=432..448`, the response becomes `1/30/1 { scene=01桃花岛_02.sce, posinfo=(80,60) }`.
  - evidence status: confirmed for this local client/resource behavior; still a hypothesis for the original server's exact choice of response family.

Split-completion correction from the next rerun:

- confirmed runtime evidence:
  - the `2/10` portal response did dispatch successfully at first: `mock_actor_other_portal_response ... len=54`, followed by `trace_business_dispatch_item ... kind=30 subtype=1` at tick `237`.
  - consuming that immediate full `30/1 scene+posinfo` set `sceneState=7` and closed `sceneObj+0x164` (`trace_scene_dispatch_gate_write ... old=1 new=0 pc=01039766`), but the client stayed on the old scene for a roughly 30-tick transition countdown.
  - at tick `266..268` the client emitted only another `2/10 Type=1`; the previous mock returned empty `2/10`, and business dispatch hit `early_gate_off` because the scene dispatch gate was already closed.
  - later `actor-other-portal-check` calls also polluted the persisted state by saving live source coordinates under the already-saved target scene, e.g. `scene=01桃花岛_02.sce pos=229,434`.
- corrected mock:
  - the `2/10` portal trigger no longer snapshots/saves position just to check the rect; it reads the live actor grid without mutating persisted state.
  - the immediate portal response now mirrors the already verified split scene-change pattern: `1/30/2 { result=1,type=2,scene=01桃花岛_02.sce }` with no `posinfo`, and records a pending target `(80,60)`.
  - the following normal `2/10` response, while the pending target is valid, returns the deferred completion bundle used by the scene-change follow-up path: task/other/banner side-family objects plus `27/12`, `27/11`, `27/4`, and full `30/2 { result,type,scene,posinfo }`.
- status:
  - confirmed negative: immediate full `30/1` is parser-valid but incomplete for this portal because it closes the dispatch gate before the follow-up `2/10` can be consumed.
  - hypothesis pending rerun: split `30/2` ack plus deferred full `30/2` on the next `2/10` should preserve the dispatch gate long enough to enter the target map, matching the existing `2/3 mapID/exitID` split-completion evidence.

Newest adjacent-map portal correction:

- confirmed runtime evidence:
  - the latest run starts in `01桃花岛_02.sce` from persisted state and actorinfo confirms that scene key.
  - the actor motion trace reaches `grid=321,314`; packet traffic after that is only ordinary `2/1` movement acks and periodic empty `2/10 Type=1` responses.
  - no `2/3 mapID/exitID` or `4/1` hotspot request is emitted for this edge portal.
- local `.sce` evidence:
  - `tools/inspect_sce.py bin/JHOnlineData/01桃花岛_02.sce` parses the east edge portal as target `01桃花岛_03.sce`, trigger rect `(320,272)-(352,336)`, spawn point `(305,310)`, and `target_entry_id=3`.
  - `grid=321,314` is inside that trigger rect.
- corrected mock:
  - the `2/10` portal fallback is no longer hardcoded to only `01桃花岛_01.sce -> 01桃花岛_02.sce`.
  - it now uses a small local `.sce`-derived portal table for the adjacent桃花岛 edge portals; the current confirmed failing case is `taohuadao-02-east-to-03`, which returns the same split `30/2` ack and deferred completion already used by the first portal.
  - target spawn coordinates for adjacent reverse-edge pairs are derived from the paired local scene portal spawn; for ambiguous `target_entry_id` values this remains a `hypothesis` until a real server trace confirms exact spawn selection.

Follow-up after the next rerun:

- confirmed runtime evidence:
  - the portal fallback now fires: `mock_actor_other_portal_response ... portal=taohuadao-02-east-to-03 ... target=01桃花岛_03.sce targetPos=40,70`.
  - the immediate response is consumed as `kind=30 subtype=2`; it sets scene-side state and triggers a short `25/5` default-scene request, but no target-map enter/load follows.
  - the deferred response is also consumed and reaches `kind=30 subtype=2`; after it, runtime reports `sceneState=7` and the dispatch gate closes, but the actor remains on `01桃花岛_02.sce`.
- correction:
  - `30/2` is sufficient as the first split ack, but the deferred completion should finish with the normal scene-enter consumer (`30/1 scene+posinfo`) rather than a second full `30/2`.
  - `mock_actor_other_deferred_scene_response` now appends a trailing scene-aware `30/1` while preserving the immediate `30/2` ack.
- status:
  - confirmed negative: `30/2` ack plus deferred full `30/2` does not live-switch the `01桃花岛_02.sce -> 01桃花岛_03.sce` edge portal.
  - hypothesis pending rerun: `30/2` ack plus deferred trailing `30/1` should drive the same parser used by normal scene-enter responses.

Second follow-up:

- confirmed runtime evidence:
  - the deferred trailing `30/1` is consumed (`trace_business_handler ... kind=30 subtype=1`), so the remaining failure is not a gate-off before `30/1`.
  - however, the same response processes `27/12` before `30/1`; `27/12` closes the scene dispatch gate at `0x0100EA6A` and shows the user-visible prompt family.
  - after the deferred response, the live actor remains on the source portal coordinates, e.g. `grid=40,54`, instead of the saved target `01桃花岛_02.sce pos=305,310`.
- correction:
  - the `2/10` portal deferred response no longer includes the `27/12`, `27/11`, or `27/4` prompt/target family.
  - it now sends the parser-safe task/other subset followed directly by trailing `30/1 scene+posinfo`.
- status:
  - confirmed negative: placing `27/12` before the deferred `30/1` is not correct for this actor-other edge-portal completion.
  - hypothesis pending rerun: removing the `27/*` prompt family should allow `30/1` to be the gate-closing scene-enter object for this path.

Third follow-up:

- confirmed runtime evidence:
  - the actor-other portal rect detection is still working: `mock_actor_other_portal_response ... portal=taohuadao-02-east-to-03 ... pos=321,310 target=01桃花岛_03.sce targetPos=40,70`.
  - the immediate split ack is consumed as `kind=30 subtype=2` at tick `390`, then the client immediately emits a short `25/5` default-scene request at tick `390`.
  - the current mock answers that short `25/5` with only `25/12 result=4`; the scene-aware completion is delayed until the next periodic `2/10` at tick `420`.
  - the delayed `2/10` completion consumes trailing `30/1`, but no `trace_actor_motion_descriptor_context` or local resource-open for `01桃花岛_03.sce` follows, and the actor remains on the old live scene coordinates.
- conclusion:
  - confirmed negative: putting actor-other portal completion only in the next periodic `2/10` response is too late or the wrong continuation window for this path.
  - hypothesis: the short `25/5` emitted immediately by the `30/2` handler is the expected actor-other portal completion window.
- corrected mock:
  - pending scene-change targets now remember whether they came from the actor-other portal fallback.
  - only for that source, `builtin-scene-default-event` responds to the immediate short `25/5` with `1/30/1 { scene=<target>, posinfo=<targetPos> }` and clears the pending target.
  - ordinary explicit `2/3 mapID/exitID` scene changes keep the existing non-actor-other pending flow.
- status:
  - hypothesis pending rerun: completing through the immediate `25/5` should give the client the scene-enter object before the later periodic `2/10` loop resumes.

Crash correction:

- confirmed runtime evidence:
  - the crash run did not reach the new actor-other portal completion path. Packet traffic stops during initial scene startup; no `mock_actor_other_portal_response` or `mock_scene_default_event_actor_other_portal_completion` appears.
  - startup loaded `mock_player_pos_load scene=01桃花岛_03.sce pos=293,310` from `nvram/jhol_mock_player_pos.bin`.
  - stdout reports an access fault at `pc=0104521C`, address `0x4C`; IDA identifies `sub_10451C2()` as a collision/grid table query that indexes through `a1[6]`.
- conclusion:
  - confirmed: the immediate split `30/2` actor-other portal ack must not persist the target scene before a real completion object is accepted.
  - hypothesis: the poisoned saved state paired source-map coordinates with the target scene, producing an invalid collision/grid lookup on the next startup.
- corrected mock:
  - `builtin-actor-other-portal` now only remembers the pending target after the split `30/2` ack.
  - the target scene/position is saved only when the mock actually emits scene-aware `30/1 scene+posinfo` completion, either on the immediate short `25/5` path or the remaining deferred `2/10` fallback.

Fourth follow-up:

- confirmed runtime evidence:
  - the repaired startup state reaches `01桃花岛_02.sce` and the actor is visible at `grid=305,310`.
  - the east-edge portal fallback still fires: `mock_actor_other_portal_response ... portal=taohuadao-02-east-to-03 ... pos=321,310 target=01桃花岛_03.sce targetPos=40,70`.
  - the split `30/2` ack is consumed, and the immediate short `25/5` response carrying a single `30/1 scene+posinfo` is also consumed.
  - after that, no `trace_actor_motion_descriptor_context` for `01桃花岛_03.sce` appears. The live actor remains on `01桃花岛_02.sce`, and the client emits a new `4/1 id=3,index=4,posx=299,posy=237` request.
  - the generic `4/14 result=3` reply to that later request is queued after the scene dispatch state is no longer in the expected completion window, leaving `loadingGate_R9_5530=1`.
- conclusion:
  - confirmed negative: `30/2` ack plus immediate single-object `30/1` is parser-valid but still incomplete for this actor-other portal.
  - hypothesis: the immediate `25/5` continuation should include the same parser-safe task/other synchronization subset used by other scene-resource follow-up paths before the final `30/1 scene+posinfo`.
- corrected mock:
  - the actor-other short-`25/5` completion now returns task-list/actor-other/info-banner side objects followed by trailing `30/1 scene+posinfo`, instead of a lone `30/1`.
  - this keeps the change in the server response stream and does not write client globals or patch the scene/draw logic.

2026-06-10 follow-up for the later encounter/hotspot request:

- confirmed runtime evidence:
  - the latest run still reaches `01桃花岛_02.sce` and later emits `WT len=60 hdr=4/1 objs=1/4/1(id=3) count=1`.
  - nearby trace shows the actor moving around the same east-side transition area after the earlier `taohuadao-02-east-to-03` server-assisted portal path.
  - the generic `builtin-challenge-interaction` response is still `4/14 result=3`; it is parser-shaped but does not provide a scene/battle continuation and leaves the loading strip path active for that interaction window.
- negative runtime evidence:
  - after adding the special-scene branch, the next manual monster touch still emitted `WT len=60 hdr=4/1 objs=1/4/1(id=3) count=1`, and the mock answered it as `source=builtin-special-scene-interaction responseLen=54`.
  - the response was `1/30/1 { scene=01桃花岛_03.sce, posinfo=(40,70) }`; the client showed the progress strip and consumed `kind=30 subtype=1`, but did not enter a battle screen.
  - no `mock_challenge_interaction_response response=4/10` appeared in that run, proving the special-scene branch was masking the Battle.cbm parser experiment.
- corrected mock:
  - the `01桃花岛_02.sce` `4/1 id=3,index=4` special-scene branch is withdrawn.
  - the same request now falls through to generic `builtin-challenge-interaction`, which returns the Battle.cbm-shaped `1/4/10 { side=1, battleinfo=<raw typed stream> }` response.
- status:
  - confirmed negative: `id=3,index=4` on `01桃花岛_02.sce` should not currently be treated as a scene-enter response.
  - hypothesis pending rerun: it is the monster/encounter path that should exercise the Battle.cbm `4/10 battleinfo` parser.

Latest `01桃花岛_03.sce` bottom portal rerun:

- confirmed runtime evidence:
  - the newest run starts from saved `01桃花岛_03.sce pos=116,398`; actorinfo and `trace_actor_motion_descriptor_context` confirm the live scene is `01桃花岛_03.sce`.
  - the earlier `4/1 id=4,index=4,posx=211,posy=444` was handled by the generic `4/14 result=3` fallback and did not enter a map.
  - after that, the actor reaches the bottom portal area: `trace_current_actor_motion_state` reports positions including `grid=208,558`, `grid=208,574`, and nearby values.
  - packet traffic in that phase is only `2/1 moveinfo` uploads; no `2/10 Type=1` refresh appears, so the previous actor-other-only portal fallback cannot run.
- local `.sce` evidence:
  - `tools/inspect_sce.py bin/JHOnlineData/01桃花岛_03.sce` parses the bottom edge portal as target `01桃花岛_04.sce`, trigger rect `(160,553)-(240,570)`, spawn `(200,540)`, target entry `0`.
  - `tools/inspect_sce.py bin/JHOnlineData/01桃花岛_04.sce` parses the reverse north portal spawn as `(42,60)`, so the mock continues to use `(42,60)` as the current best target-side spawn hypothesis for `01桃花岛_03.sce -> 01桃花岛_04.sce`.
- corrected mock:
  - `2/1 moveinfo` handling now also checks the same local `.sce`-derived portal table, with a small `8px` margin for sampled movement-upload positions around the rect edge.
  - when matched, it returns the same split `30/2 { result=1,type=2,scene=<target> }` ack and records a pending target; the existing immediate `25/5` completion path remains responsible for emitting the final scene-aware `30/1`.
  - the target scene/position is still not persisted until completion, preserving the previous crash correction.
- status:
  - confirmed negative: relying only on periodic `2/10` refreshes misses this portal because the client emits only `2/1 moveinfo` while walking through it.
  - hypothesis pending rerun: split scene ack on the movement upload should give the client the same continuation window used by the actor-other portal path.

Correction after the next rerun:

- confirmed runtime evidence:
  - the `01桃花岛_03.sce -> 01桃花岛_04.sce` portal fallback now triggers through `2/10` at `grid=204,562`.
  - the split `30/2` ack is consumed and emits the immediate short `25/5`.
  - the short-`25/5` completion response is consumed as six objects: `6/1`, `6/13`, `6/14`, `2/10`, `25/5`, and trailing `30/1`.
  - the trailing `30/1` reaches `parse_scene_response_entry()` and closes the scene dispatch gate at `0x01039766`; post-dispatch state is `sceneState=7`.
  - no `trace_actor_motion_descriptor_context` for `01桃花岛_04.sce` follows, so emitting a parser-consumed `30/1` is still not confirmed as a live load.
  - the mock had saved `01桃花岛_04.sce` immediately when it emitted the completion packet; later movement snapshots then paired source-map live coordinates with the target scene.
- static evidence:
  - IDA `parse_scene_response_entry()` (`0x010396D6`) reads `scene` and `posinfo`, calls the scene object's vtable method at offset `0x74`, calls vtable offset `0x44`, sets `sceneState=7`, and clears the dispatch gate. It does not by itself prove the target `.sce` resource was opened.
- corrected mock:
  - actor-other portal completion no longer persists the target scene/position when the mock merely emits the completion response.
  - instead, it records a pending host-side save and commits it only if runtime later reaches `parse_actor_motion_descriptor` for the same target `.sce`.
  - this prevents the mock's persisted scene from running ahead of the client's actual loaded scene.
- status:
  - confirmed negative: "completion packet emitted" is not equivalent to "target scene loaded".
  - hypothesis pending rerun: the next clean log, without persisted-scene pollution, should isolate the remaining scene-load contract after the consumed `30/1`.

Latest gate-off rerun:

- confirmed runtime evidence:
  - in the last clean session, startup loaded `01桃花岛_03.sce pos=204,554`.
  - the bottom portal fallback fired from `2/10 Type=1`: `taohuadao-03-bottom-to-04`, source `01桃花岛_03.sce`, target `01桃花岛_04.sce`, target position `(42,60)`.
  - the split `30/2` ack was consumed and produced the short `25/5` continuation.
  - the short `25/5` completion was consumed as `6/1`, `6/13`, `6/14`, `2/10`, `25/5`, and trailing `30/1`.
  - the trailing `30/1` reached `parse_scene_response_entry()` and closed `sceneObj+0x164` at `0x01039766`, but no `parse_actor_motion_descriptor` trace for `01桃花岛_04.sce` followed.
  - later `2/10` and `2/1` portal responses were queued, but `net_business_response_dispatch` exited through `early_gate_off` because the scene dispatch gate was already `0`.
- static evidence:
  - IDA `parse_scene_response_entry()` (`0x010396D6`) calls the scene object callback at `0x01039754` as `sceneObj+0x74(scene,len,x,y,0)`, then calls `sceneObj+0x44(0)` at `0x0103975C`, then clears the dispatch gate at `0x01039766`.
  - IDA `parse_scene_posinfo_field()` (`0x01039770`) has the same `sceneObj+0x74/+0x44` path when `30/2` contains `posinfo`; the current split ack intentionally omits `posinfo`.
- added trace-only instrumentation:
  - `trace_scene_enter_apply` now logs the arguments and callback addresses at `0x01039754`, `0x0103975C`, `0x010397FC`, and `0x01039804`.
  - this is evidence gathering only; it does not mutate guest state or bypass client code.
- corrected mock hypothesis:
  - the actor-other portal short-`25/5` completion no longer ends with `30/1 scene+posinfo`.
  - it now ends with `30/2 result=1,type=2,scene,posinfo`, because IDA shows the `30/2` posinfo path runs the same scene-object apply callbacks and then continues through `sub_10491AE()`, which performs additional scene/loading cleanup and follow-up work that `30/1` does not perform.
- status:
  - confirmed negative: repeated portal responses after the consumed `30/1` are not sufficient, because the dispatcher is already gate-off.
  - hypothesis pending rerun: `task/other` subset plus trailing full `30/2` should exercise the scene-object apply path and the `sub_10491AE()` continuation in the same immediate `25/5` window.

Follow-up after trailing full `30/2`:

- confirmed runtime evidence:
  - the short-`25/5` completion now reaches the `30/2` posinfo parser path: `trace_scene_enter_apply ... label=scene30_2_obj_apply_0x74 ... scene=01桃花岛_04.sce len=15 x=42 y=60`.
  - the scene object callbacks are invoked with the expected target scene and spawn coordinates, then `parse_scene_posinfo_field()` closes the scene dispatch gate.
  - `parse_scene_posinfo_field()` continues into `sub_10491AE()`, which emits another short `25/5`; that later response is queued after the gate is already off and exits through `early_gate_off`.
  - no `trace_actor_motion_descriptor_context` for `01桃花岛_04.sce` follows, and runtime later reaches the same-current screen-manager path (`last=010182a6`) instead of opening the target `.sce`.
- static evidence:
  - IDA `sub_101809C` (scene object vtable `+0x74`) stores the pending scene string, pending coordinates, and same-class mode, then uses `sub_100EEBC(scene)` to decide whether to call the full load path (`sub_10037A6`) or the same-current manager callback.
  - `sub_100EEBC()` returns the scene class bit from a local matcher or from a leading `c`; adjacent `01桃花岛_03.sce -> 01桃花岛_04.sce` appears to stay in the same class, so a full class-change load is not expected from this callback alone.
- conclusion:
  - confirmed negative: the remaining portal failure is not caused by malformed `30/2 scene,posinfo` field order; those fields are parsed correctly.
  - hypothesis: same-class map switching depends on the scene runtime table consumed by `sub_1018166()` rather than another business response object after the gate closes.
- added trace-only instrumentation:
  - new log key `trace_same_class_scene_table` at `sub_1018166()` branch sites `0x01018166`, `0x01018190`, `0x010181C2`, `0x010181D6`, `0x010181EA`, `0x010181EE`, `0x01018296`, `0x010182A6`, `0x010182AA`, and `0x0101835E`.
  - each sample records pending scene, pending mode/coordinates, current actor grid, scene class flags, table base/count, and up to six 0x20-byte table entries (`rect`, scene pointer/name, state byte, countdown).
- next evidence check:
  - after a rerun, search for `trace_same_class_scene_table` and verify whether any entry names `01桃花岛_04.sce` and whether its state byte reaches `2`.

Latest same-class table rerun:

- confirmed runtime evidence:
  - `trace_same_class_scene_table` shows the local table already contains the bottom portal before the server response: entry `0` has rect `(160,553)-(240,570)`, scene `01桃花岛_04.sce`, `state=2`.
  - entry `1` similarly points back to `01桃花岛_02.sce` with rect `(20,10)-(60,55)`, `state=2`.
  - while the actor is still above the portal (`grid=204,530/534`), `sub_1018166()` correctly iterates both entries and reaches `no_match`.
  - after the actor reaches `grid=204,554`, the old `builtin-actor-other-portal` fallback returns `30/2` immediately; that response is consumed and closes the business dispatch gate, and later follow-ups hit `early_gate_off`.
- static evidence:
  - IDA `sub_1018166()` sends `send_game_event_type(1)` and sets a 30-tick countdown when a `state=2` table entry matches the current actor coordinates. After the countdown it copies the entry scene name and runs the same-class screen-manager path.
- conclusion:
  - confirmed negative: these adjacent桃花岛 edge portals do not require the mock to invent a `30/2` scene ack on `2/10`; the target scene/rect/state are already present in the local `.sce`-derived table.
  - hypothesis: the observed `2/10 Type=1` at the portal is part of the client's local same-class countdown/notification path. Returning a server-forced `30/2` there interrupts the local transition by closing the dispatch gate early.
- corrected mock:
  - `builtin-actor-other-portal` no longer emits a split `30/2` response for local `.sce` table portals.
  - when such a portal is detected, it logs `mock_actor_other_portal_local_table_passthrough` and falls through to the normal empty `2/10 othernum=0` response.
  - `trace_same_class_scene_table` cap was raised so the next run can capture the match/countdown branch after the actor enters the rect.

Combined continuation packet:

- confirmed runtime evidence:
  - after a movement-upload portal split ack, the client emits a combined request rather than separate objects: `WT len=24 hdr=2/10 objs=1/2/10,1/25/5 count=2`.
  - raw bytes: `5754001801020a000f045479706500030001010119050005`, i.e. `2/10 { Type=1 }` followed by empty `25/5`.
  - this packet is emitted after `parse_scene_posinfo_field()` / `sub_10491AE()` queues the scene-default continuation; treating only standalone short `25/5` as valid caused the mock to assert.
- corrected mock:
  - added an exact matcher for the two-object `2/10 Type=1 + 25/5` continuation.
  - it reuses `vm_net_mock_build_scene_default_event_response()`, so pending actor-other portal completions get the same task/other plus trailing `30/2 scene,posinfo` response as the standalone short `25/5` path.
  - without a pending target it falls back to the existing parser-safe `25/12 result=4` response.

Local-table completion staging:

- confirmed runtime evidence:
  - after the passthrough change, the client no longer asserts and the local same-class table path reaches its intended branches.
  - at `grid=204,554`, `sub_1018166()` matches entry `0` (`01桃花岛_04.sce`, `state=2`) and calls `send_game_event_type(1)`.
  - subsequent ticks reach `countdown_check` / `countdown_copy`; the pending scene string stored in the scene object changes from `01桃花岛_03.sce` to `01桃花岛_04.sce`.
  - after the countdown, the client calls the same-current screen-manager path, but it still does not open `01桃花岛_04.sce`; later periodic `2/10` requests receive empty otherinfo and the live actor remains on the source portal coordinates.
- corrected mock hypothesis:
  - the first `2/10` while `pendingScene` is still the source scene remains passthrough.
  - once the client itself has copied the target scene into the scene object pending scene string, the next local-table `2/10` is treated as the server completion point.
  - that response is the same parser-safe task/other subset plus trailing full `30/2 scene,posinfo` used by the scene-default completion path, and the target save remains pending until `parse_actor_motion_descriptor` confirms the target `.sce` opened.

Pending-scene encoding correction:

- confirmed runtime evidence:
  - the latest run reached the intended local table path: `trace_same_class_scene_table_entry` showed the active entry rect `(160,553)-(240,570)` and target scene `01桃花岛_04.sce`, and the actor remained inside it at `grid=204,554`.
  - after the countdown copy, the trace-visible pending scene was `01桃花岛_04.sce`, but `mock_actor_other_portal_local_table_passthrough` still logged `target=01ÌÒ»¨µº_04.sce` and returned empty `otherinfo`.
- conclusion:
  - confirmed mismatch: the trace label was decoded to UTF-8 for readability, while the mock fallback target scene constants are stored as the original GBK byte strings used by the packet/resource builders.
  - the local-table completion comparison must use the raw guest NUL-terminated bytes; decoded labels are evidence/logging only.
- corrected mock:
  - `vm_net_mock_build_actor_other_portal_response()` now reads both forms from `sceneObj+0x475`: a decoded label for logs and raw bytes for comparison against the target scene.
  - `pendingRawMatch` in the trace records whether the raw pending scene matches the GBK target scene.

Local-table completion split correction:

- confirmed runtime evidence:
  - after the raw-byte fix, the next run logged `mock_actor_other_local_table_completion ... pendingRawMatch=1`, so the mock did recognize that the client had copied `01桃花岛_04.sce` into its pending scene.
  - that response was consumed as six objects ending in `kind=30 subtype=2`; `trace_scene_enter_apply ... scene30_2_obj_apply_0x74 ... scene=01桃花岛_04.sce x=42 y=60` confirms the field order and values were parsed.
  - the client then called the same-current screen-manager path and emitted the short `25/5` continuation, but the mock answered that continuation with only `25/12 result=4`; no `trace_actor_motion_descriptor_context` for `01桃花岛_04.sce` followed.
- conclusion:
  - confirmed negative: a full `30/2 scene,posinfo` inside the local-table `2/10` response is parser-valid but still not the live map-load completion.
  - hypothesis: this path should mirror the actor-other split flow: the `2/10` response acknowledges the target with `30/2 result,type,scene` and no `posinfo`, while the immediately following short `25/5` carries the task/other subset plus final scene-position completion.
- corrected mock:
  - `mock_actor_other_local_table_split_ack` now emits only the split `30/2` ack and remembers the target as an actor-other portal target.
  - the existing `builtin-scene-default-event` actor-other completion path handles the next short `25/5` and saves the target only when that completion response is emitted.

Actor-other position sync inside same-class completion:

- confirmed runtime evidence:
  - the split `2/10` ack and the following short-`25/5` completion both dispatch.
  - the final `30/2 scene,posinfo` parses correctly, and `pendingPos` becomes `(42,60)`.
  - the live current actor node still reports the source grid `(204,554)` after completion, and no `parse_actor_motion_descriptor` for `01桃花岛_04.sce` occurs.
  - `sub_1012958()` reads `2/10 otherinfo` records and calls `scene_node_find_or_create(actorId, gridX, gridY, targetY, targetX, ...)`.
- conclusion:
  - confirmed negative: same-class local-table completion does not rely on a target `.sce` descriptor reload.
  - disproven hypothesis: carrying a compact actor-other record for actor `10001` in this completion is unsafe.
- follow-up crash evidence:
  - after the non-empty `2/10 otherinfo` was consumed, stdout reported `pc=0x10`, `lr=0x01014597`, `lastPc=0x01014594`.
  - IDA `scene_draw_actor_pass()` (`0x01014456`) shows `0x01014592..0x01014594` loads the actor node callback from node offset `+0x148` and executes `BLX R3`; the crash means that callback slot had become `0x10`.
  - therefore the non-empty actor-other completion record either has an incomplete/incorrect record shape for the current actor, or it is the wrong completion window for updating the local player node.
- corrected mock:
  - actor-other portal completions no longer include non-empty `2/10 otherinfo`.
  - when the client-local table already has the same raw pending scene key, the `2/10 Type=1` request is treated as the post-local-table synchronization window and receives a single `30/1 scene+posinfo` final scene object.
  - evidence for this window: `sub_1018166()` first sends `2/10 Type=1` when the portal rect is entered, starts a 30-frame countdown, calls `screen_manager same_current`, and then emits the second `2/10 Type=1` with `pendingRawMatch=1`.

Same-class load criterion correction:

- static evidence:
  - IDA `sub_101809C(scene,len,x,y,a5)` stores `scene` at `sceneObj+0x475`, writes `a5` to `R9+23692`, resets the current scene node, writes pending x/y to `R9+23694/23696`, then checks `sub_100EEBC(scene)`.
  - if the target scene class bit equals the current `R9+23669`, it calls the screen-manager `+8` same-current path and does not call `sub_10037A6()`.
  - only a class-bit mismatch calls screen-manager `+0x18`, then `sub_10037A6(3 or 4)`, and updates `R9+23669`.
  - IDA `sub_1018166()` uses the same class-bit branch after copying a matched local-table entry scene into `sceneObj+0x475`; same-class Taohuadao edge portals therefore naturally reach `same_current`.
- runtime evidence:
  - the latest final `30/1 scene+posinfo` path called `sceneObj+0x74` with target `01桃花岛_04.sce`, `x=42,y=60`, `stack0=0`, `stack4=0x2a`, then immediately reached `screen_manager idx=2 same_current`.
  - the live current actor node remained at the source portal grid `(204,554)` with unchanged draw/step callbacks, so the failure is still real; however, absence of `parse_actor_motion_descriptor_context` for `01桃花岛_04.sce` is no longer a valid negative criterion for same-class map switching.
- added trace-only instrumentation:
  - new log key `trace_same_class_node_callback` records the local-table completion path around `0x0101824E`, `0x01018260`, `0x01018264`, and `0x010182A6`.
  - it captures pending scene/mode/position, `hudState+0x40` current node, `hudState+0x44` reset target, callback argument node, grid/name, and draw/step callback pointers before and after the same-class node callback.
- next evidence check:
  - rerun manually and compare `trace_same_class_node_callback` before/after `same_class_node_callback_before/after`.
  - if the callback leaves current node grid/draw/step unchanged, the missing contract is likely in the local same-class node migration callback input/state rather than in packet field order.
  - if it updates the node briefly and later reverts, inspect the later owner/state-machine path that restores source grid.

Moveinfo fallback race correction:

- confirmed runtime evidence:
  - the newest run reached the local same-class path at `grid=204,554` and emitted the first `2/10 Type=1`; `builtin-actor-other-portal` correctly logged `mock_actor_other_portal_local_table_passthrough`.
  - before the local-table path reached a clean post-countdown completion, a `2/1 moveinfo` at `grid=204,546` matched the same portal via the `8px` margin and returned `mock_actor_moveinfo_portal_split_response`.
  - that moveinfo split ack set the actor-other pending target and the immediate `25/5` completion marked a pending scene save for `01桃花岛_04.sce (42,60)`.
  - later `2/10 Type=1` requests had `pendingRawMatch=1`, but the local-table final response was suppressed by the already-pending scene save guard and fell back to empty otherinfo.
  - `trace_same_class_node_callback` showed the same-class callback did not move the current actor node: current node stayed at `grid=204,554`, with unchanged draw/step callbacks before and after.
- conclusion:
  - confirmed negative: the moveinfo-side portal split ack races the client-local same-class countdown for Taohuadao local-table portals.
  - the latest run did not actually test the intended pure local-table `pendingRawMatch=1 -> 30/1` completion path, because the moveinfo fallback had already installed a pending target/save.
- corrected mock:
  - `2/1 moveinfo` still snapshots coordinates and logs portal-margin hits, but local-table Taohuadao portal hits now pass through to the normal empty subtype-1 moveinfo ack.
  - this leaves the local `sub_1018166()` countdown and the later `2/10 pendingRawMatch=1` completion window unclaimed by moveinfo.
- status:
  - hypothesis pending rerun: with moveinfo no longer racing the local path, the next `2/10` after `pendingRawMatch=1` should emit the intended single `30/1 scene+posinfo` local-table final response.

Latest `01桃花岛_03.sce` hotspot rerun:

- confirmed runtime evidence:
  - the newest run did not enter the local table portal rect. `trace_same_class_scene_table` repeatedly reports current grid `(204,546)`, while entry `0` for `01桃花岛_04.sce` is rect `(160,553)-(240,570)`, so `sub_1018166()` correctly reaches `no_match`.
  - there is no `mock_actor_moveinfo_portal_split_response`, no `pendingRawMatch=1`, and no `mock_actor_other_local_table_final_scene`; the prior moveinfo race fix held.
  - the only later portal-like request is `WT len=60 hdr=4/1 objs=1/4/1(id=4)`, with fields `id=4,index=4,posx=211,posy=444`, handled by the generic `builtin-challenge-interaction` response `4/14 result=3`.
- conclusion:
  - confirmed: this run still did not validate the pure local-table final path because the actor stopped above the portal trigger rectangle.
  - hypothesis: when the actor is near the `01桃花岛_03.sce` bottom portal but not inside the local rect, the client uses a server-decided `4/1` hotspot interaction, analogous to the earlier `01桃花岛_01.sce` bottom hotspot.
- corrected mock hypothesis:
  - `vm_net_mock_build_special_scene_interaction_response()` now also recognizes `01桃花岛_03.sce` request `id=4,index=4,posy>=400`.
  - it returns a narrow `30/1 scene+posinfo` response targeting `01桃花岛_04.sce (42,60)`, using the target-side spawn recovered from the reverse local portal evidence.
  - unlike the older `01桃花岛_01.sce -> 01桃花岛_02.sce` hotspot, this same-class case marks the save pending rather than immediately persisting the target scene, pending another run to confirm the client accepts the transition.

Same-class local-table final confirmation:

- confirmed runtime evidence:
  - with moveinfo no longer racing the local path, the actor entered the `01桃花岛_03.sce` bottom rect and `sub_1018166()` reached `countdown_check` / `countdown_copy`.
  - `trace_same_class_node_callback` fired around the same-current path; before and after the node callback, both `currentNode` and `nodeArg` were `05400000`, with unchanged grid `(204,554)` and unchanged draw/step callbacks.
  - the later `2/10 Type=1` reached `mock_actor_other_local_table_final_scene ... pendingRawMatch=1`, and the response was a single `30/1 scene+posinfo` targeting `01桃花岛_04.sce (42,60)`.
  - the client consumed that response as `kind=30 subtype=1`; `trace_scene_enter_apply ... scene30_1_obj_apply_0x74` confirmed `scene=01桃花岛_04.sce`, `x=42`, `y=60`, then closed the scene dispatch gate.
  - the live current actor node still reported source grid `(204,554)` afterward.
- conclusion:
  - confirmed: the local-table final `30/1` is parser-valid and reaches the scene-object apply path.
  - confirmed negative: neither the local same-current callback nor the later `30/1` apply currently migrates the live actor node to `(42,60)`.
  - because same-class transitions do not necessarily reload the target `.sce`, waiting for `trace_actor_motion_descriptor_context` to confirm pending save is not a valid persistence criterion for this path.
- corrected mock:
  - `mock_actor_other_local_table_final_scene` now saves `01桃花岛_04.sce (42,60)` immediately when emitting the confirmed final `30/1`.
  - this is a persistence correction only; it does not alter guest registers, scene globals, parser control flow, draw callbacks, or the local same-class state machine.

Immediate-save follow-up:

- confirmed runtime evidence:
  - the next manual run still started from `01桃花岛_03.sce`; actorinfo and `trace_actor_motion_descriptor_context` both reported `01桃花岛_03.sce` during startup.
  - this is expected for the first run after the code change, because the prior run only marked a pending save and did not persist `01桃花岛_04.sce`.
  - during the new run, the local-table final path fired again: `mock_actor_other_local_table_final_scene ... pendingRawMatch=1 ... response=30/1 save=immediate`.
  - the immediate-save trace confirms the target was written: `mock_player_pos_save reason=actor-other-local-table-final scene=01桃花岛_04.sce pos=42,60 path=nvram/jhol_mock_player_pos.bin`.
  - the same response again parsed as `scene30_1_obj_apply_0x74 ... scene=01桃花岛_04.sce x=42 y=60`.
- status:
  - confirmed: the persistence correction now writes the target scene/position when the final local-table `30/1` is emitted.
  - still confirmed negative: in the same live session, current actor node remains on the source-side grid after the `30/1` apply.
  - next rerun should verify whether startup now loads the persisted `01桃花岛_04.sce (42,60)` entry.

Same-class persistence verified on next startup:

- confirmed runtime evidence:
  - the next manual run starts from the persisted target. `trace_scene_actorinfo_snapshot` reports `scene=01桃花岛_04.sce`, confirming that the immediate save from the previous `03 -> 04` final `30/1` affected startup.
  - the live actor node is also on the target side, with later motion traces showing `grid=42,56`, then `grid=42,36`.
  - movement uploads on `01桃花岛_04.sce` update the persisted position normally, e.g. `mock_player_pos_save reason=moveinfo-upload scene=01桃花岛_04.sce pos=42,68`.
  - after moving into the north-side local portal, the client reaches the same local-table final path in reverse: `mock_actor_other_local_table_final_scene ... portal=taohuadao-04-north-to-03 ... target=01桃花岛_03.sce targetPos=200,540 response=30/1 save=immediate`.
  - the reverse final response parses as `scene30_1_obj_apply_0x74 ... scene=01桃花岛_03.sce x=200 y=540`.
- conclusion:
  - confirmed: same-class local-table transition persistence works across startup for `01桃花岛_03.sce -> 01桃花岛_04.sce`.
  - confirmed: the reverse `01桃花岛_04.sce -> 01桃花岛_03.sce` local-table path uses the same `pendingRawMatch=1 -> 30/1 scene+posinfo` completion shape and immediate-save policy.
  - remaining live-session behavior: after a same-session reverse `30/1`, the actor still remains visually/live-positioned on the source-side 04 grid until the next startup or a separate live-node migration contract is recovered.

Reverse persistence verified:

- confirmed runtime evidence:
  - the newest manual run begins from the prior `04` save: `trace_scene_actorinfo_snapshot` reports `scene=01桃花岛_04.sce`.
  - the actor moves on the `04` side, with motion traces around `grid=42,56`, `42,36`, and moveinfo persistence `scene=01桃花岛_04.sce pos=42,68`.
  - the north-side local-table portal then fires in reverse: `mock_actor_other_local_table_final_scene ... portal=taohuadao-04-north-to-03 ... target=01桃花岛_03.sce targetPos=200,540 response=30/1 save=immediate`.
  - the reverse response is consumed as `kind=30 subtype=1`, and `trace_scene_enter_apply ... scene30_1_obj_apply_0x74` confirms `scene=01桃花岛_03.sce`, `x=200`, `y=540`.
  - later same-class table traces show `currentNode` at `grid=200,540`, `pendingScene=01桃花岛_03.sce`, and no match against the `03 -> 04` bottom rect because `y=540` is below the trigger's `553..570` range.
- conclusion:
  - confirmed: the `04 -> 03` reverse immediate-save path also takes effect within the run, placing the actor on the expected `03` target-side coordinates.
  - the bidirectional `01桃花岛_03.sce <-> 01桃花岛_04.sce` same-class local-table completion contract is now confirmed at packet parse and persistence levels.

Same-current screen-manager contract:

- static/runtime evidence:
  - IDA `sub_101809C(scene,len,x,y,a5)` writes the pending scene and pending coordinates, then calls `sub_100EEBC(scene)` to classify the target scene.
  - when the target scene class matches the current class, `sub_101809C()` does not call the full load path `sub_10037A6()`. Instead it calls the platform screen manager slot `+8` with the current scene screen pointer.
  - runtime logs show exactly that shape after local-table finals: `screen_manager idx=2 same_current ... last=010182a6` for the local table same-current callback and `last=01018150` for the scene-object apply path.
  - the emulator's screen-manager shim previously treated `idx=2` where `r0 == vmAddedScreen` as a no-op, so the client reached the intended platform API but the emulator swallowed the lifecycle notification.
- corrected platform contract:
  - `hook_vm_manager_screen_func()` now treats `idx=2` calls from the confirmed local-table same-current callback site (`last=0x010182A6`) as a real re-change of the current screen.
  - it sets `VM_SCREEN_nextSubTScreen_ADDRESS`, clears `VM_SCREEN_isInQuit`, and raises `screenStructChange` so the scheduler re-enters the existing screen lifecycle instead of ignoring the same-current request.
  - this does not alter guest parser results, draw callbacks, actor nodes, scene globals, or packet contents; it restores an emulator platform behavior that the client was already requesting.
- status:
  - hypothesis pending rerun: after a same-class local-table final, the new `screen_manager idx=2 same_current_rechange` should let the screen re-enter its scene setup path and either migrate the live actor node to the pending target map/position or expose the next missing platform callback.

Follow-up correction after the first successful loading-screen rerun:

- confirmed runtime evidence:
  - the `0x010182A6` same-current rechange now enters the loading flow and opens the target descriptor: `trace_actor_motion_descriptor_context ... name=01桃花岛_04.sce`.
  - target position is `01桃花岛_04.sce (42,60)`.
  - that spawn is not inside the reverse north portal rect `(20,5)-(64,35)`, and the reverse target spawn `01桃花岛_03.sce (200,540)` is not inside the forward bottom portal rect `(160,553)-(240,570)`.
  - the observed load loop instead correlates with repeated `builtin-scene-change` / `builtin-scene-task-subset-followup` responses for the same target and repeated screen-manager calls from `last=0x01018150` after `scene30_2_obj_apply_0x74`.
- corrected contract:
  - keep the platform rechange only for `last=0x010182A6`.
  - treat `last=0x01018150` as a scene-object apply follow-up site, not as a standalone same-class lifecycle request.
  - explicit `mapID/exitID` scene-change responses now remember the last completed target and suppress re-registering another deferred completion for the same scene/exit/position within a short tick window.
- status:
  - confirmed negative: the new repeated loading is not explained by target spawn overlapping a portal trigger rectangle.
  - hypothesis pending rerun: cutting the repeated deferred completion and `0x01018150` rechange should keep the first target load while preventing the immediate second loading loop.

Battle type-1 `valueB` remaining-HP experiment:

- latest runtime evidence:
  - `bin/logs/net_packets.log` tick `378`: first live `4/2 index=0 Operate=0` receives `1/4/6 actionnum=1`, `actioninfoLen=47`, and the type-1 record contains damage text `12`.
  - `bin/logs/net_packets.log` tick `410`: second live `4/2` receives `1/4/6 actionnum=1`, `actioninfoLen=46`, and damage text `8`.
  - `bin/logs/net_packets.log` tick `437`: client still emits a third `4/2`; the mock then returns the terminal `4/7+4/8+4/11+4/9` chain.
  - `bin/logs/net_trace.log` around ticks `436..437` keeps `enemy0=...,00000014,00000014`; the local Battle.cbm fighter table has not been decremented even though mock-side enemy HP is `0/20`.
  - `bin/logs/net_trace.log` tick `438` reaches subtype-9 result handling (`0x05189BF0..0x05189EA4`) with `mockHp=120/120,0/20`, still paired with unchanged `enemy0=...,00000014,00000014`.
- static/runtime context:
  - `HandleBattleActionMsg()` / `sub_6EB0` (`Battle.cbm 0x05188DD0`) materializes type-1 records and exposes both child dwords as `valueA` and `valueB` at the traced `0x05188F96..0x05188FBC` window.
  - current no-crash type-1 record already proves `valueA` drives the visible damage text, but `valueB=0` leaves local HP/death state unchanged.
- conclusion:
  - confirmed negative: terminal victory objects are too late/wrong if the lethal `4/6` did not first settle Battle.cbm's local fighter HP and death animation state.
  - hypothesis: for the stable type-1 actioninfo record, `valueB` should carry post-hit remaining HP rather than a placeholder zero.
- current mock contract:
  - type-1 `valueB` now defaults to remaining HP after mock-side settlement (`CBE_BATTLE_TYPE1_VALUEB_REMAINING_HP=1`).
  - expected next wire shape for default HP `20` and damage `12/8`:
    - first hit: `valueA=12,valueB=8`
    - lethal hit: `valueA=8,valueB=0`
  - overrides are available for controlled reruns: `CBE_BATTLE_TYPE1_VALUEB_REMAINING_HP=0`, `CBE_BATTLE_FIRST_VALUE_B=<n>`, `CBE_BATTLE_COUNTER_VALUE_B=<n>`.
  - `iteminfo/teaminfo` remain omitted until their real record grammar is recovered; prior evidence showed raw empty, tagged-empty, and empty WT blob fields are all unsafe for `InitActionSlot_B()` (`Battle.cbm off 0x6DBC`).

Battle actioninfo still not committing HP locally:

- confirmed runtime evidence from the latest manual battle:
  - `bin/logs/net_packets.log` ticks `177`, `204`, `230`: first `4/2` and lethal second `4/2` receive subtype `4/6`; the third post-lethal `4/2` receives terminal `4/7+4/8+4/11+4/9`.
  - `bin/logs/net_trace.log` `mock_battle_operate_response` shows mock-side HP changed `20 -> 8 -> 0`.
  - `trace_battle_actioninfo_materialize_detail` at `Battle.cbm 0x05188F3A` confirms the two type-1 records materialized as `valueA/valueB=12/8` and `8/0`.
  - `trace_battle_local_state_write` and terminal `sub_4B70_battle_apply_entry` still report canonical `enemyHp=20/20` and `pendingDelta=0,0`.
- static evidence:
  - `HandleBattleActionMsg()` (`Battle.cbm 0x05188DD0`, IDA `0x6EB0`) parses subtype `4/6` action records and stores type-1 child values at record offsets `+0x18/+0x1C`, then uses `+0x5C` to select/copy an effect template before invoking the callback through battle state `+0x24`.
  - `sub_4B38()` (`Battle.cbm 0x05184B38`, IDA `0x4B38`) only merges HP into the active fighter block when local byte `+0x0D == 4`; it adds signed pending deltas from `R9+0x34B4/+0x34B6` into active block fields `+0x530/+0x538`, then clears the pending deltas.
- conclusion:
  - confirmed negative: type-1 `valueB=remaining HP` is parser-valid but not sufficient to drive the client's local HP/death state.
  - current hypothesis: the missing contract is upstream of terminal objects, either an action record/effect-template field that lets `DrawBattleAnimEffect()` generate pending deltas, or a companion `iteminfo/teaminfo` object whose recovered grammar has not yet been proven.
- trace-only follow-up:
  - added `trace_battle_anim_effect_delta_detail` at `DrawBattleAnimEffect` windows (`Battle.cbm IDA 0x31FA, 0x3448, 0x3492, 0x34BC, 0x34E4`), `sub_4582()` (`0x4582`), and `sub_4B38()` state-4 delta consumer windows (`0x4B4C, 0x4B50, 0x4B6C, 0x4B72, 0x4B90`).
  - the trace is read-only and records local action state, active action values, pending delta, canonical fighter HP, and selected `DrawBattleAnimEffect` scratch fields.
  - first rerun limitation:
    - confirmed runtime evidence: the trace budget was consumed entirely by `damage_number_effect_entry` (`sub_4582`, Battle.cbm IDA `0x4582`), producing `160` samples and no `anim_*` / `state4_*` labels.
    - confirmed runtime evidence: sampled damage-number entries all had `pendingDelta=0,0` and canonical `enemyHp=20/20`, while the mock-side HP had already reached `0/20`.
    - conclusion: this proves the current visible-number path is HP-inert, but it does not yet prove that the `DrawBattleAnimEffect` entry windows or `sub_4B38` state-4 merge windows are unreachable.
  - corrected trace-only instrumentation:
    - `damage_number_effect_entry` now has a small independent sample cap.
    - `DrawBattleAnimEffect` windows and `sub_4B38` state-4 HP-merge windows have separate budgets, so the next run should capture whether local byte `+0x0D` ever reaches `4` and whether `R9+0x34B4/+0x34B6` ever become nonzero.

Battle actioninfo effect-index/tail experiment boundary:

- latest runtime evidence:
  - the refined trace pass shows the action/effect state machine does reach `state4_store_target` and `state4_target_copy_store`, but the local record byte stays `record+0x0D == 1`.
  - `trace_battle_state4_detail` reports `state4cc=0`, `callback52c=2`, and no HP merge; terminal `sub_4B70_battle_apply_entry` still reports `pendingDelta=0,0` and canonical enemy HP `20/20`.
  - `trace_battle_actioninfo_materialize_detail` continues to confirm type-1 records with `valueA/valueB=12/8` and `8/0`.
- packet field boundary:
  - `HandleBattleActionMsg()` reads, for type-1 records, an effect/template index into local offset `+0x5C`, a string/blob scratch into `+0x04..+0x0F`, and three tail bytes into `+0x60..+0x62`.
  - current baseline has used `effectIndex=0` and tail bytes `0,0,0`; these are parser-safe but HP-inert.
- mock support:
  - `src/mock-server.c` now parameterizes this narrow field family with `CBE_BATTLE_TYPE1_EFFECT_INDEX` and `CBE_BATTLE_TYPE1_TAIL0/1/2`.
  - defaults preserve the confirmed baseline packet exactly.
  - success criterion for a non-default rerun is `record+0x0D == 4`, nonzero `R9+0x34B4/+0x34B6`, or a Battle.cbm fighter HP table change before terminal settlement.
  - failure criterion is clean materialization with `record+0x0D == 1`, pending deltas `0,0`, and enemy HP still `20/20`; in that case effect index/tail alone should be recorded as rejected for `G6/G7`.
