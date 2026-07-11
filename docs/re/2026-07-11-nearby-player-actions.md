# 周围玩家操作菜单（2026-07-11）

phase: nearby-player-actions
status: implemented

## 请求与客户端路径

`江湖OL.CBE:0x0103023C` 在周围玩家列表的二级操作菜单中按选项索引调用下列发送器。这里的请求均为一个 `major=1` 的 WT 对象，目标字段为 `id`。

| 菜单操作 | 请求 | 额外字段 | 发送器 / 证据 |
| --- | --- | --- | --- |
| 玩家信息 | `10/2` | `type:u8=2` | `0x0101A5AA` `SendShopType2Event` |
| 查看装备 | `29/4` | — | `0x0101A2B6` |
| 入帮邀请 | `10/13` | `fid` | `0x0101A3C8`; 字符串 `0x0101A74C` 为“入帮邀请已发送” |
| 交易请求 | `21/1` | — | `0x0101C408` |
| 组队邀请 | `5/1` | — | `0x0101BEF4` |
| 发起私聊 | 无网络请求 | — | `0x0101621C` 只调度本地私聊界面转换 |
| 切磋请求 | `4/14` | — | `0x0102E624` |
| 加为好友 | `10/3` | — | `0x0101A2EA` |

运行时对请求的目标约束为：`id` 必须仍能解析为当前场景中另一个已可见服务会话；处理器不会吞掉任意同头 WT 包。

## 已实现下行

### 玩家信息 `10/2`

response:
  wt_kind: 10
  wt_subtype: 2
  fields: `result:u8=1`, `type:u8=2`, `fdegree:GBK-string`, `num:u8=2`, `playerinfo:two serial strings`

ida_evidence:
  binary: 江湖OL.CBE
  function: `HandleFactionDegreeResponse`, `0x010211A8`
  parser_reads: `result`, `type`, `fdegree`, `num`, `playerinfo`

`playerinfo` 使用客户端序列阅读器所需的 16 位长度前缀字符串。客户端按 `playerinfo` 的 `num` 条目依次显示，随后固定追加 `fdegree`；当前响应采用三行 GBK 中文排版：`姓名：<名称>`、`等级：<等级>级　职业：<战士/刺客/法师>`、`帮派：暂无　性别：<男/女>`。固定标签使用显式 GBK 字节，目标角色名保留其角色数据库中的 GBK 显示名。

### 查看装备 `29/4`

response:
  wt_kind: 29
  wt_subtype: 4
  fields: `result:u8=1`, `num:u8>=1`, `level:u16`, `name:string`, `work:u8(jobIndex 0..2)`, `sex:u8(1-based 1..2)`, `equipinfo:raw`

ida_evidence:
  binary: 江湖OL.CBE
  function: `HandleEquipInfoResponse`, `0x010216D6`; equipment view renderer `0x01020C1A -> 0x010206C4`
  parser_reads: `result`, `num`, `level`, `name`, `work`, `sex`, `equipinfo`; renderer indexes resource table with `2*work + sex`

`equipinfo` 没有内置行数，行数由外层 `num` 指定；每一行是 `u32 itemId, u16 seq, i16 runtime, i16 reserved, u8 attr_count=0`。服务端从目标在线会话捕获的实际装备槽位生成行，并只发送本地 `equip.dsh` 已知的 id。若会话尚未完成升级后的在线状态捕获，或请求来自离线好友行，则回退到目标职业的默认武器（`1001`、`1501` 或 `2001`）。

目标 `id` 的授权范围是当前场景中可见的其他角色，或持久化好友表中属于当前角色的好友行；后者覆盖好友列表上的“查看装备”请求，避免将任意 `29/4` 包泛化为可查询所有角色。

### 好友邀请 `10/3 -> 10/4 -> 10/5 -> 10/6`

好友请求不再在发起时直接写入好友表。服务端先向目标角色排队一条下行邀请；目标客户端在下一次正常场景同步轮询中收到：

| 阶段 | WT | 字段 | 客户端证据 |
| --- | --- | --- | --- |
| 发起 | `10/3` | `id` | `0x0101A2EA` |
| 目标邀请 | `10/4` | `id`, `name` | `0x010114FC`；显示“邀请加你为好友”并安装确认回调 |
| 目标回复 | `10/5` | `id`, `result:u8`（`1` 同意，`2` 拒绝） | `0x010114A4` |
| 发起方结果 | `10/6` | `result:u8`, `name` | `0x010114FC`；显示同意或拒绝结果 |

只有目标回复 `result=1` 后，服务端才将双向好友关系写入 `nvram/mock_service_friends.bin`。因此好友列表需要先经过真实的目标端确认才会显示新好友。

### 交易请求 `21/1 -> 21/2 -> 21/3 -> 21/4`

| 阶段 | WT | 字段 | 客户端证据 |
| --- | --- | --- | --- |
| 发起 | `21/1` | `id` | `0x0101C408` |
| 目标邀请 | `21/2` | `id`, `name` | `0x01011132`；打开同意/拒绝确认框 |
| 目标回复 | `21/3` | `id`, `result:u8`（`1` 同意，`2` 拒绝） | `0x01011076` |
| 发起方结果 | `21/4` | `result:u8`, `name` | `0x01011132`；同意时进入客户端交易界面，拒绝时显示提示 |

本轮实现的是邀请与确认的跨客户端投递；具体交易物品、锁定和结算的后续业务包仍未实现。

### 投递机制

远程 mock service 是短连接的请求/响应模型，无法对空闲 CBE 客户端主动写 socket。每个在线客户端已有 event-7 场景同步轮询，因此服务端将待投递的好友/交易通知作为正常 WT 对象附在该轮询响应中；每次只投递一个通知，且已有未回复的模态邀请时不会叠加新的邀请框。

### 其余邀请与请求

入帮、组队、切磋发送器仍会在发起端立即显示“已发送”提示，服务端目前仅返回空 WT 传输确认。它们的目标端入站通知与接受/拒绝协议尚未根据客户端证据实现；不会伪造目标端的“同意”、帮派关系、队伍关系或切磋战斗包。

negative_evidence:
  missing_or_bad_field: `29/4` 缺少 `equipinfo` 时，`HandleEquipInfoResponse` 走“查看玩家装备信息异常”分支；`result=1` 配合 `num=0`/零行流会让装备查看状态半初始化。`work` 若错误写为持久化职业值 `1..3`，职业显示会错位；职业 3 更会使 `2*work+sex` 越过六项角色资源表，随后在 `DrawMapTileLayer(0x01004E9C)` 访问无效地图层并闪退。
  observed_failure: 先前把菜单动作当作 `2/7` 的变种会导致未处理 WT；这些动作是独立的业务头。好友列表的 `29/4` 目标 id 不一定仍在当前 nearby seed 中，若只按 nearby 解析会落入未处理断言。

unknowns:
  - name: 入帮邀请、组队邀请、切磋请求的目标端入站通知与接受/拒绝包
    current_value: 未实现
    why_kept: 当前发送端没有相应下行解析契约；伪造成功包会跳过真实目标端流程。
