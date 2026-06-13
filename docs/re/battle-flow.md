# Battle Flow: Client Requests and Server Responses

Status tags: `confirmed` (verified by code), `hypothesis` (inferred, needs confirmation)

## Overview

Battle in Jianghu OL follows a request-response pattern over WT packets. The client drives
the flow: it sends action requests, the server validates and returns results, and the client
updates local state and UI accordingly.

Key address references use the main binary `江湖OL.CBE` (ARM-32, little-endian).

---

## Phase 1: Battle Trigger (NPC/Monster Interaction)

### Entry Point

Function: `HandleSceneNPCInteraction` @ `0x1027D3C`

When the player presses key `0x4000` (confirm/interact) while adjacent to an NPC or monster,
the scene interaction handler is invoked. If the target is a hostile NPC (monster), the flow
proceeds to battle entry.

### Battle Entry Validation

Function: `ValidateBattleEnter` @ `0x1015DEC`

Before entering battle, the client checks:
1. Whether the player is already in battle (vtable[102] returns 1)
2. Whether the player is in a state that prevents battle (vtable[114]==2 and target mismatch)
3. Whether the player has a valid battle target (vtable[101] returns non-null)

If validation passes, the client shows a battle confirmation dialog.

### Battle Confirmation Dialog

Function: `HandleBattleConfirm` @ `0x101AE42`

When the player confirms the battle dialog:
- `n2==1` (主动攻击): Sets `battleMode = 2`, calls `HandleBattleEnterReq(0)`
- `n2==2` (逃跑): Sends event `(5, 1, 2, 12)` — escape request

Key code path:
```c
if (n2 == 1) {
    *(byte*)(gameState + 1399) = 2;  // battleMode = 2 (主动攻击)
    HandleBattleEnterReq(0);
    CloseActiveDialog();
} else if (n2 == 2) {
    CloseActiveDialog();
    alloc_outgoing_game_event(5, 1, 2, 12);  // 逃跑请求
}
```

---

## Phase 1B: Monster Battle Flow (Wild Encounter)

NPC挑战和怪物遭遇是两种不同的战斗触发方式。NPC挑战需要玩家主动交互（按确认键），
而怪物遭遇是玩家在场景中移动时自动触发的碰撞检测。

### Monster Encounter Trigger

Function: `SceneTickUpdatePositions` @ `0x10163A4`

当玩家在场景中移动（按方向键）时，场景tick函数检测玩家与actor的碰撞：

1. 玩家按方向键（0x20=右, 0x40=左, 0x80=下, 0x100=上）
2. 调用碰撞检测函数 `vtable[0x58](sceneData, 0xE, 0x327)`
3. 如果碰撞检测返回actor类型4（本地/怪物），进入怪物战斗流程

**Actor类型分类**（来自 `StartSceneTransition` @ `0x101621C`）：

| 类型值 | 标签 | 含义 |
|--------|------|------|
| 0 | (世界) | 传送门/出口 |
| 2 | (队伍) | 队友 |
| 3 | (帮派) | 帮派成员 |
| 4 | (本地) | 场景怪物/NPC |
| 7+ | (私人) | 私人/其他 |

### Monster Battle Entry Path

在 `SceneTickUpdatePositions` 中，当key=0x20（方向键右/确认）且碰撞到actor类型4时：

```c
// 0x101687A - 0x10168B4
if (vtable_check(sceneData, 0xE, 0x327) != 0) {
    // 条件满足：直接进入场景过渡
    StartSceneTransition(0, 1, actorType, 0);  // 直接进入战斗
} else {
    // 条件不满足：显示提示（如"没有小喇叭请到商城购买"）
    ShowConfirmOrMessage(loc_1016BF0, HandleBattleConfirm0, HandleSceneTouchRegion, ...);
}
```

**关键区别**：
- 怪物战斗：通过 `StartSceneTransition` 直接进入，无需确认对话框
- NPC挑战：通过 `HandleBattleConfirm1` → `HandleBattleEnterReq` 进入，需要确认对话框

### StartSceneTransition

Function: `StartSceneTransition` @ `0x101621C`

```c
StartSceneTransition(int transitionType, char direction, int targetId, int extraParam)
```

