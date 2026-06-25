# <阶段或卡点标题>

Date:

Status: investigating | ready-to-implement | implemented | validated | blocked

## 1. 当前卡点

- 可见现象：
- 触发方式：
- 本轮最小目标：

## 2. 运行时证据

- 最新请求/响应现象：
- 当前屏幕 / loading / assert：
- 已知会发生的下一步：
- 实际缺失的下一步：

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `0x...` | | |

## 4. 调用链 / 业务流程

1. 
2. 
3. 

## 5. 结构体 / 状态字段笔记

- owner:
- offset / field:
- read site:
- write site:
- current meaning:
- confidence:

## 6. 请求 / 响应契约

### Request

- WT:
- objects:
- key fields:
- sample len:

### Response

- WT:
- objects:
- key fields:
- arrays / strings / blobs:

## 7. 成功路径与失败路径

### Success path

- 

### Failure path

- 

## 8. Negative Evidence

- 尝试过什么：
- 结果如何：
- 说明了什么：

## 9. Unknowns / Hypotheses

- unknown:
  - current guess:
  - why it matters:
  - next probe:

## 10. 本轮实现计划

- 计划改动：
- 目标文件：
- 为什么这次只改这一小块：

## 11. 验证清单

- [ ] 请求被预期 detector 命中
- [ ] 响应长度正确且通过正常网络事件入队
- [ ] 客户端 parser 经过预期分支
- [ ] 没有强写客户端全局状态
- [ ] 客户端出现新的自然后续行为
- [ ] 结果已回写到本文件
