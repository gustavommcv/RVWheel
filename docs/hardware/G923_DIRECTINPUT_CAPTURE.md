# Logitech G923 — DirectInput Hardware Baseline

## Environment

- Device: Logitech G HUB G923 Racing Wheel for PlayStation 4 and PC (USB)
- Backend: DirectInput
- VID/PID: `046D:C266`
- Device ID observed in this session: `0xC1CEC0BBE4BA0B66`
- Driver software: Logitech G HUB
- Probe: `rvwheel_device_probe` Release build
- Force feedback: not exercised

## Capture

- File: `g923-capture.jsonl` (local hardware artifact; not copied into this document)
- Duration: 29.933 seconds
- Samples: 1,679
- Failed polls: 0
- Reported dropped frames: 5
- Parse failures: 0
- Every sample was connected, valid, and returned `pollStatus=Ok`

## Observations

### Steering

- Minimum: `-1.0`
- Center after the protocol: approximately `+0.000854`
- Maximum: `+1.0`
- Conclusion: full range, direction, centering, and Layer 2 normalization are correct for this device.

### Pedals

All three pedal channels are physically present and independent.

| Channel | Released after device settled | Fully pressed | Current semantic direction |
|---|---:|---:|---|
| Throttle | `1.0` | `0.0` | inverted |
| Brake | `1.0` | `0.0` | inverted |
| Clutch | `1.0` | `0.0` | inverted |

Movement windows observed in the manual protocol:

- throttle reached `0.0` around 9.256 s and returned to `1.0` around 10.139 s;
- brake reached `0.0` around 11.822 s and returned to approximately `1.0` around 12.6 s;
- clutch reached `0.0` around 13.638 s and returned to `1.0` around 14.439 s.

The DAL contract requires pedals to report `0.0` when released and `1.0` when fully pressed. The current DirectInput calibration assumes `lMin=released` and `lMax=pressed`; the G923 reports the opposite. Calibration endpoints must therefore be swapped through a device/profile policy rather than hardcoded in generic normalization math.

### Startup transient

For approximately the first 2.05 seconds, all three pedal channels simultaneously reported `0.499992`. They then moved together to their real released value (`1.0`) without a poll failure.

This is a device/driver acquisition transient, not the stable resting calibration. A future profile/readiness policy should prevent the transient midpoint from being consumed as a real half-pedal input after startup or hot-plug.

### Buttons and POV

- Device reports 25 buttons and one POV.
- The protocol observed button indices: `0, 1, 2, 3, 4, 6, 7, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22`.
- POV observations covered centered plus all eight directions.
- The Logitech Driving Force shifter connected through the G923 base was later
  captured systematically: neutral reports no button, gates 1–6 report buttons
  `12–17`, and reverse reports button `18`.

## Architectural conclusions

1. DirectInput enumeration, connection state, polling, steering, buttons, POV, and all three pedal channels work on the G923.
2. Generic `AxisNormalizer` math should not be changed; it already supports inverted endpoints.
3. The DirectInput backend needs profile-provided pedal endpoint direction for `046D:C266`.
4. Device-specific values must live in a configurable profile, not be scattered as VID/PID conditionals through backend code.
5. The startup/hot-plug readiness policy must suppress or explicitly mark the approximately two-second midpoint transient.
6. Force feedback remains unvalidated and must be tested in a separate, safety-bounded task.

## Addendum: re-test after implementing the profile system (same session date)

After building `logitech-g923-ps-pc-directinput.json` (steering normal; throttle/brake/clutch
inverted, per the observations above) and re-testing against the same physical G923 with the
profile applied and matched (confirmed via `--list`: `profile logitech-g923-ps-pc-directinput
origin=BuiltInProfile`, exact VID/PID match):

- Steering: unaffected, confirmed correct (`+0.000` at rest).
- **Pedals: `throttle`/`brake`/`clutch` now read `~0.500` at rest, sustained for at least 10
  real seconds with `readiness=Ready` -- not the `~0.0` the `inverted` direction was expected to
  produce.** `--calibrate`'s raw-axis printout confirms this is not a display bug: the actual raw
  values for `Y`/`Rz`/`Slider0` (and `X`) all read `32767` -- the midpoint of the queried
  `0..65535` range -- at rest, in this session.