- `transitionType=4`（本地/怪物）：格式化"(本地)"文本，调度场景过渡
- 如果 `gameState[1278]==4`（已在本地场景），显示消息并返回
- 设置 `gameState[1134]=targetId`（目标ID）
- 发送 `send_game_event_type(1)` 通知场景切换
- 调度 `DispatchSceneTransition` 执行实际过渡

### DispatchSceneTransition

Function: `DispatchSceneTransition` @ `0x1015FAC`

根据 `transitionType` 分发场景过渡请求：

| transitionType | 处理 | 网络请求 |
|----------------|------|----------|
| 0 | 世界过渡 | `vtable[24720]()` + `vtable[24748]()` |
| 2 | 队伍过渡 | `vtable[22456](22420, 2, playerData[100], 24040)` |
| 3 | 帮派过渡 | `vtable[22336](22300, 3, playerData[100], 24040)` |
| 4 | 本地/怪物 | `vtable[22276](22240, 4, playerData[100], 24040)` |
| 7+ | 私人过渡 | `vtable[22396](22360, 7, playerData[100], 24040)` |

对于怪物战斗（type=4），发送场景进入请求后等待服务端响应。

### Monster Battle Enter Response

Handler: `net_handle_actor_move_info` case 9 @ `0x1012C80`

**WT Response Packet:**
- Object: `cmd=5, subcmd=9`
- Field: `"result"` — 进入战斗结果

| result | 含义 | 客户端处理 |
|--------|------|------------|
| 0/1 | 战斗成功 | 设置`gameState[0x164]=0`, `battleState=4`, 切换到战斗画面 |
| 2 | 无法进入战斗 | 显示"你不是队长!"消息 |
| 3 | 需要确认 | 显示确认对话框（`HandleBattleConfirm0` + `HandleSceneTouchRegion`） |

**result=0/1时的处理流程**：
```c
// 0x1012CFE - 0x1012D1C
gameState[0x164] = 0;           // 清除战斗等待标志
battleState = 4;                // 设置战斗状态=4
parserState = 4;                // 解析器状态=4
sceneSwitchPending = 1;         // 标记场景切换待处理
// 读取posinfo字段更新玩家位置
// 根据actorSceneVariant切换到战斗画面
```

**result=3时的确认对话框**：
```c
ShowConfirmOrMessage(
    byte_10116DC,           // "您的苦宝不足!请进入商城充值苦宝"
    HandleBattleConfirm0,   // 确认回调（怪物碰撞触发，n2=0）
    HandleSceneTouchRegion, // 触摸区域回调
    0, 0, 0, R5
);
```

### HandleBattleConfirm0 vs HandleBattleConfirm1

| 函数 | 调用者 | n2参数 | battleMode设置 | 触发方式 |
|------|--------|--------|----------------|----------|
| `HandleBattleConfirm0` @ `0x101AE7C` | 怪物碰撞 | 0 | 不设置 | 场景移动碰撞自动触发 |
| `HandleBattleConfirm1` @ `0x101AE82` | NPC挑战 | 1 | `battleMode=2` | 玩家主动交互触发 |

两者确认后都调用 `HandleBattleEnterReq(0)` 发送战斗进入请求。

### SelectBattleTarget (Monster Target Selection)

Function: `SelectBattleTarget` @ `0x1016C88`

在战斗初始化时，客户端选择怪物目标：

```c
// 遍历actor列表（最多25个）
for (i = 0; i < 25; i++) {
    actor = 340 * i + actorListBase;
    if (actor[315] == 2 && actor[327] != 1) {  // type=2(怪物) 且 未死亡
        break;  // 找到有效怪物目标
    }
}
session[25347] = result;     // 保存选择结果
if (result == 1) {
    session[23682] = 1;      // battleState = 1 (玩家回合)
}
vtable[364](5);              // 通知战斗系统目标已选择
```

**Actor结构关键字段**：

| 偏移 | 大小 | 含义 |
|------|------|------|
| +282 | byte | actor类型（2=怪物） |
| +315 | byte | 战斗类型（2=怪物目标） |
| +327 | byte | 存活状态（0=存活, 1=死亡） |
| +242 | word | 序列号 |
| +276 | word | 场景内序列号 |

---

## Phase 2: Battle Enter Request

### Client Request: Battle Enter

