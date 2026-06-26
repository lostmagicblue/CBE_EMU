# 2026-06-26 Local Role DB

## Goal

Move mock-server player state from scattered constants/env defaults into a local
server-side role database.

Current contract:

- store a role list locally, max 5 roles for the title role-list parser
- allow 0 persisted roles after deleting the last role; the title client then
  displays only its create-role sentinel row
- persist HP, MP, money, scene, position, level, total EXP, and backpack items
- derive level as `exp / 100 + 1`
- grant 10 EXP once per victorious monster battle settlement
- keep position and backpack state in the selected role row; do not use legacy
  mock-wide position or backpack state as a source of truth

## IDA Evidence

### Title Role List

`mmTitleMstarWqvga.cbm:0x3544` parses the title `actorinfo` field as a compact
role table:

```text
u8 count
repeat count:
  tagged u32 roleId
  tagged u8 job
  tagged u8 sex
  string name
  tagged i16 level
```

The parser clamps count to 5. It does not read HP, MP, money, scene, or position
from this title payload.

### Scene Actor Info

`Jianghu OL.CBE:0x0100FA88` parses the scene/login `actorinfo` player blob.
The mock keeps the recovered stream shape and now sources these fields from the
active local role:

```text
roleId, job/visual, sex/visual, name,
playerId, account/display,
level/status word,
hp, hpMax, mp, mpMax,
exp summary,
money/gap09C0,
backpackCapacity,
target/grid fields,
actor resource,
scene key,
motion/grid words
```

### Scene Position

`Jianghu OL.CBE:0x010396D6` and `0x01039890` parse scene-enter/change results
with a scene string plus `posinfo`. Existing safe-position adjustment remains in
place; the final safe scene/x/y now writes only the active role record.

### Backpack

`Jianghu OL.CBE:0x01039952` parses `1/30/21` by reading `gridnum`, then raw
`iteminfo` rows of `itemId`, `seq`, `count`, and the common item-extra block.

`mmGameMstarWqvga.cbm:0x418C` parses `1/17/1` full-list `iteminfo` as a counted
row list of `itemId` plus the same common item-extra block. Both response paths
now source rows from the active role's backpack array.

### Battle Settlement

`mmBattleMstarWqvga.cbm:0x743C` handles battle settlement object `1/4/7` and
reads, in order:

```text
exp, lastexp, curexp, persentexp,
energy, energymax, gold, level, result, bagstatus, hp, mp,
itemnum, iteminfo, autorevive
```

Therefore EXP/level/gold/HP/MP must be sent in subtype 7, not guessed from a
later scene fallback.

## File Format

Primary file:

```text
nvram/jhol_mock_roles.bin
```

Header:

```text
magic      "JHR1"
version    2
activeRoleId
roleCount
roles[5]
```

Role row:

```text
u32 roleId
char name[32]
u8 job
u8 sex             // scene-side 0..1; title list sends sex+1
u8 backpackCapacity
u8 reserved
u32 level          // normalized from exp
u32 exp            // total EXP
u32 hp, hpMax
u32 mp, mpMax
u32 money
char scene[64]
u16 x, y
u8 backpackItemCount
u8 reserved
u16 nextBackpackSeq
backpackItems[40]:
  u32 itemId
  u16 seq
  u16 reserved
  u32 count
```

Default role:

```text
roleId = 10001
name = GBK bytes for the existing role name
job = 1
sex = 0
hp/mp = 120/100
money = 1000
backpackCapacity = 40
scene = default local Penglai scene
position = 223,382
backpack = item 800 seq 1 count 5
nextBackpackSeq = 2
```

Version 1 role DB files are upgraded in-place to version 2 by copying existing
role fields and seeding each role with the default backpack. The old
`nvram/jhol_mock_player_pos.bin` mirror is no longer read or written.

## Server Behavior

Login and scene enter:

- `vm_net_mock_build_actor_info()` reads active role ID/name/job/sex/level,
  HP/MP, EXP, money, backpack capacity, scene, and position.
- scene/login actorinfo uses the active role name for its role name, display
  name, and short label defaults so map-side role information matches title
  selection.
- actor motion-resource generation uses the active role job/sex as its default,
  so the map sprite resource follows the selected role unless a `CBE_ACTOR_*`
  environment override is explicitly set.
- `vm_net_mock_append_login_success_object()` sends `lastexp` as the current
  level start, `curexp` as total EXP, and `persentexp` as EXP within the level.

Title role list:

- `vm_net_mock_build_title_role_list_actorinfo()` emits all stored roles up to
  the client limit of 5.
- title role-list compact actorinfo emits `jobIndex = role->job - 1` and
  `sex = role->sex + 1`. This matches `mmTitle:0x3544` and the create-success
  row layout at `mmTitle:0x5324`.
- role select handles request `1/1/6`, parses `actorID`, and updates
  `activeRoleId` before scene/login actorinfo is built. Scene helpers read
  `scene/x/y` from that active role row.
- title role create handles request `1/1/7`, appends a persisted role when
  capacity allows it, starts the new role at the default Penglai position with
  the default teleport-stone stack, and returns `actorid/result` to the title
  parser.
- role DB load repairs duplicate legacy rows that were previously persisted
  with the default name after a create-payload decode miss. The first default
  role keeps the GBK default name; later duplicate default rows become stable
  fallback names such as `Role10002`.
- title role delete handles request `1/1/8`, removes the matching persisted
  role by `actorID`, and returns `result=0` only on success.

Money sync:

- shop `coolmoney`, group/type `money`, and generic game-type `money` read the
  active role money.

Backpack:

- scene bootstrap `30/21` emits the active role's backpack rows and uses each
  row's persisted `seq/count`.
- backpack UI `17/1` emits the active role's backpack rows and capacity.
- NPC/shop buy `17/2` adds or stacks the purchased item into the active role
  before returning `14/3 { seq, result }`.

Battle:

- battle start reads active role HP/MP/ID/name.
- battle settlement applies reward only when the enemy HP is zero, player HP is
  nonzero, and the current battle serial has not already been rewarded.
- default monster reward is `10` EXP and `0` gold. `CBE_BATTLE_REWARD_EXP` and
  `CBE_BATTLE_REWARD_GOLD` remain focused experiment overrides.

## Validation

The normal `make` link to `bin/main.exe` was blocked because the executable was
in use. Since the Makefile does not track the included `src/mock-server.c`, the
current source was validated with:

```text
gcc -g -w -c src/main.c -o obj/main.codex.o
```

That temporary object compiled successfully. A same-command temporary link to
`bin/main.codex-build.exe` also succeeded. The temporary object and exe were
removed afterward.
