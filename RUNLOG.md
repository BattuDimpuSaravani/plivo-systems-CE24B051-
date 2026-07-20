# RUNLOG

Format per run: profile, delay_ms, miss%, overhead, what changed and why.
Fill in real numbers from `python3 run.py --profile ... --delay_ms ...` output.
Replace every `TODO` below with your actual measured values.

## Baseline (unmodified naive sender/receiver, for reference)
- profile=A delay_ms=40 miss%=TODO overhead=TODO — result: INVALID (no loss handling at all)

## Iteration log

### Run 1 — get VALID at a generous delay
- profile=A delay_ms=120 miss%=TODO overhead=TODO
- change: switched to custom sender/receiver — sequence numbers, piggyback
  redundancy (each packet carries current frame + copy of previous frame,
  skipped every 16th frame to hold bandwidth under 2.0x), ring-buffer jitter
  buffer on the receiver, playout scheduled on wall clock at T0+delay_ms+i*20ms.
- why: isolated single-frame losses are now recoverable with zero added
  latency (no retransmit round trip needed).
- result: TODO (VALID / INVALID)

### Run 2 — shrink delay
- profile=A delay_ms=TODO miss%=TODO overhead=TODO
- change: lowered delay_ms from 120 toward TODO
- why: find the lowest delay where miss% stays <=1%
- result: TODO

### Run 3
- profile=A delay_ms=TODO miss%=TODO overhead=TODO
- change: TODO
- why: TODO
- result: TODO

### Run 4 — stress on profile B
- profile=B delay_ms=TODO miss%=TODO overhead=TODO
- change: TODO (e.g. adjusted REDUNDANCY_SKIP_MOD from 16 to TODO because
  profile B showed burst losses that a single redundant copy couldn't cover)
- why: TODO
- result: TODO

### Run 5 — final lock
- profile=A delay_ms=TODO miss%=TODO overhead=TODO — VALID
- profile=B delay_ms=TODO miss%=TODO overhead=TODO — VALID
- Final chosen delay_ms: TODO (lowest value valid on both profiles across
  repeated runs)

## Notes on methodology
- Each profile/delay_ms combination was run TODO times to check for
  run-to-run variance (flaky networks are noisy — a single passing run isn't
  proof).
- Overhead was computed as total bytes through the relay (both directions)
  divided by the raw stream size (160 bytes * number of frames).
