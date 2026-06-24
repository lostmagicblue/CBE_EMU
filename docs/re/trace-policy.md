# Trace 规范

## 原则

项目默认不保留常驻 trace。服务端复刻依赖协议证据，但证据应沉淀到 `docs/re/`，不要长期依赖运行时日志开关、海量文件或散落的 `trace_*` helper。

## 允许保留

- 真实错误输出：`printf`、`assert`、GDB 停止原因、无法访问内存等会直接阻断运行的问题。
- 业务状态：mock-server 自身需要维护的变量、持久化存档、响应构包逻辑。
- 临时取证代码：为当前卡点临时加入，必须在完成协议点后删除。

## 禁止保留

- 常驻 `*_trace.log`、packet dump、stdout mirror、storage trace。
- 新增宏开关或环境变量开关来隐藏 trace。
- 长期存在的 `trace_*` hook、watch helper、按 PC 分发的取证状态机。
- 只为了日志而读取大量寄存器、VM 内存、屏幕状态、战斗状态。
- 在 handler 中输出长 evidence 字符串替代文档。

## 临时取证流程

1. 先在 `docs/re/` 写清楚要验证的问题、目标函数或目标 packet。
2. 临时加最小观测点，只读取必要字段。
3. 运行一次或少量几次，记录关键现象。
4. 把结论写回 `docs/re/`：请求签名、响应契约、IDA 证据、运行现象、失败样例。
5. 删除临时取证代码和产物，再提交服务端逻辑。

## 协议实现记录

每个新增 handler 的长期证据写成文档，而不是 trace：

```text
phase:
request:
response:
ida_evidence:
runtime_evidence:
negative_evidence:
unknowns:
```

## 提交前检查

- `rg -n "trace|TRACE|*_trace.log|net_packets|storage_trace" src` 不应出现新的运行日志设施。
- `bin/logs/` 不应作为提交内容。
- `make` 必须通过。
- 如确实需要临时 trace，提交前必须删除，并在 PR/提交说明中写明证据已迁移到 `docs/re/`。
