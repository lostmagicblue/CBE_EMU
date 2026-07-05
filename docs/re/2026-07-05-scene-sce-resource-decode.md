# 2026-07-05 场景 SCE 资源解码修复

## 现象

运行时进入 `c00蓬莱仙岛_01.sce` 后，服务端日志出现：

```text
mock_scene_entry_missing scene=c00..._01.sce exit=1 action=no-center-fallback
mock_scene_change_unresolved_entry_ack scene=c00..._01 exit=1 ... action=ack-only-no-pending
```

这说明 `WT 2/3` 场景切换请求已经到达服务端，但服务端没有从目标 SCE 中解析出 `exit=1` 对应的入口坐标，只能下发无 `posinfo` 的 ack。

## 根因

`web/fs/JHOnlineData/*.sce` 不是裸 `SCE2` 文件，而是：

```text
little-endian u32 block_len
u8 resource_type
big-endian u32 compressed_len
big-endian u32 decoded_len
LZSS stream
```

真实的场景 payload 在 LZSS 解码后才以 `SCE2` 开头。

本次出错的 `c00蓬莱仙岛_01.sce` 原始文件头为：

```text
FA 00 00 00 02 00 00 00 F1 00 00 01 1B A5 53 43 45 32 ...
```

其中 `SCE2` 恰好出现在压缩流内部。旧的 `vm_net_mock_load_scene_resource()` 先扫描 raw 数据，看到 32 字节内有 `SCE2` 就直接返回 raw，导致后续 `edge_portal` 解析器扫描的是未解压数据。

## 修复

`vm_net_mock_load_scene_resource()` 现在优先识别真实资源包装：

- 若存在合法 `block_len` 且资源类型为 `1/2`，先按客户端等价的 `GetStreamDataFormRes/LzssDecode` 规则解码；
- 解码结果必须能解析出 `SCE2` payload，否则视为读取失败；
- 只有不符合资源包装格式时，才接受裸 `SCE2` payload。

这不是坐标兜底，而是服务端读取真实资源的前置步骤。

## 本地验证

只读解析脚本确认：

```text
c00蓬莱仙岛_01.sce raw=254 type=2 compressed=241 decoded=283
  spawn=(223,382) target=00蓬莱仙岛_02.sce entry=1 targetEntry=0 rect=(203,402)-(240,422)

00蓬莱仙岛_02.sce raw=267 type=2 compressed=254 decoded=336
  spawn=(128,45) target=c00蓬莱仙岛_01.sce entry=1 targetEntry=2 rect=(108,5)-(148,25)
  spawn=(396,473) target=c00蓬莱仙岛_03.sce entry=0 targetEntry=3 rect=(421,453)-(441,490)
```

因此 `c00蓬莱仙岛_01.sce exit=1` 不应再进入 `mock_scene_entry_missing`，而应按目标 SCE 的 `entry=1` 入口落到 `00蓬莱仙岛_02.sce` 上方入口附近。
