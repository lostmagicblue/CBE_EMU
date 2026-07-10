# 2026-07-10 Nearby Player Movement Sync

phase: nearby-player scene movement and idle-observer refresh
status: validated

## Request

```text
movement upload:
  wt_kind: 2
  wt_subtype: 1
  objects: 1/2/1
  key_fields: moveinfo (raw/blob, 1..10 bytes)
  sample_len: 32
  packet_log: bin/logs/nearby_service_after2.log

idle observer transport poll:
  outer_magic: CBMS
  outer_flags: bit1 = scene-sync poll
  metadata: u32 clientId
  WT payload: none

clean client disconnect:
  outer_magic: CBMS
  outer_flags: bit2 = client disconnect
  metadata: u32 clientId
  WT payload: none
```

The scene tick does not upload a steady idle heartbeat. Once a movement batch
has started, it appends one byte per frame until the batch reaches ten bytes:

- `1..4` are movement directions;
- `0` is an idle frame inside the same ten-frame timeline.

Therefore `2,2,2,0,0,0,0,0,0,0` is not an opaque packet. It represents three
right steps followed by seven idle frames.

## Response

```text
baseline push:
  WT object 1/2/10 { othernum, otherinfo }
  WT object 1/2/2  { moveinfo = x, y, actorId, rawLen, raw }

movement delta push:
  WT object 1/2/2  { moveinfo = startX, startY, actorId, rawLen, rawTimeline }

peer removal push:
  WT object 1/2/6  { actorid }

queued_event: normal event 7 through scheduler_queue_net_event
```

The `1/2/2` outer x/y fields are the timeline start, not its end. The client
first writes those fields to `ActorSceneNode+24/+26`, resets the raw-blob read
cursor, and then advances from that point one byte per rendered frame. The
server keeps the authoritative end position separately for later baselines.

## IDA Evidence

```text
binary: 江湖OL.CBE

function: scene_runtime_tick (0x01014EE0)
dispatch_case: outgoing 1/2/1 moveinfo
parser_reads: writes one direction/zero frame into the local buffer and sends
              after the buffer reaches ten bytes

function: net_handle_actor_move_info (0x01012ADC), case 2
dispatch_case: incoming 1/2/2 moveinfo
parser_reads: i16 x, i16 y, u32 actorId, len16 raw blob

function: scene_node_update_move_blob (0x01012A76)
parser_reads: copies raw blob to node+136, clears node+317, writes node+318,
              then writes outer x/y to node+24/+26

function: ProcessSceneAutoAction (0x01045428)
parser_reads: consumes one byte per frame; 1=up, 2=right, 3=down, 4=left;
              each direction advances four pixels

function: ParseSceneOtherNodeData (0x01012958)
dispatch_case: incoming 1/2/10 otherinfo
parser_reads: creates or refreshes the remote ActorSceneNode
```

## Runtime Evidence

Before the fix, the dual-client trace showed:

```text
moveinfo_store ... kind=dir-queue len=10 pos=(284,312)
moveinfo_store ... kind=opaque-small len=10 pos=(284,312)
```

The second batch contained idle-frame zeroes and did not advance the server
position. The idle client also sent no WT request after entering the scene, so
it had no response path on which to receive the moving client's update.

After the fix, two isolated guest clients produced:

```text
moveinfo_store ... kind=timeline len=10 pos=(296,312)
mock_actor_moveinfo_ack ... steps=2,2,2,0,0,0,0,0,0,0 pos=(296,312)
scene_sync_poll baseline ... roles=1 moveinfo=1 resp=140

moveinfo_store ... kind=timeline len=10 pos=(296,316)
mock_actor_moveinfo_ack ... steps=3,0,0,0,0,0,0,0,0,0 pos=(296,316)
scene_sync_poll delta ... objects=1 resp=48
```

The idle observer's client trace then showed exactly one delta parse:

```text
net_queue_scene_sync_poll ... event=7 resp=48
scene_actor_parser case2_moveinfo
scene_actor_update_move actor=1779389638 len=10 gridX=296 raw0=00000003
```

Subsequent polls did not emit another delta for the same movement serial. This
validates both idle-observer delivery and non-replay behavior. Autotest
screenshots in `tmp/nearby-sync-after-b/autotest/screens` also show both roles
present in the observer scene.

### Initial online and disconnect lifecycle

The initial-map path has no movement upload, so movement cannot be the event
that makes a role visible. The authoritative online transition is now the
post-enter scene task-subset request. If there is no deferred scene change,
the service marks that client session ready from the active role DB's own
`scene/x/y` before appending nearby objects. It does not infer a coordinate
from another session or from a synthetic movement queue.

Two clients entering the map without any movement produced:

