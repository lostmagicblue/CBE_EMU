# 江湖OL.CBE 网络模拟响应记录

本文件记录已经验证过的模拟响应契约，避免后续会话重新绕回运行时猜包。

## 原则

- 让游戏代码自己推进。模拟器只实现平台/API语义和服务端网络响应。
- 不在网络 mock 中主动写 CBE 全局变量，不强制 CBE 函数返回值。
- 临时验证逻辑时可以短暂加只读日志；验证后不要留下会改变游戏状态的 hook。
- 启动测试时从 `E:\DevOs\CBE_EMU\bin` 作为工作目录启动 `main.exe`，点击操作由人工完成。

## WT 包格式

当前网络响应使用 `WT` 包：

- `out[0..1] = "WT"`
- `out[2..3] = packet_len`，大端
- `out[4] = object_count`
- 每个对象头 6 字节：`major, kind, subtype, 0, object_len_be16`
- 对象字段使用 `name_len, name, value_len_be16, value`
- 整数字段值内部编码为 `0, byte_len, big_endian_value`
- 字符串/二进制字段值内部编码为 `data_len_be16, data`

`src/main.c` 里保留了 helper：

- `vm_net_mock_begin_wt_object`
- `vm_net_mock_finish_wt_object`
- `vm_net_mock_finish_wt_packet`
- `vm_net_mock_put_object_u8/u32/blob/string`

新增/调整响应时优先复用这些 helper，不要手写头部。

## 启动版本响应

触发请求包含 `version`。

已验证的正确响应包含两个对象：

- subtype `5`：字段 `result`
- subtype `9`：字段 `type`, `id`, `code`

原因：

- `handle_version_update_response` 读取 subtype `5` 的 `result`。
- `startup_update_net_callback` 读取 subtype `9` 并进入 `startup_handle_update_metadata`。
- 只有 subtype `5` 会让 `update_state=2`，但启动屏不会移除，界面会卡住。
- `type=0` 是“无需更新/本地已可用”的完成路径，CBE 自己会调用 screen remove。

当本地已有 `JHOnlineData/MMORPGTempcbm` 和 `JHOnlineData/mmorpg_updateversioncbm` 时，内置版本响应返回 `result=0`，并在 data event 后追加 close event `9`。不要改成 event `8`，CBE 的 net wrapper 对 event `8` 不会调用当前对象 callback。

## 更新分片响应

触发请求包含 `start` 和 `id`。

字段：

- `totalsize`
- `crc`
- `type`
- `name = MMORPGTempcbm`
- `data`

`start` 必须从请求字段读取，`crc` 是截至当前分片末尾的 signed-byte 累计和。分片大小当前限制为 `0x1000`。

更新 payload 来源优先级：

- `JHOnlineData/MMORPGTempcbm.mock`
- `JHOnlineData/mmBattleMstarWqvga.cbm`
- `JHOnlineData/mmGameMstarWqvga.cbm`
- `JHOnlineData/mmTitleMstarWqvga.cbm`

不要直接把已经安装好的 `JHOnlineData/MMORPGTempcbm` 再当下载源，否则会掩盖真实安装/更新路径。

## 业务进场响应

业务网络入口是 CBE `0x01012e4c`。

已确认字段：

- 登录/角色：`actorinfo`, `playerinfo`
- 进场/地图：`scene`, `posinfo`, `npcnum`, `npcinfo`
- 资源更新：`version`, `start`, `totalsize`, `crc`, `data`, `result`
- 其他：`rolesinfo`, `roleinfo`, `iteminfo`, `giftinfo`, `battleinfo`

`type=2/3` 的游戏响应需要：

- 默认 response subtype `0x1a`
- `scene = "00蓬莱仙岛_01"` 的 GBK 字节
- `posinfo` 包含两个 i16 坐标，当前为 `120,120`

如果后续继续解析角色资源偏移，优先反推 CBE `0x0100fa88` 的 `actorinfo` 格式，不要靠主动写 actor 字段推进。

## 屏幕/生命周期配套修正

这些不是网络响应，但会影响启动网络流程是否能走完：

- CBE 更新屏调用 screen manager idx `6` 移除自身后，模拟器需要恢复栈顶下层屏幕。
- 下层屏幕可能没有 destroy 函数，destroy 为 0 时应跳过。
- 这些是模拟器屏幕管理语义修正，不是游戏状态注入。

## 已验证启动链路

1. 启动屏 state 进入 `7`。
2. net open event `5` 后，CBE 调 `send_version_update_request`。
3. mock 返回 subtype `5 + 9` 版本响应。
4. `handle_version_update_response` 设置 update flags 和 `update_state=2`。
5. `startup_handle_update_metadata` 解析 `type/id/code`，CBE 自己调用 screen remove。
6. 模拟器恢复底层屏幕。
7. CBE 加载 `JHOnlineData/mmTitleMstarWqvga.cbm` 并进入动态 CBM 屏幕。

