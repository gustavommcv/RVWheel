# Force Feedback

> [!IMPORTANT]
> Force feedback is **still not a finished feature**, but `--bridge` now has
> a real, opt-in force feedback path: `rvwheel_device_probe --bridge
> --enable-force-feedback` arms the profile-configured centering spring
> through the exact same safety controller and exclusive-access handling
> validated by the gated hardware tests (see the **Incident log** in
> [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md)). It is
> off by default and requires two independent opt-ins (the CLI flag and the
> profile's own `forceFeedback.enabled`). It applies only a static
> centering spring/damper — **no telemetry, no reaction to speed, terrain,
> or collisions**, and no other wheel model or gain level has been tried.
> The launcher does not enable this automatically. See
> [docs/research/FORCE_FEEDBACK_FEASIBILITY.md](research/FORCE_FEEDBACK_FEASIBILITY.md)
> for the full history. Do not raise the validated gain values or flip a
> shipped profile's `enabled` to `true` without a matching gated hardware
> validation entry.

## Status at a glance

| Piece | Status |
|---|---|
| DirectInput effect creation/update/stop (`CreateEffect`, `SetParameters`, `Stop`, `SendForceFeedbackCommand`) | Implemented; root cause of the ~2s `DIERR_NOTEXCLUSIVEACQUIRED` failure found and fixed (see Incident log) -- confirmed stable for a full 5s weak effect in both foreground-focused and background cooperative levels |
| Capability detection (`DIDC_FORCEFEEDBACK`) | Implemented and confirmed working against a real G923 (read-only) |
| Exclusive FFB acquisition | Confirmed working on a real G923 without breaking input polling, in both `DISCL_FOREGROUND` and `DISCL_BACKGROUND` |
| Cooperative-level hypothesis (`DISCL_FOREGROUND` vs `DISCL_BACKGROUND`) | **Resolved as a red herring**: the ~2s failure was a `DeviceManager` re-enumeration bug, not a foreground/background distinction. Fixed once, confirmed stable in both modes |
| Safety controller (clamps, watchdog, slew rate, fault handling) | Implemented, unit-tested (37+ tests); a real gain-ramp overshoot bug was found and fixed after the first real activation |
| Profile-configured spring/damper source | Implemented, unit-tested; ran a full stable 5s window on real hardware across multiple runs, see Incident log |
| Telemetry-derived self-aligning torque | **Not implemented** — see [Limitations](#limitations) |
| Simulation mode (`--ffb-simulate`) | Implemented, exercised against real hardware in read-only/simulated form |
| Real force applied to a device | Weak spring/damper diagnostic (gain 0.2) now runs a full stable 5s window with a clean stop, confirmed across several runs in foreground-focused and background modes; no unsafe motion reported in any run |
| `--bridge --enable-force-feedback` | Implemented: two independent gates (CLI flag + profile `forceFeedback.enabled`), readiness-gated `Enable()`, optional `--duration` bound, confirmed/observable `Stop()`, disables periodic re-enumeration while active. Physically validated standalone (weak spring, 5s bounded run) -- see the "Bridge integration first physical test" entry in [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md) |
| `rvwheel_launcher --enable-force-feedback --profiles-dir <path>` | Implemented: both opt-in and off by default (a plain launcher invocation is unchanged), fail-closed if a bridge is already running, pure/tested argument parsing and command-line construction. Physically validated once **inside a real game session**: `engine ENABLED` reached, steering/pedals/clutch/shifter unaffected, spring stayed stable, resistance returned to normal after closing the game -- see the "First in-game physical test" entry in [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md) |

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
  real `IWheelDevice`. Driven by `--ffb-simulate` (through a recording
  sink), the gated hardware-test modes, and now `--bridge
  --enable-force-feedback` via `BridgeForceFeedbackSession`
  ([`BridgeForceFeedbackSession.hpp`](../tools/device_probe/BridgeForceFeedbackSession.hpp)),
  a small RAII wrapper that arms the engine once, ticks it every bridge
  frame with the device's own current steering (no telemetry), and
  guarantees exactly one stop on every exit path.
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

`forceFeedback.enabled: true` is necessary but not sufficient: `--bridge`
also requires the separate `--enable-force-feedback` runtime flag (off by
default) before it ever calls `ForceFeedbackEngine::Enable()` — see
[Bridge integration](#bridge-integration) below. A normal player run of
`rvwheel_launcher.exe` (no arguments) never passes this flag, so it never
enables force feedback regardless of profile content -- the launcher can
forward it, but only when `--enable-force-feedback` is explicitly given.
`hasForceFeedback` being reported by a device is also only a capability,
not proof every effect type actually works on that unit — see
[Limitations](#limitations).

## Bridge integration

`rvwheel_device_probe --bridge --enable-force-feedback [--profile <id-or-path>] [--duration <seconds>]`
is the first place force feedback can run outside a diagnostic mode. Two
independent gates must both be satisfied before any force is ever applied:

1. the `--enable-force-feedback` CLI flag (runtime, off by default);
2. the resolved profile's own `forceFeedback.enabled: true` (data, off by
   default in every shipped profile).

Passing the flag alone requests `DISCL_EXCLUSIVE | DISCL_BACKGROUND` access
up front (before the profile is even resolved, since a device must be found
before its profile can be), so exclusive access is held for the rest of the
run even if the profile turns out to lack a valid `forceFeedback` block --
in that case the bridge logs why and simply runs input-only, exactly like a
plain `--bridge` invocation. **No demonstration/fallback configuration is
ever substituted** the way `--ffb-simulate` substitutes one; either the
profile's real config is used, or nothing is armed. That config is the
resolved profile's own block, carried forward directly from whichever
`DeviceProfile` was actually applied to the device (see
`AppliedProfileInfo::forceFeedback` in `DeviceProbeApp.cpp`) --
`ProfileRepository::MergeProfiles()` collapses a built-in and a
same-`profileId` user override into exactly one entry (the user profile
replaces the built-in outright; there is no second, built-in candidate to
fall back to), so a bridge session never re-derives this by searching for
`profileId` a second time.

Even once armed, `BridgeForceFeedbackSession::Enable()` is **not** called
immediately after the profile resolves. The bridge waits until a `Poll()`
succeeds and the device reports `connected == true`, `valid == true`, and
`readiness == Ready` (see `IsReadyToEnableForceFeedback` in
`BridgePolicies.hpp`) before calling `Enable()` -- input keeps publishing
normally while it waits, and if readiness never arrives, the engine is
simply never enabled and no force is ever applied.

While a force feedback session is active, `--bridge` **never calls
`DeviceManager::RefreshIfDue()`** — see `ShouldRefreshDuringBridge` in
[`BridgePolicies.hpp`](../tools/device_probe/BridgePolicies.hpp) and the
invariant explained in
[FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md)'s
incident log: periodic re-enumeration racing an active exclusive effect was
the confirmed root cause of the original `DIERR_NOTEXCLUSIVEACQUIRED`
failure. This decision depends only on whether `--enable-force-feedback`
was passed for the run, never on the session's live state (armed, active,
faulted, stopped) -- a session ending or faulting mid-run can never flip
refresh back on. One consequence: **reconnecting the wheel while
`--enable-force-feedback` is active requires restarting the bridge
process** in this first version, since the bridge will not rediscover a
new device instance.

`BridgeForceFeedbackSession::Stop()` returns a `BridgeForceFeedbackStopResult`
(the safety controller's own stop decision plus the `Status` of an explicit,
belt-and-suspenders `StopForceFeedback()` call) rather than discarding the
outcome -- `RunBridge()` prints `Final StopForceFeedback(): Ok` (or the
failing `StatusCode`/message) and **returns a non-zero exit code if the
stop was not confirmed**, so a caller (a script, or an operator reading the
console) never has to assume the stop worked. This runs on every exit path:
normal loop exit, `--duration` elapsing, the supervised parent process
disappearing, Ctrl+C, and — via the session's own destructor, best-effort
and `noexcept` — any other unwind, including an exception. A backend fault
(e.g. a repeat of the `DIERR_NOTEXCLUSIVEACQUIRED` failure) logs once, stops
force feedback, and lets the bridge keep publishing input normally for the
rest of the run; nothing automatically re-arms it.

`--duration <seconds>`, when explicitly given alongside `--bridge`, stops
the bridge automatically after that many seconds through this exact same
graceful shutdown sequence, instead of relying solely on an operator's own
Ctrl+C timing -- this is the mechanism the first real physical test uses
(see [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md)).
Omitting it preserves `--bridge`'s exact prior infinite-until-Ctrl+C
behavior, with or without `--enable-force-feedback`.

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
rvwheel_device_probe.exe --bridge --enable-force-feedback [--profile <id-or-path>] [--duration <s>]  # REAL force; see below.
rvwheel_launcher.exe --enable-force-feedback [--profiles-dir <path>]  # REAL force, inside the actual game; see below.
```

The launcher forwards `--enable-force-feedback` and `--profiles-dir`
(when given) straight to the bridge it starts, plus its usual `--rate 60`
and `--parent-pid`. Neither flag changes what a plain
`rvwheel_launcher.exe` (no arguments) does. If a bridge is already
running when `--enable-force-feedback` is passed, the launcher refuses to
reuse or kill it -- it shows a message asking you to close the existing
process first, since that bridge might be plain input-only.

`--ffb-simulate` resolves the device's profile (or a conservative built-in
demonstration config if the profile has no `forceFeedback` block), runs the
full engine loop, and prints each computed command, the safety controller's
state, and how many times a command/stop would have reached the device —
all against an in-process recording sink, never the real device. See
`SimulatedForceFeedbackSink` in
[`DeviceProbeApp.cpp`](../tools/device_probe/DeviceProbeApp.cpp) for the
structural (not just behavioral) guarantee behind that claim.

The `--ffb-hw-test-*` commands are gated real-hardware diagnostics, not
simulations. `--ffb-hw-test-stop-only` creates no effect and starts no
force, but it does request exclusive DirectInput access and calls the real
stop path once. `--bridge --enable-force-feedback` is the one command in
this list that applies real, sustained force -- see
[Bridge integration](#bridge-integration) above. Run any of these only
with explicit operator authorization and the procedure in
[FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md).

## How to disable force feedback entirely

It already is, by default: plain `rvwheel_launcher.exe` and plain
`--bridge` (no extra flag) never call `ForceFeedbackEngine::Enable()`,
regardless of profile content, and every shipped profile ships with
`forceFeedback.enabled: false`. If you specifically ran `--bridge
--enable-force-feedback` and want to stop, close the bridge process (Ctrl+C
or closing the window) — `BridgeForceFeedbackSession`'s destructor
guarantees a stop on the way out. To make a profile permanently inert
again, delete its `forceFeedback` block or set `enabled` back to `false`.

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
  spring/damper baseline exists. `--bridge --enable-force-feedback` applies
  a static centering spring regardless of speed, steering angle, terrain,
  or collisions -- it does not "feel" the game in any way yet.
- No Lua-side telemetry capture exists; `VehicleTelemetry` has no real
  producer. `BridgeForceFeedbackSession` always ticks the engine with an
  empty `VehicleTelemetry`.
- A normal player run of `rvwheel_launcher.exe` (no arguments) never
  enables force feedback regardless of profile content -- this stays true
  even though the launcher can now forward `--enable-force-feedback`/
  `--profiles-dir <path>` to the bridge when explicitly given both flags.
  Nothing about the default double-click path changed.
- Reconnecting the wheel while `--enable-force-feedback` is active requires
  restarting the bridge process in this first version -- periodic
  re-enumeration is disabled for the whole run once exclusive access is
  requested, so a physical unplug/replug is not rediscovered.
- Only the Logitech G923's `masterGain`/`springStrength`/
  `maxTorqueNormalized` at `0.2` and `slewRatePerSecond` at `0.5` have been
  physically validated (see the hardware test incident log, and
  `RunFfbHardwareTestWeakEffect`'s own fixed constants in
  `DeviceProbeApp.cpp`, which the shipped profile's values must always
  match); `damperStrength` stays at `0.0` in the shipped profile because
  damper was not validated across consecutive runs after the
  exclusive-access fix. No other wheel model has been tried, no gain above
  `0.2` has been tried, and no slew rate faster than `0.5`/s has been
  tried -- raising it is a separate validation step, not a "probably fine"
  tuning change.
- The shipped G923 profile ships with `forceFeedback.enabled: false`, so
  `--enable-force-feedback` alone does nothing against the default
  profile; a real end-to-end manual test needs a profile override with
  `enabled: true` -- see [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md).
- Only DirectInput's condition/constant-force effect types are wired;
  periodic effects (sine, square, etc.) and envelopes are not implemented.
- The game *has* now been opened with force feedback active, once, via
  `rvwheel_launcher --enable-force-feedback --profiles-dir <path>` --
  `engine ENABLED` was reached, steering/throttle/brake/clutch/shifter
  all worked normally, the spring stayed stable with no oscillation, and
  resistance returned to normal after closing the game -- see the "First
  in-game physical test" entry in
  [FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md). This
  is one run, one operator, one play session: it does not validate
  multiple consecutive in-game sessions, a mid-session disconnect, or the
  launcher's fail-closed check against an actually-conflicting bridge
  (unit-tested, not hardware-tested).