```text
session_online client=84f129c5 account=guest00021 ...
  scene=c00蓬莱仙岛_01 pos=(296,316) reason=scene-task-subset-followup
session_online client=1da36471 account=guest00022 ...
  scene=c00蓬莱仙岛_01 pos=(272,232) reason=scene-target-complete
scene_sync_poll baseline observer=84f129c5 ... roles=1 moveinfo=1 resp=202
scene_sync_poll baseline observer=1da36471 ... roles=1 moveinfo=1 resp=204
```

Normal emulator shutdown sends the outer bit-2 disconnect frame after the
host loop exits. The service immediately removes the source from the online
set; the next observer poll emits the existing client-native removal object:

```text
session_offline client=84f129c5 ... reason=explicit-disconnect
scene_sync_poll delta observer=1da36471 ... objects=1 resp=27
client B: net_queue_scene_sync_poll ... event=7 resp=27
```

Forced termination cannot send that frame. A 300 scheduler-tick (30 second)
presence timeout is the fallback. A forced-stop run produced:

```text
session_offline client=c29fbdda ... reason=heartbeat-timeout
scene_sync_poll delta observer=4019b6b6 ... objects=1 resp=27
client B: net_queue_scene_sync_poll ... event=7 resp=27
```

The clean-disconnect run is captured in
`tmp/nearby-online-offline-test2`; the forced-timeout observer trace is in
`tmp/nearby-timeout-test`.

## Implementation

- Accept movement timelines containing bytes `0..4`, with at least one real
  direction, instead of classifying every zero-containing batch as opaque.
- Advance server position through only the nonzero direction semantics while
  preserving all ten bytes for remote animation timing.
- Store one source movement serial and one delivery cursor per observer/source
  pair. A movement is delivered once to every observer, not once globally.
- Add an outer mock-service scene-sync poll. It returns no packet when nothing
  changed, a baseline for a newly visible peer, a `1/2/2` delta for a new
  movement serial, or `1/2/6` when a previously visible peer leaves.
- Queue poll responses through the existing network callback as event `7`.
- Treat the post-enter scene task-subset request as the initial online trigger,
  using the selected role's persisted scene and position.
- Send a client-level disconnect control frame on a clean host-loop exit and
  expire abruptly lost sessions after 30 seconds. Both paths converge on the
  same `1/2/6 { actorid }` observer update.
- Mix the emulator process id into `clientId`; simultaneous processes started
  in the same second must not collapse into one account-bound session.

## Negative Evidence

- Replaying the last raw queue on every nearby response resets node+317 and
  restarts the animation.
- Clearing one global pending queue after the first response loses the same
  movement for all other observers.
- Treating a zero-containing ten-byte upload as opaque loses real direction
  bytes and leaves the authoritative remote position behind.
- Adding more endpoint guesses cannot update an idle observer because the old
  request/response transport had no delivery opportunity at all.

## Latency Reduction

The client-side producer remains the hard lower bound: `scene_runtime_tick`
collects ten 100 ms frames before uploading `1/2/1 moveinfo`, so the service
cannot broadcast the first direction before that completed timeline arrives.
No client patch or host-side state read was added to bypass this contract.

The controllable downlink path was shortened as follows:

- scene-sync polling now defaults to one scheduler frame (100 ms), down from
  five frames (500 ms). `CBE_SCENE_SYNC_POLL_TICKS` can override the cadence;
- the narrow single-object `1/2/1 { moveinfo }` detector is dispatched before
  unrelated shop/resource/scene probes;
- subtype-1 upload responses now return only the 11-byte empty ACK. Nearby
  `otherinfo/moveinfo` is delivered by the independent scene-sync poll instead
  of being rebuilt redundantly for the moving client;
- steady movement updates use the exact timeline-derived position and do not
  run scene-landing/SCE portal normalization. The role position is kept dirty
  in the account context and flushed on clean disconnect;
- downlink catch-up drops historical zero/idle bytes and retains at most the
  last four real directions. The outer x/y is advanced through the skipped
  prefix, so `outer start + retained queue` still lands on the exact server
  endpoint. `CBE_SCENE_SYNC_MAX_CATCHUP_STEPS` controls this bound.

Runtime evidence from `tmp/nearby-latency-catchup`:

```text
scene_sync_poll cadence ticks=1 interval_ms=100
mock_actor_moveinfo_ack ... steps=2,2,2,0,0,0,0,0,0,0 ... resp=11
scene_move_catchup ... frames=10 directions=3 sent=3
scene_sync_poll delta ... movement=1 ... resp=41
client B: net_queue_scene_sync_poll ... event=7 resp=41
```

The observed three-step sample now consumes only three render frames (300 ms)
instead of rearming a ten-frame queue. For a continuous ten-direction upload,
the default catch-up bound sends only the last four steps from the exactly
advanced prefix coordinate, capping post-delivery animation lag at 400 ms.

## Unknowns

- Small upload blobs containing values above `4` remain classified as opaque
  and are not replayed into `1/2/2`; no matching client producer contract has
  been recovered for those bytes.