Function: `HandleBattleEnterReq` @ `0x1015E14`

The client sends a battle enter request packet:

**WT Packet:**
- Object: `cmd=2, subcmd=1`
- Fields: `type=2, param=10`
- TLV field: `"Type" = 2`

```c
v5 = alloc_outgoing_game_event(2, 1, 2, 10);
if (v5) {
    v5[15](v5, "Type", 2);  // set Type field = 2
}
*(word*)(session + 23682) = 3;  // battleState = 3 (等待响应)
if (a1 == 1) {
    *(byte*)(gameState + 332) = 1;  // 主动攻击标记
}
```

**Packet format (WT):**
```
WT <len> <obj_count>
  obj: major=2 kind=1 subtype=0
    field "Type" = 2 (u8)
```

### Server Response: Battle Enter Result

Handler: `HandleBattleResultEvent` @ `0x103DABC`

Registered as the net dispatch handler in `InitScenePageTable` @ `0x1042F34`
(page 0, slot 4 = net dispatch function).

**WT Response Packet:**
- Object: `cmd=10, subcmd=22`
- Fields:
  - `"result"` = 1 (胜利/进入战斗) or 0 (失败/拒绝)

**Client processing:**
```c
if (packet.cmd == 10 && packet.subcmd == 22) {
    byte_battlePending = 0;
    dword_battleTimer = 0;
    battleState[0] = 0;  // reset battle state
    if (read_field("result") == 1) {
        scene_reset_status_snapshot_nodes(1);
        ShowMessage("进入战斗");  // unk_103DCE8
        StartBattleTurn();       // 开始战斗回合
    } else {
        ShowBattleLostMsg();     // unk_103DCF8, 战斗失败/拒绝
    }
}
```

### StartBattleTurn

Function: `StartBattleTurn` @ `0x1017DA0`

```c
*(word*)(session + 23682) = 1;  // battleState = 1 (玩家回合)
RefreshSceneUI();                // 刷新场景UI
vtable[82]();                    // 进入战斗场景
```

---

## Phase 3: Battle Actions (Per Turn)

### Action Selection

Function: `SelectBattleAction` @ `0x101C3BE`

When the player selects a battle action (attack/skill/item/defend):
```c
vtable[113](2);              // 设置战斗速度
gameState[2337] = actionType; // 保存选择的动作类型
gameState[2404] = 3;         // battlePhase = 3
gameState[2405] = 0;
FormatCurrentSkillName();     // 格式化技能名称
MergeInventorySlots(24,25,26);// 合并物品栏
BuildBuffDisplayList();       // 构建buff列表
vtable[82]();                 // 刷新UI
```

### Action Dispatch

Function: `HandleBattleActionDispatch` @ `0x101CDC6`

This is the central battle action dispatcher, handling key inputs during battle:

| Key | Action | Handler |
|-----|--------|---------|
| `0x2000` (确认) + a2==1 | 执行当前动作 | Depends on battlePhase switch |
| `0x1000`/`0x4000` (方向+确认) | 选择目标/确认 | Target selection / action confirm |
| `0x8000`/`0x10` (上下) | 翻页 | `BattleAction_ShowConfirmDialog` |
| `0x10000`/`0x40` (左右) | 翻页(反向) | `BattleAction_ShowConfirmDialog` |
| `0x20000`/`0x4` | 技能/物品选择 | vtable[11] callback |
| `0x40000` | 确认选择 | vtable[11] callback |
| `0x100` (取消) | 返回 | (no-op) |

#### Battle Phase Switch (key 0x1000/0x4000, a2==1)

When confirming an action in battle, the behavior depends on `battlePhase`:

| Phase | Action | Network Request |
|-------|--------|-----------------|
| 0 (攻击) | 普通攻击/技能攻击 | SendSerialPacket (see below) |
| 1 (技能) | 使用技能 | `BattleAction_SelectSkill` |
| 2 (物品) | 使用物品 | `BattleAction_UseItem` or `BattleAction_SendAttack` |
| 3 (防御) | 防御 | `BattleAction_Defend` |
| 4 (逃跑) | 逃跑 | `BattleAction_Cancel` → `BattleAction_Defend` |
| 5 (自动) | 自动战斗 | `BattleAction_Cancel` |

### Attack Request (Phase 0)

