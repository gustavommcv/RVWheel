# RVT1 telemetry transport: hardening validation (in-game, no FFB)

This records real, authorized validation of the `RVT1` telemetry transport
hardening (`tools/device_probe/VehicleTelemetryTransport.hpp/.cpp`,
`--telemetry-monitor`'s instrumentation) described in
[ARCHITECTURE.md](../ARCHITECTURE.md#vehicle-telemetry-protocol-rvt1). As
with [AVS_TELEMETRY_DISCOVERY.md](AVS_TELEMETRY_DISCOVERY.md), this
document records **only what was observed** — measured values and log
output, not attributed causes beyond what the logs themselves show.

Force feedback was never enabled at any point in this session: the
launcher was run with no arguments, `RVWheelDiscovery` stayed disabled in
`ue4ss/Mods/mods.txt`, and `--telemetry-monitor` never enumerates or
acquires a wheel device and never calls `ApplyForceFeedback`/
`StopForceFeedback`.

## Pre-game: zero-fresh-frames proof against a leftover file

Before opening the game, `--telemetry-monitor --duration 3` was run
against the real `vehicle-telemetry.txt` left over from a previous
session (last sequence 34859, `valid=1 local=1`). Result:

```
Telemetry monitor finished: 180 polls, 0 with a fresh frame.
Instrumentation (measured, not assumed from the Lua side's configured cap):
  new sequences observed: 1
  repeated polls:         179
  missing/invalid reads:  0
  new-sequence interval:  not enough new sequences observed to measure
```

The leftover sequence was observed exactly once (consumed as the
baseline) and never reported as fresh across the remaining 179 polls —
confirming the baseline-only-first-observation rule against a real
leftover file, not just synthetic unit tests.

## In-game runs

Three `--telemetry-monitor --duration 45 --rate 60` runs were made with
the launcher running normally (no `--enable-force-feedback`) and the
player in the vehicle. The first two runs did not follow the intended
driving procedure; each still confirmed a distinct property, so all
three are recorded rather than discarded.

### Run 1 — vehicle occupied but stationary

The player stayed in the vehicle without driving. Result:

```
Telemetry monitor finished: 2686 polls, 2612 with a fresh frame.
Instrumentation (measured, not assumed from the Lua side's configured cap):
  new sequences observed: 779
  repeated polls:         1906
  missing/invalid reads:  1
  new-sequence interval:  min=30.2698ms avg=57.7947ms max=1685.19ms (n=778)
  effective rate:         17.3026 Hz (measured across the whole run, including any idle/stale periods)
  age at last 'unavailable' transition: 501ms
```

`speed`/`forward`/`lateral` stayed below 0.09 m/s for the entire run
(consistent with "occupied, not driving," not a transport defect).
A single mid-run publish gap exceeded the 500 ms staleness threshold —
the monitor printed one `telemetry unavailable (no fresh frame; last
usable sample was 501ms old)` line, then a new sequence arrived and
freshness recovered immediately (no further `valid=0`/`local=0`
sequence was ever observed in this run). This is a real-world
confirmation that a stale tracker recovers as soon as a new valid/local
sequence appears, distinct from — but related to — the sequence-restart
recovery case covered by the unit tests.

### Run 2 — driving, but still in the vehicle when the window closed

The player drove for the full 45 s without exiting the vehicle. Result:

```
Telemetry monitor finished: 2683 polls, 2682 with a fresh frame.
Instrumentation (measured, not assumed from the Lua side's configured cap):
  new sequences observed: 796
  repeated polls:         1883
  missing/invalid reads:  4
  new-sequence interval:  min=29.8635ms avg=56.5401ms max=187.136ms (n=795)
  effective rate:         17.6866 Hz (measured across the whole run, including any idle/stale periods)
```

No `telemetry unavailable` transition occurred (expected — the vehicle
was never exited). `speed` rose smoothly to 6.75 m/s with `forward`
tracking it closely and `lateral` swinging between positive and
negative during turns, e.g. sample `speed=6.7111 forward=6.6924
lateral=-0.4981` — `sqrt(6.6924² + 0.4981²) ≈ 6.703`, matching `speed`
within the expected rounding from independent per-field sampling. This
run confirms `speed`/`forward`/`lateral` stay coherent during real
driving; it does not exercise the unavailable-transition path.

### Run 3 — full procedure: drive, then exit the vehicle with margin

The player drove (accelerating to 7.56 m/s, braking, two direction
reversals visible in `lateral`'s sign) and exited the vehicle with
several seconds of margin before the 45 s window closed. Result:

```
Telemetry monitor finished: 2695 polls, 882 with a fresh frame.
Instrumentation (measured, not assumed from the Lua side's configured cap):
  new sequences observed: 252
  repeated polls:         2442
  missing/invalid reads:  1
  new-sequence interval:  min=30.4372ms avg=57.1801ms max=107.805ms (n=251)
  effective rate:         17.4886 Hz (measured across the whole run, including any idle/stale periods)
  age at last 'unavailable' transition: 500ms
```

The monitor printed exactly one `telemetry unavailable (no fresh frame;
last usable sample was 500ms old)` line, immediately after the last
driving sample and with no further fresh sample for the remainder of
the run (the vehicle was not re-entered). Representative samples:

| seq | speed (m/s) | forward (m/s) | lateral (m/s) |
|---|---|---|---|
| 10192 | 6.9074 | 6.8624 | -0.7131 |
| 10248 | 7.1985 | 7.1438 | 0.5408 |
| 10190 | 6.7369 | 6.6806 | -0.5769 |
| 10267 | 5.7889 | 5.7832 | 0.1852 |

`lateral` changes sign multiple times across the run (e.g. -0.75 →
+0.54 → -0.08 → +0.31 → -0.08), consistent with curves in both
directions. `yaw` was `unavailable` on every one of the 252 new
sequences across all three runs — consistent with the already-recorded
`GetActorRotation()` outcome in
[AVS_TELEMETRY_DISCOVERY.md](AVS_TELEMETRY_DISCOVERY.md); this session
does not add a new finding about yaw reachability.

## Checklist against the validation criteria

| Criterion | Result |
|---|---|
| Effective rate measured, not assumed | 17.3–17.7 Hz across all three runs (measured; the Lua side's cap is 20 Hz) |
| Mean new-sequence interval | 56.5–57.8 ms average, with jitter (min ~30 ms, max varies by run) |
| speed/forward/lateral coherent during driving | Confirmed in runs 2 and 3 (`forward` ≈ `speed` when near-straight; `sqrt(forward² + lateral²) ≈ speed`) |
| lateral changes sign across opposite curves | Confirmed in runs 2 and 3 |
| First observation is baseline-only, never fresh | Confirmed structurally in all three in-game runs and in the pre-game leftover-file proof |
| "unavailable" age at a stale transition | 501 ms (run 1, mid-drive gap) and 500 ms (run 3, vehicle exit) — both within the expected ~500–520 ms band for a 500 ms staleness threshold |
| Zero crash or Lua error | Confirmed — no error/exception/crash/stack-traceback line in `ue4ss/UE4SS.log` across the session |
| Force feedback never armed | Confirmed — no "Force feedback: armed" (or any force-feedback line at all) in `bridge.log` for this session's bridge instance |
| Steering/pedals/gearbox kept working | Confirmed by the operator after run 3 |
| Supervised, clean shutdown | Confirmed — `bridge.log` recorded "Bridge stopped after 49690 polls (124 publish failures); shutdown confirmed safe" when the game was closed |

## Raw data

The full per-poll `--telemetry-monitor` output for all three in-game runs
and the pre-game proof run were captured in the session transcript at
the time of capture; this document is a curated summary, not a
replacement for that raw output. The corresponding `ue4ss/UE4SS.log` and
`%LOCALAPPDATA%\RVWheel\logs\bridge.log` cover the same time window.
