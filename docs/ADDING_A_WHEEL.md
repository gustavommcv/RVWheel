# Adding support for a new wheel

RVWheel's Device Abstraction Layer (DAL) already speaks generic DirectInput:
axes, buttons, and POVs. Adding a new wheel, pedal set, or shifter is
normally a **data change** (a new JSON profile, optionally a small Lua table
entry for a non-standard control layout) — not new C++. This guide walks
through producing that data honestly, from a real device.

> [!IMPORTANT]
> Do not write a profile from a spec sheet, a forum post, or another
> project's VID/PID table. Every profile in this repository must be backed
> by an actual capture against the physical device. A plausible-looking
> profile with no capture behind it is worse than no profile: it will
> silently misread someone's pedals. See the [evidence requirement](../configs/default_profiles/README.md#adding-a-new-profile-evidence-requirement)
> for the full policy.

## 1. Identify the device

With the wheel connected and powered on:

```powershell
.\rvwheel_device_probe.exe --list
```

Record the exact backend (`DirectInput`), vendor ID, product ID, button
count, and POV count reported. If a profile already claims your exact
VID/PID, check whether it was actually verified against your unit — the
same model can differ across regional SKUs (see the G923 PS/PC-vs-Xbox
addendum in
[G923_DIRECTINPUT_CAPTURE.md](hardware/G923_DIRECTINPUT_CAPTURE.md)).

## 2. Run the guided calibration wizard

```powershell
.\rvwheel_device_probe.exe --calibrate
```

The wizard walks through baseline, steering center/left/right, and each
pedal released/pressed, using continuous sampling
(`StableRawAxisSampler`) rather than a single snapshot — it waits for a
stable 500 ms window before accepting a value, specifically because some
wheels (the G923 included) report a transient placeholder value for up to a
few seconds after power-up. See
[docs/prompts/005-calibration-continuous-sampling.md](prompts/005-calibration-continuous-sampling.md)
for the full root-cause writeup if you are curious why this matters.

At the end, the wizard writes a candidate profile JSON under
`%LOCALAPPDATA%\RVWheel\profiles\`. Review it before trusting it:

- Does `axes.*.source` match a physically distinct control for each role
  (steering/throttle/brake/clutch)? The wizard reports `Ambiguous` instead of
  guessing if two roles moved together.
- Does `axes.*.direction` match what you observed (released vs. pressed
  closer to the raw minimum or maximum)?

If the wizard reports `NoMovement`, `Inconsistent`, or `Ambiguous` for a
role, do not hand-edit the JSON to "fix" it — that role's physical control
was not clearly identified, and shipping a guessed value defeats the point
of the evidence requirement. Re-run the step, or fall back to manual
`--monitor` investigation (below) to understand why.

## 3. Cross-check with manual monitoring (optional but recommended)

```powershell
.\rvwheel_device_probe.exe --monitor --duration 30 --rate 60
```

Exercise every control (full steering lock-to-lock, each pedal released and
fully pressed, every shifter gate, every button) while watching the raw
values. This is how the initial G923 startup-transient and pedal-inversion
findings were discovered — by watching real numbers, not by assuming the
wizard's output is correct on the first try.

## 4. Test the profile against real gameplay

Copy the wizard's output profile into `configs/default_profiles/` (only once
you are proposing it as a built-in profile — during development it can stay
in the user profiles directory, which already takes precedence). Confirm:

```powershell
.\rvwheel_device_probe.exe --list
```

reports `profile <your-id> origin=UserProfile` (or `BuiltInProfile` once
moved) with an exact VID/PID match, not `AmbiguousMatch` or
`ProvisionalFallback`.

Then run a real drive with `rvwheel_launcher.exe` and confirm steering,
throttle, and brake respond correctly in-game before considering the profile
validated. Report exactly what you tested — "steering and pedals validated
in-game, force feedback not tested" is the expected level of honesty, not an
unqualified "works."

## 5. Non-standard controls (H-pattern shifters, extra buttons)

RVWheel's native bridge (`RVW2` protocol) already transmits all 128
DirectInput button bits generically — you do not need to touch the DAL,
profile schema, or protocol to support a new shifter. Device-specific button
meaning is resolved in
[mods/RVWheel/Scripts/main.lua](../mods/RVWheel/Scripts/main.lua), in the
`H_PATTERN_SHIFTERS` table keyed by `"VID:PID"`:

```lua
H_PATTERN_SHIFTERS["046D:C266"] = {
    [12] = 1, [13] = 2, [14] = 3, [15] = 4, [16] = 5, [17] = 6,
    [18] = -1, -- reverse
}
```

Use `--monitor` or `--list` (with the shifter engaged one gate at a time) to
find which button index each gate reports, then add one table entry for your
device's VID:PID. This is the entire integration surface for a new H-pattern
shifter — no other file needs to change.

## Why activation-gating exists

Some devices (the G923 is the documented case) report a stable-looking
midpoint value for a period after power-up, before any control has actually
been touched — the value is stable enough to pass a naive "has it settled
down" readiness check, but it is not real input. A profile can set
`readiness.requireAxisActivation: true` with an
`readiness.activationThreshold` (a normalized deviation from center/rest);
when set, `WheelState.valid` stays `false` and the readiness state reports
`AwaitingActivation` until at least one mapped axis moves past that
threshold. This is why RVWheel sometimes asks you to "move the wheel once"
before input is applied — it is a deliberate safety gate, not a bug. See the
full measured writeup in
[G923_DIRECTINPUT_CAPTURE.md](hardware/G923_DIRECTINPUT_CAPTURE.md#addendum-guided-calibration-and-activation-gate-validation).

## Profile schema reference

The full JSON schema, field-by-field, lives in
[configs/default_profiles/README.md](../configs/default_profiles/README.md) —
this guide covers the workflow to produce correct values; that file is the
authoritative reference for what each field means.