In phase 0, the client validates the target and sends an attack:

1. Get current target via `vtable[24716]()` (获取当前选中目标)
2. Validate target: check `targetType`, `targetSeq`, `targetId` match
3. If target is valid and not "same faction" type (7-9 range check):
   - If has active skill: `SendSerialPacket(ctx, 9, skillSeq, targetSeq)` — 技能攻击
   - If no active skill: `SendSerialPacket(ctx, 8, 3, targetSeq)` — 普通攻击
4. Set `gameState[1468] = 1` (等待响应标记)

**Network packet for normal attack:**
```
SendSerialPacket(battleContext, actionType=8, subType=3, targetSeq)
```

**Network packet for skill attack:**
```
SendSerialPacket(battleContext, actionType=9, skillSeq, targetSeq)
```

### Skill Selection Request

Function: `BattleAction_SelectSkill` @ `0x101CD1E`

**WT Packet:**
- Object: `cmd=4, subcmd=1, type=29, param=a2`
- TLV field: name from `loc_101D030`, value = skillSeq

```c
result = alloc_outgoing_game_event(4, 1, 29, a2);
if (result) {
    result[14](result, &skillName, skillSeq);  // set skill seq field
    gameState[1468] = 1;  // 等待响应
}
```

### Item Use Request (Battle)

Function: `BattleAction_UseItem` @ `0x101CA42`

**WT Packet:**
- Object: `cmd=3, subcmd=1, type=7, param=29`
- TLV fields: `"type" = 2`, `"seq" = currentSeq`, `"id" = currentId`

```c
result = alloc_outgoing_game_event(3, 1, 7, 29);
if (result) {
    result[15](result, "type", 2);     // type = 2 (使用物品)
    result[14](result, "seq", currentSeq);  // 物品序列号
    result[13](result, "id", currentId);     // 物品ID
    gameState[1468] = 1;  // 等待响应
}
```

### Defend Action

Function: `BattleAction_Defend` @ `0x101C99A`

Defend does NOT send a network request directly. Instead it:
1. Gets current target
2. Validates target
3. Calls `BattleAction_Cancel()` to reset battle UI
4. Calls `InitBattleTargetInfo()` to set up target info for the defend action

### Escape (Flee) Request

Two paths for escape:

**From battle confirmation dialog:**
```c
alloc_outgoing_game_event(5, 1, 2, 12);  // cmd=5, subcmd=1, type=2, param=12
```

**From battle action (BattleAction_UseItem with type=2):**
```c
alloc_outgoing_game_event(3, 1, 7, 29);
// with fields: type=2, seq=currentSeq, id=currentId
```

### Battle Action State Machine

Function: `HandleBattleActionState` @ `0x100E342`

During battle, the client manages action state:

| State | n2 | Action | Network Request |
|-------|----|--------|-----------------|
| Waiting | 1 | Send action | `alloc_outgoing_game_event(2, 1, 25, 11)` |
| Waiting | 2 | Send chat | `alloc_outgoing_game_event(3, 1, 22, 5)` |

The `cmd=2, subcmd=1, type=25, param=11` request is the **battle operate request** that
matches the `4/2` packet documented in `protocol.md` (the WT framing maps internal
cmd/subcmd to the wire format differently).

---

## Phase 4: Battle Action Response

### Response Handler (Main Scene)

Handler: `HandleBattleResultEvent` @ `0x103DABC`

**WT Response Packet:**
- Object: `cmd=10, subcmd=22`
- Field: `"result"` = 1 (success) or 0 (failure)

When `result == 1`: client calls `StartBattleTurn()` to begin the next turn.
When `result == 0`: client shows battle lost message.

### Response Handler (Faction Scene)

Handler: `HandleBattleActionResult` @ `0x1042AAE`

Dispatched from `DispatchFactionNetEvents` @ `0x1042CB2` when `cmd=10, subcmd=17`.

**WT Response Packet:**
- Object: `cmd=10, subcmd=17`
- Field: `"result"` = value

| result | Meaning | Client Action |
|--------|---------|---------------|
| 1 | 战斗胜利 | Show victory message, `InitBattleActorSlot` |
| 2 | 战斗失败 | Show defeat message, `AllocRoleListEntries`, `SetRoleDirection` |
| 3 | 平局 | Show draw message |
| 4 | 逃跑成功 | Show escape message |
| 5 | 逃跑失败 | Show escape fail message |
| default | 未知 | `InitFactionSceneVTable` (reset) |

