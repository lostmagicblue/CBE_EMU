# 2026-06-27 Role Position Follows Active Role

## Problem

Selecting a title role could still enter a map at a position that looked like the
old mock-wide player position instead of the selected role's persisted
`scene/x/y`. New title-created roles also need a deterministic Penglai starting
point.

## IDA Evidence

- `mmTitleMstarWqvga.cbm:0x39FC` copies the selected role row actor ID into the
  login record workspace.
- `mmTitleMstarWqvga.cbm:0x1B9C` serializes subtype `1/6` with that selected
  actor ID.
- `mmTitleMstarWqvga.cbm:0x53EC` handles subtype `6` by continuing through the
  login record path, so the following scene actorinfo must be built for the
  active role.
- `JianghuOL.CBE:0x0100FA88` parses scene-side `actorinfo`; the trailing
  `sceneKey` string and trailing grid words seed the visible map actor.
- `JianghuOL.CBE:0x010396D6` and `0x01039890` parse scene enter/change results
  by reading the same response object's `scene` string plus tagged `posinfo`
  `x/y` stream. This means scene key and spawn coordinates must come from one
  role position source.

## Negative Evidence

The old local DB bootstrap migrated `nvram/jhol_mock_player_pos.bin` into the
default role when `nvram/jhol_mock_roles.bin` did not exist. That was useful
before role rows existed, but it turns the legacy compatibility mirror back into
source-of-truth state and can make newly initialized roles inherit an unrelated
global map position.

`vm_net_mock_load_player_pos_state()` also returned immediately when its process
global cache had been loaded once. After a role selection, helpers such as
`vm_net_mock_scene_key_name()` and `vm_net_mock_scene_spawn_x/y()` could keep
serving the old cached scene/coordinate until a later save happened.

## Implementation

- New/default roles now initialize to the fixed Penglai starting scene returned
  by `vm_net_mock_default_scene_name()` with `VM_NET_MOCK_ROLE_INITIAL_X/Y`
  `(223,382)`.
- The role DB no longer imports the legacy player-position file during first
  initialization. The legacy file is only written as a mirror after role
  selection, role creation, role deletion fallback, and position saves.
- The process player-position cache is rebuilt from `vm_net_mock_active_role()`
  each time it is requested instead of trusting an old loaded flag.
- `sceneKey`, generic `scene/posinfo`, and no-target `30/1` scene-enter helpers
  now default to the active role's scene/position. `CBE_SCENE_KEY` and
  `CBE_SCENE_POS_X/Y` remain explicit test overrides.

## Validation

- `make` succeeded.
- Manual include compile succeeded with
  `gcc -g -w -c src/main.c -o obj/main.codex-check.o`; the temporary object was
  removed afterward.
- `git diff --check` reported only CRLF normalization warnings, no whitespace
  errors.
- Existing runtime DB check from `bin/nvram/jhol_mock_roles.bin` showed one
  saved role at its own stored scene/position. This change intentionally does
  not reset existing roles; it changes new/default role initialization and the
  active-role source path.
- Live startup was not run in this pass because normal scene entry can write the
  current role's saved coordinates.
