# Startup Version To Map

phase: startup version handshake -> title login -> map entry
status: validated

## Request / Response Chain

1. Version request

- request: WT `18/9`, sample len `137`, fields include `version`, `codeVersion`, `client`, `Screen`, `plat`, `imsi`, `proj`.
- response source: `builtin-version`.
- response len: `63`.
- effect: startup leaves "获取版本信息" and reaches title menu.

2. Login and title flow

- request: WT `1/12`, sample len `86`.
- response source: `builtin-login`.
- follow-up requests observed during validation:
  - WT `1/12` -> `builtin-login`
  - WT `1/16` -> `builtin-title-rolelist-wait-server-select`
  - WT `1/4` -> `builtin-title-server-select`
  - WT `1/6` -> `builtin-title-role-select`
  - WT `5/10` -> `builtin-group-type1`
  - WT `7/7` -> `builtin-game-type`
  - WT `12/1` -> `builtin-scene-resource-followup`

## Bug Fixed

`vm_net_mock_build_battle_operate_response()` returned a battle response for non-battle packets because the non-match branch did not return unless the relaxed battle detector matched. The startup version request `WT 18/9` was incorrectly handled as `builtin-battle-operate`, so the startup parser never received version metadata and stayed on the communication screen.

Fix: return `0` whenever the strict battle-operate detector does not match. The relaxed battle path remains handled by `vm_net_mock_build_battle_operate_response_fallback()`.

## Runtime Evidence

Validated with:

```powershell
cd E:\DevOs\CBE_EMU\bin
.\main.exe --autotest --shot-ms=1000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f
```

Final screenshot reached the map UI with title `蓬莱仙岛_十二域` and the player label visible.

## Unknowns

- The title/login chain still emits repeated queued event entries while callbacks are pending. Current behavior is parser-safe and reaches map, but the scheduler duplicate policy may need a separate cleanup pass.
- The role-list wait response is still staged as a no-op before explicit server select; keep it unless IDA evidence proves the live server combines those payloads.