### Response Handler (Battle Menu)

Handler: `HandleBattleMenuResult` @ `0x1042E3E`

Dispatched from `HandleFactionBattleResult` @ `0x1042F16` when `cmd=10, subcmd=40`.

**WT Response Packet:**
- Object: `cmd=10, subcmd=40`
- Field: `"result"` = value

| result | Meaning | Client Action |
|--------|---------|---------------|
| 1 | 胜利 | Show victory, calculate exp gain, `SendGuildPageReq` |
| 2 | 失败 | Show defeat, `AllocRoleListEntries(0,0)`, `SetRoleDirection` |
| 3 | 平局 | Show draw message |
| 4 | 逃跑 | Show escape message |
| other | 未知 | Show generic message |

After processing, `battleState[3] = 0` (reset battle state).

### Skill Response Handler

Handler: `HandleBattleSkillResponse` @ `0x1011ACA`

**WT Response Packet:**
- Object: `cmd=10, subcmd=??` (routed through business dispatch)
- Fields: `"flag"`, `"type"`, `"result"`

| type | flag | result | Client Action |
|------|------|--------|---------------|
| 2 | 1 | 1 | Skill learned success, refresh skill list |
| 2 | 2 | 1 | Skill used success, update HP, cancel battle action |
| 2 | 1 | 2 | Skill learn failed, show dialog |
| 2 | 2 | 2 | Skill use failed, show dialog, reset battle state |

### Experience Battle Response

Handler: `HandleExpBattleResponse` @ `0x102CB46`

**WT Response Packet:**
- Object: `cmd=7, subcmd=18|19|21`

| subcmd | Fields | Meaning |
|--------|--------|---------|
| 18 | `todaypasthour`, `todaypastmin`, `getexp`, `todaylasthour`, `todaylastmin`, `alllasthour`, `alllastmin`, `isgold` | 经验战斗时间信息 |
| 19 | `helpinfo` (blob) | 帮助信息 |
| 21 | `result` | 经验战斗结果 |

### Process Exp Battle Result

Handler: `ProcessExpBattleResult` @ `0x1040A2C`

**WT Response Packet:**
- Object: `cmd=10, subcmd=??`
- Fields: `"result"`, `"lastexp"`, `"curexp"`

When `result == 1`:
```c
battleState[2] = 2;  // 标记战斗结束
AllocRoleListEntries(gameState + 104, 340);  // 分配角色列表
vtable[271](packet, 1);  // 处理角色数据
gameState[124] = read_field("lastexp");  // 上次经验
gameState[128] = read_field("curexp");   // 当前经验
if (gameState[104]) {
    InitSceneViewport(1, gameState[104], gameState[104]);  // 初始化场景视口
}
```

---

## Phase 5: Battle End and Cleanup

### Battle End Flow

When the server sends a battle result with `result` indicating victory/defeat:

1. **Victory (result=1):**
   - Client shows victory message
   - Processes experience gain (`ProcessExpBattleResult`)
   - Updates player stats (`lastexp`, `curexp`)
   - Returns to scene view (`InitSceneViewport`)

2. **Defeat (result=2):**
   - Client shows defeat message
   - Frees role list entries (`AllocRoleListEntries(0, 0)`)
   - Resets role direction (`SetRoleDirection`)
   - Returns to scene

3. **Escape (result=4):**
   - Client shows escape message
   - Resets battle state
   - Returns to scene

### Battle State Reset

Function: `ResetBattleState` @ `0x101D1E2`

Clears all battle-related state:
```c
gameState[1468] = 0;     // battlePending
gameState[1469] = 0;     // battlePending+1
gameState[1470] = 0;     // battlePending+2
mem_zero(gameState[1397], 6);   // battle sub-state
mem_zero(gameState[1416], 0x34); // battle data
mem_zero(gameState[1492], 0x144); // battle target data
mem_zero(gameState[1816], 0x144); // battle target data 2
gameState[1408] = 0;
gameState[1404] = 0;
```

### BattleAction_Cancel

Function: `BattleAction_Cancel` @ `0x101C85A`

