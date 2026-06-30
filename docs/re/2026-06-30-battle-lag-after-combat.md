# 2026-06-30 战斗后一轮画面卡顿

## 现象

进入并完成一轮战斗后，回到地图或继续游戏时画面明显卡顿。此前运行日志中曾出现大量：

- `[info][battle] mp_probe ...`
- `[info][battle] mp_write ...`

## 排查结论

这次问题不在服务端战斗包本身，而在宿主侧遗留的战斗 MP 调试探针。

探针挂在三个高频路径上：

- `src/hookRam.c::hookRamCallBack`
  - Unicorn 全局 `UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE` 每次内存读写都会进入。
  - 旧代码每次都会调用 `vm_note_battle_mp_write()`。
- `src/main.c::hookCodeCallBack`
  - 每条 VM 指令都会进入。
  - 旧代码每次都会调用 `vm_note_battle_mp_pc()`。
- `src/mock-server.c::hook_vm_pool_code_callback`
  - VM 内存池代码 hook。
  - 旧代码也会调用 `vm_note_battle_mp_pc()`。

`vm_note_battle_mp_pc()` 和 `vm_note_battle_mp_write()` 会在战斗状态变量仍然有效时查找 battle screen、读取大量 battleR9 字段并打印日志。战斗结束后若战斗状态缓存没有立即清空，这些探针仍可能在主循环高频执行，造成明显卡顿。

## 修复

删除战斗 MP 实时探针及其调用点：

- 移除 `vm_note_battle_mp_write()` 的 RAM hook 调用。
- 移除 `vm_note_battle_mp_pc()` 的 code hook 调用。
- 移除 `vm_note_battle_mp_pc()` 的 VM pool hook 调用。
- 删除对应的 `mp_probe/mp_write` 调试函数。

保留正常的 mock-server 战斗状态和响应构造逻辑；没有改客户端逻辑，也没有写 CBE 全局状态。

## 验证

- `gcc -g -w -c src/main.c -o obj/main.codex-check.o` 通过。
- `rg` 确认 `vm_note_battle_mp_*`、实时 `mp_probe/mp_write` 调用已清空。

## 后续规范

需要临时定位 battleR9 或战斗字段时，可以使用短期只读探针，但不能长期挂在全局内存/指令 hook 中。问题读通后必须删除，避免污染运行性能和正常日志。