This is mathematically why `direction` alone did not help: `AxisNormalizer::NormalizePedal`
(verified independently by unit tests) produces `~0.5` for a raw value at the midpoint of the
calibration range *regardless* of whether `rawAtReleased`/`rawAtPressed` are swapped, since a
midpoint is equidistant from both ends either way. The original capture's conclusion --
"released sits at `rawMax`, pressed at `rawMin`" -- does not match what this re-test observed;
either the device/driver's reported rest position is not perfectly stable across sessions (e.g.
G HUB software recalibrating/re-centering the pedal range), or the original ~2.05s transient
window was measured too narrowly and a similar-looking plateau can, in some sessions, persist
well past it. Both are real possibilities this session's data cannot distinguish between.

**This means `logitech-g923-ps-pc-directinput.json` should be treated as unverified for pedal
direction until a fresh, dedicated capture confirms stable rest/pressed raw values (ideally
across multiple app launches and G HUB states) before being trusted for gameplay.** No profile
value was changed to paper over this discrepancy; the `direction`/readiness *code* is correct
(covered by unit tests) and applies exactly whatever a profile specifies -- what's in question is
whether this specific profile's `inverted` values still match this specific unit's current raw
behavior.

## Addendum: guided calibration and activation gate validation

A later hardware-guided session continuously polled raw axes, explicitly exercised every physical
control once before taking the baseline, and captured stable 500 ms medians. It confirmed:

| Role | Source | Released/center raw | Fully actuated raw | Direction |
|---|---|---:|---:|---|
| Steering | `X` | approximately `32767` | left `0`, right `65535` | normal |
| Throttle | `Y` | `65535` | `0` | inverted |
| Brake | `Rz` | `65535` | `0` | inverted |
| Clutch | `Slider0` | `65535` | `0` | inverted |

The generated user profile reloaded successfully as
`logitech-g923-ps-pc-directinput` and therefore overrides the built-in profile without creating an
ambiguous second exact match.

A fresh probe process then exposed the remaining driver behavior precisely: before any physical
movement, all three normalized pedals stayed at approximately `0.500008`, even beyond the previous
fixed warmup. This is a stable placeholder, so elapsed time plus stability alone cannot distinguish
it from real input.

The verified G923 profile now enables `requireAxisActivation` with a normalized threshold of `0.05`.
The readiness state remains `AwaitingActivation` and `WheelState.valid=false` until any mapped axis
moves meaningfully; only then does the ordinary warmup/stability state machine begin. A real idle
capture after this change produced 300/300 `AwaitingActivation` samples, zero valid samples, zero
poll failures, and zero dropped frames. Thus the placeholder is no longer publishable as gameplay
input.

That final live monitor validation subsequently passed. After physical activation and returning all
controls to rest, a 15-second Release run reported:

- `readiness=Ready`, `connected=true`, and `valid=true`;
- steering approximately `-0.003` at physical center;
- throttle, brake, and clutch approximately `0.000` when released;
- 853 polls at an observed 56.9 Hz;
- zero failed polls and two dropped frames;
- final poll status `Ok`.

This completes the real-hardware validation of device discovery, profile selection, activation
gating, readiness, axis normalization, continuous polling, all four driving axes, and the attached
H-pattern shifter for this G923. The same setup subsequently passed a playable in-game test for
steering, throttle, brake, clutch-gated forward gears 1–5, neutral, and reverse. Force feedback
remains a separate, unvalidated milestone.

### Evidence files for this addendum

Two local, gitignored (`*.jsonl`) capture files back the two measurements above and are kept
alongside the repository for anyone re-running this validation:

- `g923-activation-gate.jsonl` — 300 samples over ~5 s. Every sample reports
  `readinessState=AwaitingActivation`, `valid=false`, and the untouched placeholder
  `throttle=brake=clutch=0.500008`. This is the raw data behind the "300/300 AwaitingActivation
  samples, zero valid samples" claim above.
- `g923-post-calibration.jsonl` — 300 samples over ~5 s captured immediately after calibration,
  pedals left untouched. It shows the readiness state machine advancing on its own timing:
  `WarmingUp` from `t=0ms`, `Stabilizing` from `t=3000ms`, and `Ready` (with `valid` flipping to
  `true`) from `t=3501ms`. Because the pedals were not pressed during this specific capture, the
  values stay at the `0.500008` placeholder throughout; it demonstrates state-machine timing only,
  not pedal travel. Real pedal travel to `~0.0` at rest is documented separately in the final
  15-second Release run described above.