Resets battle UI and prepares for next turn:
```c
vtable[17](1);           // reset something
RefreshSceneUI();        // 刷新场景UI
vtable[268](gameState[1460]);  // 释放资源1
vtable[268](gameState[1464]);  // 释放资源2
battleTimer = *(char*)(gameState + 1400);  // 保存战斗计时器
battleTurn = *(char*)(gameState + 1401);   // 保存当前回合
battlePhase = *(char*)(gameState + 1402);  // 保存战斗阶段
gameState[1399] = 0;     // 清除战斗模式
battleTargetSelected = 0;
if (battleSkillData) {
    battleSkillData[10]();  // 调用技能清理回调
}
vtable[268](gameState[2204]);  // 释放额外资源
FreeEquipViewSlots();     // 释放装备视图
RefreshBattleUI();        // 刷新战斗UI
vtable[113](100);         // 设置战斗速度=100
```

---

## Complete Packet Sequence Summary

### Monster Battle Flow (Wild Encounter)

与NPC挑战不同，怪物战斗是场景移动碰撞自动触发的。

```
Client                              Server
  |                                    |
  | 1. SceneTickUpdatePositions        |
  |    (玩家按方向键移动)               |
  |                                    |
  | 2. 碰撞检测: vtable[0x58]          |
  |    检测到actor类型4(本地/怪物)       |
  |                                    |
  | 3. StartSceneTransition(0,1,4,0)   |
  |    transitionType=4(本地)           |
  |    send_game_event_type(1)         |
  |                                    |
  | 4. DispatchSceneTransition         |
  | --- 场景进入请求(type=4) --------->|
  |                                    |
  |<-- WT cmd=5 subcmd=9 -------------|
  |    result=0/1 (战斗成功)            |
  |    或 result=2 (不是队长)           |
  |    或 result=3 (需要确认/付费)      |
  |                                    |
  | 5. battleState=4, sceneSwitch=1    |
  |    切换到战斗画面                   |
  |                                    |
  | 6. SelectBattleTarget              |
  |    查找actor[315]==2的怪物目标      |
  |    battleState=1 (玩家回合)         |
  |                                    |
  | 7. HandleBattleEnterReq(0)         |
  | --- WT cmd=2 subcmd=1 ----------->|
  |    Type=2                          |
  |    (注意: isAttacker=0, 被动遭遇)   |
  |                                    |
  |<-- WT cmd=10 subcmd=22 -----------|
  |    result=1 (进入战斗)              |
  |                                    |
  | 8. StartBattleTurn()               |
  |    battleState=1 (玩家回合)         |
  |                                    |
  |    ... 战斗回合循环(同NPC战斗) ...   |
  |                                    |
  |<-- WT cmd=10 subcmd=17 -----------|
  |    result=1 (胜利) 或 2 (失败)      |
  |                                    |
  | 9. EndBattleTurn()                 |
  |    ProcessExpBattleResult()        |
  |    ResetBattleState()              |
```

### Normal Battle Flow (NPC Challenge)

```
Client                              Server
  |                                    |
  | 1. HandleSceneNPCInteraction       |
  |    (key 0x4000 on monster)         |
  |                                    |
  | 2. ValidateBattleEnter             |
  |    (check preconditions)           |
  |                                    |
  | 3. HandleBattleConfirm(n2=1)       |
  |    battleMode = 2                  |
  |                                    |
  | 4. HandleBattleEnterReq(0)         |
  | --- WT cmd=2 subcmd=1 ----------->|
  |    Type=2                          |
  |    battleState = 3 (waiting)       |
  |                                    |
  |<-- WT cmd=10 subcmd=22 -----------|
  |    result=1 (enter battle)         |
  |                                    |
  | 5. StartBattleTurn()               |
  |    battleState = 1 (player turn)   |
  |                                    |
  | 6. SelectBattleAction(0)           |
  |    (player selects attack)         |
  |                                    |
  | 7. HandleBattleActionDispatch      |
  |    (key 0x1000/0x4000, a2=1)       |
  |    phase=0: attack                 |
  | --- SendSerialPacket(8,3,target) ->|
  |    gameState[1468] = 1             |
  |                                    |
  |<-- WT cmd=10 subcmd=22 -----------|
  |    result=1 (action success)       |
  |                                    |
  | 8. StartBattleTurn()               |
  |    (next turn)                     |
  |                                    |
  |    ... repeat 6-8 until ...        |
  |                                    |
  |<-- WT cmd=10 subcmd=17 -----------|
  |    result=1 (victory) or 2 (defeat)|
  |                                    |
  | 9. ProcessExpBattleResult          |
  |    (update exp, return to scene)   |
  |                                    |
  | 10. ResetBattleState()             |
  |     BattleAction_Cancel()          |
```

