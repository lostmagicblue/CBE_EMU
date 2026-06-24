# Penglai Empty NPC And Transfer Notes

Date: 2026-06-24

## Current Contract

- Scene NPC seeding is not part of the current server contract. The mock server returns empty NPC data while map transfer work continues.
- `5/10`, `12/1`, scene task subset, actor-other `2/10`, and actorinfo NPC seed paths must default to zero NPCs.
- Penglai local SCE portal rectangles are documented in `tmp/all_sce_bundle/*/scene.json` and mirrored in the mock portal fallback tables for route lookup.

## Verified

- Fresh startup still reaches `蓬莱仙岛_十二域`.
- Direct startup into `00蓬莱仙岛_02.sce` with `CBE_KEEP_EMPTY_PENGLAI02_SAVE=1` reaches visible map `蓬莱一线剑谷`.
- Reproduced old crash by starting at `00_蓬莱仙岛01.sce` position `(200,486)` and stepping through the bottom portal:
  - old failing chain ended after `2/3 -> builtin-scene-change`;
  - process asserted in `hookRam.c` at `pc=0x01014ee0`.
- Current chain no longer asserts:
  - `2/10 -> builtin-actor-other-only10`
  - `2/3 -> builtin-scene-change`
  - optional `25/5 -> builtin-scene-default-event`

## Remaining Gap

The transfer no longer crashes, but the client remains on the loading screen after `2/3` when entering `00蓬莱仙岛_02.sce` from `00_蓬莱仙岛01.sce`. Since direct `_02` startup renders correctly, the remaining missing contract is likely a scene-change completion state, not scene resource availability or NPC data.

Next probe should focus on the client-side completion state after `2/3`, especially what live server sends between local portal countdown completion and the second `ScreenInit`.
