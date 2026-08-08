# RVWheel built-in device profiles

Each `*.json` file in this directory is a built-in `DeviceProfile` (schema
version 1) loaded automatically by `rvwheel_profiles`. A user profile with
the same `profileId`, placed under
`%LOCALAPPDATA%\RVWheel\profiles\`, overrides the built-in one — see
`rvwheel::profiles::ProfileRepository::MergeProfiles`.

## Schema

```json
{
  "schemaVersion": 1,
  "profileId": "manufacturer-model-variant-backend",
  "displayName": "Human-readable name shown in --list",
  "match": {
    "backend": "DirectInput",
    "vendorId": "0xVVVV",
    "productId": "0xPPPP"
  },
  "axes": {
    "steering": { "source": "X", "direction": "normal", "center": "rangeMidpoint" },
    "throttle": { "source": "Y", "direction": "inverted" },
    "brake":    { "source": "Rz", "direction": "inverted" },
    "clutch":   { "source": "Slider0", "direction": "inverted" }
  },
  "readiness": {
    "minimumWarmupMilliseconds": 2200,
    "stableSampleMilliseconds": 250,
    "maximumWaitMilliseconds": 5000,
    "stabilityTolerance": 0.01
  },
  "sanityChecks": {
    "expectedButtonCount": 25,
    "expectedPovCount": 1
  }
}
```

- `match.vendorId`/`match.productId` must both be present (an exact-match
  profile for one specific device) or both be absent (a generic profile
  for the whole backend). `match` alone, without a display-name check, is
  the only identity RVWheel ever uses — a device's display name is never
  matching criteria.
- `axes.*.source` must be one of the tokens `rvwheel::dal::AxisSource`
  defines: `X`, `Y`, `Z`, `Rx`, `Ry`, `Rz`, `Slider0`, `Slider1`. No two
  roles may reference the same source.
- `axes.*.direction` is `"normal"` or `"inverted"` — never a boolean. The
  DAL always queries the device's actual raw range at runtime
  (`DIPROPRANGE` for DirectInput); this field only says which end of that
  runtime-queried range is "released"/"steering -1" vs
  "pressed"/"steering +1". Never hardcode a raw range in a profile.
- `readiness` times are milliseconds, each between 0 and 60000. If
  `readiness` is omitted entirely, a conservative generic default is used
  instead of zero warmup.
- `sanityChecks` is informational only. A mismatch is reported to the
  user; it never blocks or weakens a match.

## Adding a new profile: evidence requirement

**Do not add a profile for a device you have not captured real data
from.** Every entry in this directory must be backed by an actual
`rvwheel_device_probe --calibrate` session (or an equivalent manual
`--monitor`/`--capture` investigation) against real hardware, documented
similarly to `docs/hardware/G923_DIRECTINPUT_CAPTURE.md`:

1. Run `rvwheel_device_probe --list` and record the exact backend, VID,
   PID, button count, and POV count reported.
2. Run `rvwheel_device_probe --calibrate` (or manually drive `--monitor`
   through center/left/right steering and released/pressed pedals) and
   record, for each axis: which `AxisSource` moved, and whether released
   was closer to the raw minimum or maximum.
3. Note the startup transient duration you actually observed, if any, and
   set `minimumWarmupMilliseconds` with a margin above it — do not copy
   the G923's `2200` value onto a different model.
4. Submit the generated profile alongside a short capture summary. A
   profile with plausible-looking values but no capture behind it will be
   rejected — this project would rather have zero profiles for a model
   than a wrong one silently misreading someone's pedals.

`logitech-g923-ps-pc-directinput.json` is the only profile currently
shipped, verified against a real Logitech G HUB G923 Racing Wheel (USB,
DirectInput, VID `046D` PID `C266`) — see
`docs/hardware/G923_DIRECTINPUT_CAPTURE.md` for the full capture this
profile's `axes`/`readiness` values were derived from.