### Escape Flow

```
Client                              Server
  |                                    |
  | HandleBattleConfirm(n2=2)          |
  | --- WT cmd=5 subcmd=1 ----------->|
  |    type=2, param=12                |
  |                                    |
  |<-- WT cmd=10 subcmd=17 -----------|
  |    result=4 (escape success)       |
  |    or result=5 (escape fail)       |
```

### Skill Use Flow

```
Client                              Server
  |                                    |
  | BattleAction_SelectSkill(seq,1)    |
  | --- WT cmd=4 subcmd=1 ----------->|
  |    type=29, param=1                |
  |    skillSeq=<seq>                  |
  |                                    |
  |<-- WT cmd=10 subcmd=?? -----------|
  |    type=2, flag=2, result=1        |
  |    (skill used successfully)       |
```

### Item Use Flow (in battle)

```
Client                              Server
  |                                    |
  | BattleAction_UseItem(type=2)       |
  | --- WT cmd=3 subcmd=1 ----------->|
  |    type=7, param=29                |
  |    "type"=2, "seq"=<seq>, "id"=<id>|
  |                                    |
  |<-- WT cmd=10 subcmd=22 -----------|
  |    result=1 (item used)            |
```

---

## Server Implementation Notes

### Battle Enter Response (cmd=10, subcmd=22)

Required fields:
- `"result"`: u8 — 1=允许进入战斗, 0=拒绝

When `result=1`, the client will call `StartBattleTurn()` and enter the battle loop.
The server should also send initial battle state (enemy HP, team info, etc.) in subsequent packets.

### Battle Operate Response (cmd=10, subcmd=22)

See `protocol.md` for the detailed `4/6` actioninfo format. Key points:

- Response object: `1/4/6` (or equivalent cmd=10 mapping)
- Must include `"actionnum"` field (number of action records)
- Must include `"actioninfo"` blob (damage/effect records)
- Must include `"teaminfo"` blob (team slot relations)
- Must include `"iteminfo"` blob (item usage info)

For each action record in `actioninfo`:
- Record 0: player's attack result (damage to enemy)
- Record 1 (if actionnum=2): enemy's counterattack result (damage to player)

### Battle End Response (cmd=10, subcmd=17)

Required fields:
- `"result"`: u8 — 1=victory, 2=defeat, 3=draw, 4=escape success, 5=escape fail

For victory, also send:
- `"lastexp"`: u32 — previous experience
- `"curexp"`: u32 — current experience after gain

### Battle Menu Result (cmd=10, subcmd=40)

Same result codes as subcmd=17. Used in faction/guild battle contexts.

---

## Key Function Reference

