# Hangup Battle Request

## Runtime Signature

- Request WT: `2/10`
- Objects:
  - `1/2/10` with `Type = 2`, payload length `10`
  - empty `1/25/3`
- Observed failure before handling:
  - `unhandled wt=2/10 len=24 objects=1 first=1/2/10:10,1/25/3:0`

## IDA Evidence

- `JianghuOL.CBE:0x01015E14` (`HandleBattleEnterReq`) builds the outgoing `2/10` game event and writes `Type = 2`.
- `JianghuOL.CBE:0x01012E4D` dispatches business response subtype `25`.
- `JianghuOL.CBE:0x01010C7E` consumes response-side:
  - `25/11`: `result = 8`, then `info` string for the center banner state.
  - `25/12`: `result = 4`, then clears the banner state.
- No response-side `25/3` parser was found. The request marker must not be echoed.
- `mmBattle:0x66CC` consumes battle-start `4/5` with `side` and `battleinfo`.

## Server Contract

Success response:

- `1/2/10`: empty actor-other acknowledgement.
- `1/2/2`: moveinfo for the selected scene monster node.
- `1/4/5`: battle start, using the normal scene-monster battle start blob.
- Optional `1/4/11`: auto-battle UI flag, controlled by `CBE_HANGUP_BATTLE_AUTO_FLAG`.

Failure response:

- `1/2/10`: empty actor-other acknowledgement.
- `1/25/11`: `result = 8`, `info = "No hangup monster"` or `"Monster not ready"`.

## Data Source

- The server chooses the hangup monster from `automonster.dsh`.
- Load order:
  - `JHOnlineData/automonster.dsh`
  - `bin/JHOnlineData/automonster.dsh`
  - `web/fs/JHOnlineData/automonster.dsh`
- Matching uses loose scene-name comparison, then chooses one of the row monster ids.
- `CBE_HANGUP_BATTLE_ENEMY_ID` can force a monster id for debugging only.

## Implementation Notes

- Handler source: `src/mock-server.c`, `builtin-hangup-battle-start`.
- This is intentionally narrower than generic `2/10`: it requires the exact `Type = 2` plus empty `25/3` signature.
- Do not add JSON fallback or client-global reads for this feature. The server must answer from server-side scene and `automonster.dsh` data.
