# 2026-07-01 Merge Response Buffer Repair

## Scope

Merge commit `81ad3470b7ae67fb23129bd0a760f1842ab0b6b6` has parents:

- local/user line: `fbc49fd7c91696659b5ed92effee618005ddce93`
  (`1144822034@qq.com`)
- remote line: `66a6d654d3b39b1c6d6b43e633d8bd47b233748a`
  (`2350321870@qq.com`)

For the merge repair, prefer the local/user line unless there is explicit
server-protocol evidence for changing it.

## Fix

Relative to the local/user parent, the only `src/mock-server.c` change introduced
by the merge was freeing `g_netMockResponseVmPtr` before allocating the next
mock response.

That is unsafe for this emulator network queue: `scheduler_queue_net_event()`
stores the VM response pointer in the queued task, and the callback may run
after another send has already built a later response. Freeing the previous
pointer at build time can leave the older queued callback reading freed or reused
memory, which can surface as packet parser failures or wrong backpack data.

The repair removes that eager free. Response buffers remain valid for queued
network data events.
