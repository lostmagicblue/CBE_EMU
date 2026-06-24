# 日志打印规范

## 目标

本项目的日志只服务于两个目的：

- 让开发者快速知道 emulator 为什么停止、哪个平台 API 还没实现、哪个文件或资源无法读取。
- 记录少量稳定的生命周期信息，便于确认启动、加载、GDB、网络调度等基础框架是否正常。

日志不是协议取证的长期载体。协议证据、字段解释、IDA 地址、失败样例必须写入 `docs/re/`，不要用常驻打印、文件日志或开关代替文档。

## 分类

### FATAL

用于即将 `assert(0)`、返回不可恢复错误、或 emulator 无法继续运行的情况。

格式：

```c
printf("[fatal][component] message key=%u addr=%08x\n", value, addr);
assert(0);
```

允许内容：

- 无法打开必要 CBE/CBM/resource 文件。
- Unicorn 映射、读写、执行失败。
- 内存池耗尽、非法 free、资源解码失败。
- 未实现且当前会阻断流程的平台 API。

要求：

- 同一错误点只打印一行。
- 必须包含足够定位的信息：component、API/函数名、idx/address、关键 size/path。
- 不打印大块 buffer、packet、屏幕/角色/战斗结构。

### ERROR

用于操作失败但调用方可能恢复或继续运行的情况。

格式：

```c
printf("[error][fileio] open path=%s mode=%s\n", path, mode);
```

允许内容：

- 可选资源缺失。
- 文件读写失败。
- 网络 mock 找不到可构造响应但不立即 assert。
- 解码器返回错误字符串。

要求：

- 每次失败最多一行。
- 不在循环内无节制打印；高频路径要改成状态返回或一次性错误。

### WARN

用于行为可继续但值得注意的兼容或降级路径。

格式：

```c
printf("[warn][scheduler] queue_full dropped=%u\n", dropped);
```

允许内容：

- 队列满、事件被丢弃。
- 使用 fallback 数据。
- 资源名规范化后仍可解析但存在异常。

要求：

- 不用于正常流程确认。
- 不输出协议字段详情；字段详情进 `docs/re/`。

### INFO

用于低频、稳定的生命周期信息。

格式：

```c
printf("[info][cbe] code_offset=%08x code_len=%u endian=%u\n", off, len, endian);
```

允许内容：

- 启动时的 CBE header 摘要。
- GDB server 启停。
- 主窗口、VM 初始化、主要模块加载完成。

要求：

- 默认启动一次只出现少量 INFO。
- 不在每帧、每条指令、每个 packet、每个资源读取中打印。

### DEBUG

默认不新增长期 DEBUG 打印。

允许临时使用 `DEBUG_PRINT` 或短期 `printf` 做当前问题定位，但必须满足：

- 只在本地调试期间存在。
- 不新增宏开关、环境变量开关或日志文件来隐藏它。
- 完成协议点或修复后删除。
- 结论写入 `docs/re/`。

## 禁止项

- 常驻 packet dump、WT 字段逐项打印、请求/响应 hex dump。
- 常驻 PC、LR、R0-R12、内存 watch、屏幕结构、角色结构、战斗结构打印。
- stdout mirror、`bin/logs/*.log`、`*_trace.log`、storage trace。
- 新增 `ENABLE_LOG_*`、`TRACE_*`、环境变量日志开关。
- 在 mock handler 里用长 evidence 字符串刷屏。
- 在高频路径里打印正常成功流程，例如每次 `vm_memcpy`、每帧 LCD、每个资源读取。

## 命名

统一使用：

```text
[fatal][component] ...
[error][component] ...
[warn][component] ...
[info][component] ...
```

推荐 component：

- `cbe`
- `vm`
- `hook`
- `fileio`
- `resource`
- `lcd`
- `scheduler`
- `network`
- `mock`
- `gdb`
- `gif`
- `png`

消息字段使用 `key=value`，地址统一 `%08x`，长度统一无符号十进制或必要时十六进制。中文可以用于解释性错误，但 key 名保持英文，方便搜索。

## 未实现 API 打印

平台 manager 中的未实现 API 可以保留一行 FATAL：

```c
printf("[fatal][vm_api] unimplemented manager=audio idx=%u\n", idx);
assert(0);
```

已经实现或静默兼容的 API 不打印 `[call]xxx`。正常调用打印会很快变成噪音。

## Mock Server 日志

mock-server 的长期行为不靠日志证明。

允许：

- 遇到不可恢复构包错误时打印一行 FATAL/ERROR。
- unhandled 请求如果必须阻断，可打印一行摘要：`kind/subtype/object_count/len/source`。

禁止：

- 打印完整 request/response bytes。
- 打印每个命中的 handler。
- 打印 IDA evidence、字段解释、实验结论。

这些内容写入 `docs/re/` 对应协议记录。

## 临时观测流程

1. 在 `docs/re/` 写明要验证的问题。
2. 加一处最小打印，只输出必要 key。
3. 运行并记录结论。
4. 删除打印。
5. 把结论写回文档。

不允许把临时打印改名为 DEBUG 后长期保留。

## 提交前检查

提交前执行：

```powershell
rg -n "printf\\(|fprintf\\(|puts\\(|DEBUG_PRINT" src
rg -n "trace|TRACE|net_packets|storage_trace|dump" src
```

检查标准：

- 新增打印必须属于 FATAL/ERROR/WARN/INFO 中的一类。
- 新增打印必须低频、单行、可定位。
- 不能新增常驻 trace、packet dump 或日志开关。
- `make` 必须通过。

