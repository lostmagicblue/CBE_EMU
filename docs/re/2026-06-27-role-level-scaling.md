# 2026-06-27 Role Level Scaling

## Goal

Use one server-side level model for title role rows, scene actor info, login
state, and battle settlement:

- new roles start at level 1 with 0 total EXP
- the first upgrade needs 100 EXP
- each later upgrade needs 100 more EXP than the previous upgrade
- total EXP remains the persisted source of truth; `level` is normalized from it

## Formula

EXP needed to reach a level:

```text
levelStartExp(level) = 100 * (level - 1) * level / 2
```

EXP needed to advance from the current level to the next level:

```text
nextLevelCost(level) = level * 100
```

Examples:

```text
level 1 starts at    0 total EXP, next cost 100
level 2 starts at  100 total EXP, next cost 200
level 3 starts at  300 total EXP, next cost 300
level 4 starts at  600 total EXP, next cost 400
level 5 starts at 1000 total EXP, next cost 500
```

## Wire Fields

`mmBattleMstarWqvga.cbm:0x743C` only proves the battle settlement parser reads
these fields in order; the actual progression rule is server-owned:

```text
exp, lastexp, curexp, persentexp, energy, energymax, gold, level, ...
```

The mock-server sends:

```text
exp         reward EXP gained by this settlement
lastexp     total EXP at the current level's start threshold
curexp      total persisted EXP after applying the reward
persentexp  EXP earned inside the current level
level       level derived from total persisted EXP
```

For example, after a role reaches 310 total EXP:

```text
level = 3
lastexp = 300
curexp = 310
persentexp = 10
next level starts at 600
```

## Implementation

`src/mock-server.c` now centralizes the rule in:

```text
vm_net_mock_role_level_start_exp()
vm_net_mock_role_level_from_exp()
vm_net_mock_role_last_level_exp()
vm_net_mock_role_exp_percent()
```

`vm_net_mock_role_normalize()` continues to rewrite the persisted `level` from
`exp`, so old rows and newly created rows use the same rule after load.

Battle settlement now reports the post-reward level bracket instead of using the
pre-reward total EXP as `lastexp`.

## Validation

Validated with:

```text
gcc -g -w -c src/main.c -o obj/main.codex-level.o
make
git diff --check
```

The compile and normal `make` completed successfully. `git diff --check` only
reported Git CRLF conversion warnings for touched text files; no whitespace
errors were reported. The temporary object was removed afterward.