| Address | Name | Purpose |
|---------|------|---------|
| `0x1027D3C` | HandleSceneNPCInteraction | 战斗触发入口 |
| `0x1015DEC` | ValidateBattleEnter | 战斗进入验证 |
| `0x101AE42` | HandleBattleConfirm | 战斗确认对话框 |
| `0x1015E14` | HandleBattleEnterReq | 发送战斗进入请求 |
| `0x1017DA0` | StartBattleTurn | 开始战斗回合 |
| `0x101C3BE` | SelectBattleAction | 选择战斗动作 |
| `0x101CDC6` | HandleBattleActionDispatch | 战斗动作分发 |
| `0x101C85A` | BattleAction_Cancel | 取消/重置战斗UI |
| `0x101C99A` | BattleAction_Defend | 防御动作 |
| `0x101CA42` | BattleAction_UseItem | 使用物品/逃跑 |
| `0x101CAA4` | BattleAction_SendAttack | 准备攻击数据 |
| `0x101CCAE` | BattleAction_ConfirmTarget | 确认目标 |
| `0x101CD1E` | BattleAction_SelectSkill | 选择技能 |
| `0x101CD52` | BattleAction_InitAutoBattle | 初始化自动战斗 |
| `0x101CD78` | BattleAction_AutoBattleTick | 自动战斗tick |
| `0x101D1E2` | ResetBattleState | 重置战斗状态 |
| `0x103DABC` | HandleBattleResultEvent | 处理战斗结果(cmd=10,sub=22) |
| `0x1042AAE` | HandleBattleActionResult | 处理战斗动作结果(cmd=10,sub=17) |
| `0x1042E3E` | HandleBattleMenuResult | 处理战斗菜单结果(cmd=10,sub=40) |
| `0x1040A2C` | ProcessExpBattleResult | 处理经验战斗结果 |
| `0x1011ACA` | HandleBattleSkillResponse | 处理技能响应 |
| `0x102CB46` | HandleExpBattleResponse | 处理经验战斗响应 |
| `0x100E342` | HandleBattleActionState | 战斗动作状态机 |
| `0x100E2E4` | alloc_outgoing_game_event | 创建WT请求包 |
| `0x1019B4C` | SendSerialPacket | 发送序列化数据包 |
| `0x1019B72` | SendBattleActionReq | 发送战斗动作请求 |
| `0x101A2EA` | SendBattleActionReq2 | 发送战斗动作请求2(cmd=2,type=10,param=3) |
| `0x101A8EA` | DispatchBattleAction | 战斗动作类型分发(0=进入,1=攻击,2=技能,3=逃跑,4=物品,5=防御) |
| `0x1042F34` | InitScenePageTable | 初始化场景页面表(含net dispatch注册) |
| `0x10163A4` | SceneTickUpdatePositions | 场景tick-位置更新(含怪物碰撞检测) |
| `0x101621C` | StartSceneTransition | 场景过渡(怪物战斗入口) |
| `0x1015FAC` | DispatchSceneTransition | 场景过渡分发(按transitionType) |
| `0x1016C88` | SelectBattleTarget | 选择怪物目标(actor[315]==2) |
| `0x101AE7C` | HandleBattleConfirm0 | 怪物碰撞战斗确认(n2=0) |
| `0x101AE82` | HandleBattleConfirm1 | NPC挑战战斗确认(n2=1) |
| `0x1012ADC` | net_handle_actor_move_info | actor移动/战斗进入响应(channel=5) |
| `0x1017DC8` | EndBattleTurn | 结束战斗回合 |
| `0x1042CFC` | DispatchFactionNetEvents | 帮派频道网络事件分发(channel=10) |
| `0x1031D74` | CollectActorIdsByType | 按类型收集actor ID(actor[282]=type) |

---

## Battle State Values

| Offset | Variable | Values |
|--------|----------|--------|
| session+23682 | battleState | 1=玩家回合, 3=等待响应, 4=战斗中/场景切换, 5=聊天 |
| gameState+1399 | battleMode | 0=无, 2=主动攻击(NPC挑战时设置) |
| gameState+1468 | battlePending | 0=空闲, 1=等待响应 |
| gameState+2404 | battlePhase | 3=动作选择中 |
| gameState+2405 | battleSubPhase | 0=无 |
| gameState+1250 | inBattle | 0=否, 1=是 |
| gameState+1778 | hasTarget | 0=无目标, 非0=有目标 |
| gameState+1768 | currentSeq | 当前目标序列号 |
| gameState+1774 | currentType | 当前目标类型 |
| gameState+1492 | currentId | 当前目标ID |
| gameState+1278 | sceneType | 4=本地/怪物场景 |
| gameState+1134 | transitionTargetId | 场景过渡目标ID |
| gameState+1136 | inBattleLock | 1=战斗锁定(无法操作) |
| gameState+332 | isAttacker | 1=主动攻击(NPC挑战), 0=被动(怪物遭遇) |
| session+23673 | sceneTickFlag | 1=场景tick已处理 |
| session+23824 | transitionType | 场景过渡类型(0=世界,2=队伍,3=帮派,4=本地,7+=私人) |
| session+25347 | targetSelection | 怪物目标选择结果 |
| session+25358 | battleActionPending | 0=无, 非0=动作待处理 |
| session+25512 | battleActionType | 当前战斗动作类型 |
