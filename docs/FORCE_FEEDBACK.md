# Force Feedback

> [!IMPORTANT]
> Force feedback is **not validated on real hardware**. Five gated tests
> against a real G923 (2026-08-09) passed Step 4 (stop-only) cleanly. Every
> weak spring/damper attempt (Steps 6/7) reproduced the same
> `DIERR_NOTEXCLUSIVEACQUIRED` failure after ~2 seconds, with G HUB and this
> project's own input-reacquire logic both ruled out as the cause. A weak
> damper run additionally showed the wheel's own *pre-existing* ambient
> force feedback behavior (independent of any PC software) temporarily
> weaken while this project held exclusive access, then return once the
> failure hit -- current working hypothesis is a firmware/driver-level
> watchdog on the wheel itself, unconfirmed but reassuring if true (a
> hardware safety net on top of this project's software one). **No unsafe
> motion was reported in any of the five runs.** See the **Incident log** in
> [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md) and
> [docs/research/FORCE_FEEDBACK_FEASIBILITY.md](research/FORCE_FEEDBACK_FEASIBILITY.md)
> before doing anything else with real hardware. Do not enable
> `forceFeedback.enabled` in a profile expecting a finished feature.

## Status at a glance

| Piece | Status |
|---|---|
| DirectInput effect creation/update/stop (`CreateEffect`, `SetParameters`, `Stop`, `SendForceFeedbackCommand`) | Implemented; a real spring effect ran correctly for ~2s, then `SetParameters`/`Stop` reproducibly fail with `DIERR_NOTEXCLUSIVEACQUIRED` in all 3 runs, G HUB ruled out as the cause (see Incident log) |
| Capability detection (`DIDC_FORCEFEEDBACK`) | Implemented and confirmed working against a real G923 (read-only) |
| Exclusive FFB acquisition | Confirmed working on a real G923 without breaking input polling (hardware test Step 4) |
| `DISCL_EXCLUSIVE | DISCL_FOREGROUND` investigation | Unfocused test confirmed `DIERR_OTHERAPPHASPRIO`; focused no-effect retention passed for 5.01s/245 polls with successful state reads and STOPALL; real-effect confirmation remains pending |
| Safety controller (clamps, watchdog, slew rate, fault handling) | Implemented, unit-tested (37+ tests); a real gain-ramp overshoot bug was found and fixed after the first real activation |
| Profile-configured spring/damper source | Implemented, unit-tested; ran briefly on real hardware, see Incident log |
| Telemetry-derived self-aligning torque | **Not implemented** — see [Limitations](#limitations) |
| Simulation mode (`--ffb-simulate`) | Implemented, exercised against real hardware in read-only/simulated form |
| Real force applied to a device | **Attempted once (weak spring, gain 0.1), no unsafe motion reported, but not yet declared safe/working** — see Incident log |

## Architecture

```text
Vehicle telemetry (not yet wired from Lua -- see Limitations)
        |
        v
IForceFeedbackSource (e.g. SpringDamperSource, profile-configured)
        |
        v
ForceFeedbackMixer  (combines multiple sources' contributions)
        |
        v
ForceFeedbackSafetyController  (clamp, slew-limit, watchdog, fault/emergency stop)
        |
        v
IWheelDevice::ApplyForceFeedback / StopForceFeedback
        |
        v
DirectInputDevice  (CreateEffect / SetParameters / Stop / SendForceFeedbackCommand)
        |
        v
Wheel actuators
```

Source code: [`src/ForceFeedback/`](../src/ForceFeedback). Each layer is
independently unit-tested and never depends on the layer below skipping a
level (the mixer never touches DirectInput types; the DirectInput backend
never knows what a "source" is).

### Why the pieces are separated this way

- **`IForceFeedbackSource`** — one independent contributor (a centering
  spring, eventually a telemetry-derived self-aligning torque). A source
  never talks to a device and never enforces a safety limit; see
  [`IForceFeedbackSource.hpp`](../src/ForceFeedback/include/rvwheel/ffb/IForceFeedbackSource.hpp).
- **`ForceFeedbackMixer`** — combines every source's contribution into one
  command (constant force summed, spring/damper take the strongest request,
  gain takes the most conservative). See
  [`ForceFeedbackMixer.hpp`](../src/ForceFeedback/include/rvwheel/ffb/ForceFeedbackMixer.hpp)
  for the exact rule and why.
- **`ForceFeedbackSafetyController`** — the *only* place a value can be
  made smaller, slower, or stopped for safety reasons. See
  [Safety](#safety) below; this is the component every other layer must
  pass through, with no bypass.
- **`ForceFeedbackEngine`** — the only class that knows about all the
  others; ties a safety controller, a mixer, and a list of sources to a
  real `IWheelDevice`, and is what `--ffb-simulate` and (eventually) the
  bridge's live loop both drive.
- **`DirectInputDevice`** — owns the actual `IDirectInputEffect` objects;
  see [`DirectInputDevice.cpp`](../src/Devices/DirectInput/src/DirectInputDevice.cpp).
  Nothing above this layer ever sees a `DIEFFECT` or a `GUID_ConstantForce`.

## Telemetry flow

**Not implemented today.** The intended flow (Lua reads vehicle
properties, formats them into a small versioned text frame, the native
bridge parses it into `VehicleTelemetry`) mirrors the existing input path's
`RVW2` protocol, but in the opposite direction. It is not built because
[the feasibility research](research/FORCE_FEEDBACK_FEASIBILITY.md#4-ue4sslua-telemetry-access--confirmed-capability-unconfirmed-vehicle-data)
found that only steering and gear state are confirmed reachable from Lua
today — speed, suspension, and lateral slip (which a self-aligning-torque
effect actually needs) have never been queried against the real game. Until
that is resolved with a scoped, one-time reflection probe (not a per-tick
scan — this project has already been burned by heavy per-tick reflection
once), `VehicleTelemetry` fields simply stay absent, and any source that
needs them must report zero rather than guess.

The `SpringDamperSource` MVP effect needs no telemetry at all, which is
exactly why it is the first (and currently only) real effect: it works
today, with zero open questions.

## Effects implemented

- **Centering spring** (`springStrength`) and **damper**
  (`damperStrength`): DirectInput `DIEFT_CONDITION` effects
  (`GUID_Spring`/`GUID_Damper`). The device computes the position-dependent
  restoring force itself once the effect starts; RVWheel only sets the
  strength coefficient.
- **Constant force**: the channel exists end-to-end (mixer, safety
  controller, `DirectInputDevice::ApplyConstantForce`) but nothing currently
  produces a nonzero value for it — it is reserved for a future
  telemetry-derived source (self-aligning torque, collision impulses).

## Configuration (device profiles)

A profile's optional `forceFeedback` block (see
[`configs/default_profiles/README.md`](../configs/default_profiles/README.md)
for the full schema) controls every tunable — `enabled`, `masterGain`,
`springStrength`, `damperStrength`, `maxTorqueNormalized`, `slewRatePerSecond`,
`watchdogTimeoutMilliseconds`, etc. **Every field defaults to off/zero/
conservative**, and a profile written before this field existed loads
exactly as before (`forceFeedback` is `std::nullopt`, force feedback fully
inert) — see `ProfileLoader`'s tests for the exact backward-compatibility
guarantee.

`forceFeedback.enabled: true` is necessary but not sufficient: the runtime
must also call `ForceFeedbackEngine::Enable()` explicitly (nothing in the
launcher or bridge does this yet), and `hasForceFeedback` being reported by
a device is a capability, not proof every effect type actually works on
that unit — see [Limitations](#limitations).

## Safety

Enforced entirely by `ForceFeedbackSafetyController`
([source](../src/ForceFeedback/include/rvwheel/ffb/ForceFeedbackSafetyController.hpp)),
independent of any device/mixer/source:

- **Off by default, explicit activation required.** Starts `Disabled`;
  `Enable()` only takes effect if the profile's own `enabled` flag is also
  true. Neither alone is sufficient.
- **NaN/Inf sanitized to zero**, never passed through, never faulted on.
- **Absolute ceilings independent of any profile**: torque is capped at
  `kAbsoluteMaxTorqueNormalized` (0.6) and the slew rate at
  `kMaxSlewRatePerSecond` (20/s) regardless of what a profile requests.
- **Slew-rate limiting**: every applied value ramps toward its target; a
  step change (enable, disable, a new source contribution) is never a jump.
- **Watchdog**: if neither fresh telemetry nor a fresh `Update()` call
  arrives within the configured timeout (capped at 500 ms), the controller
  ramps to zero and calls `StopForceFeedback()` on its own, with no
  cooperation required from the caller.
- **Fault handling**: a backend error (`ApplyForceFeedback`/
  `StopForceFeedback` reporting `BackendError`) enters `Faulted` and stays
  there — output pinned at zero — until `ClearFault()` is called explicitly.
  A disconnect (`NotConnected`) is treated as recoverable instead: it forces
  an immediate stop but returns to `Armed`, so reconnecting resumes without
  manual intervention.
- **Emergency stop**: `EmergencyStop()` is instantaneous (no ramp) and
  reaches `Disabled` from *any* state, including `Faulted`.
- **Explicit states for diagnostics**: `Disabled`, `Armed`, `Active`,
  `Stopping`, `Faulted` — see `ForceFeedbackDiagnostics` for what is
  currently applied, how stale the telemetry/command is, and the last fault
  reason.
- **Device-level belt-and-suspenders**: `DirectInputDevice::StopForceFeedback`
  and its destructor also call `SendForceFeedbackCommand(DISFFC_STOPALL)`,
  which (when exclusive access is held) stops effects device-wide, not just
  ones this process instance created/tracked.

All of the above is proven by [`tests/unit/ForceFeedbackSafetyControllerTests.cpp`](../tests/unit/ForceFeedbackSafetyControllerTests.cpp)
and [`ForceFeedbackEngineTests.cpp`](../tests/unit/ForceFeedbackEngineTests.cpp),
including an explicit test that every fault and every watchdog timeout path
ends in a `stopDevice` decision.

### What happens on focus loss

Not yet a special case. DirectInput's non-exclusive/background cooperative
level (the default for input, and the input-preserving default for force
feedback too — see the feasibility doc's discussion of
`requestExclusiveForceFeedbackAccess`) does not automatically stop effects
when the game loses focus. The watchdog is the mechanism that would catch a
game/bridge that has actually stopped responding; a game merely losing
window focus while everything is still running is not, by itself, currently
treated as a stop condition. This is a deliberate placeholder for a
decision to make once real hardware testing is underway, not an oversight.

## Compatibility

Only the DirectInput backend implements the DIEFFECT/`SendForceFeedbackCommand`
path described here. The Logitech SDK backend (compiled only when
`RVWHEEL_ENABLE_LOGITECH_SDK=ON`) has its own `ApplyForceFeedback`
implementation that delegates to `ILogitechSdk::PlayConstantForce/
PlaySpringForce/PlayDamperForce/SetGain/StopAllForces` — see
[`LogitechDevice.cpp`](../src/Devices/Logitech/src/LogitechDevice.cpp) — but
this is not RVWheel's default build path and has the same "never
hardware-validated" status.

## Diagnostics

```powershell
rvwheel_device_probe.exe --list           # Prints hasForceFeedback per device (read-only, always safe).
rvwheel_device_probe.exe --ffb-simulate [--duration <s>] [--rate <hz>] [--profile <id-or-path>]
rvwheel_device_probe.exe --ffb-hw-test-stop-only --ffb-cooperative-level foreground
rvwheel_device_probe.exe --ffb-hw-test-stop-only --ffb-cooperative-level foreground-focused
```

`--ffb-simulate` resolves the device's profile (or a conservative built-in
demonstration config if the profile has no `forceFeedback` block), runs the
full engine loop, and prints each computed command, the safety controller's
state, and how many times a command/stop would have reached the device —
all against an in-process recording sink, never the real device. See
`SimulatedForceFeedbackSink` in
[`DeviceProbeApp.cpp`](../tools/device_probe/DeviceProbeApp.cpp) for the
structural (not just behavioral) guarantee behind that claim.

The final command is a gated real-hardware diagnostic, not a simulation.
It creates no effect and starts no force, but it does request exclusive
DirectInput access and calls the real stop path once. Its foreground mode
uses a valid process-owned top-level window that is deliberately invisible
and unfocused for the first experiment. Run it only with explicit operator
authorization and the procedure in
[FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md).

## How to disable force feedback entirely

It already is, by default, in every shipped profile. If you have generated
or hand-written a profile with `forceFeedback.enabled: true`, either delete
that block or set it back to `false` — no code change is needed, since
nothing in the launcher or bridge calls `ForceFeedbackEngine::Enable()` yet
regardless of profile content.

## How to contribute support for a new wheel's force feedback

1. Confirm `--list` reports `forceFeedback=true` for your device.
2. Run `--ffb-simulate` against it and confirm the printed commands look
   sane (correct clamping, expected ramp behavior) — this is still
   completely safe.
3. Do **not** attempt a real physical test without first reading
   [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md) and
   getting explicit go-ahead; this project treats "a low force is
   automatically safe" as a false assumption, not a shortcut.
4. Report exactly what you tested and on what hardware — the same honesty
   policy as the rest of this project's hardware documentation applies.

## Limitations

- No telemetry-derived effect (self-aligning torque, collision impulses,
  terrain/engine vibration) is implemented; only a profile-configured
  spring/damper baseline exists.
- No Lua-side telemetry capture exists; `VehicleTelemetry` has no real
  producer yet.
- Nothing in the launcher or bridge calls `ForceFeedbackEngine::Enable()`;
  the engine is currently only reachable via `--ffb-simulate`.
- Real weak spring/damper diagnostics have been applied to one G923 under
  explicit per-run authorization. They reproducibly lose exclusive DirectInput
  FFB access at approximately two seconds and therefore have **not passed
  validation**; see the hardware test incident log. No production/gameplay FFB
  path is enabled.
- Only DirectInput's condition/constant-force effect types are wired;
  periodic effects (sine, square, etc.) and envelopes are not implemented.
