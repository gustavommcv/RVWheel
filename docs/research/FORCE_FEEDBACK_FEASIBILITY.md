# Force Feedback feasibility research

Access date for every source below: 2026-08-09. This document separates
**confirmed by official documentation**, **observed in this repository**,
**not yet verified in-game**, **inference**, and **risk** — do not read any
claim here as validated on real hardware/gameplay unless it says so
explicitly.

## 1. DirectInput force feedback — confirmed by Microsoft documentation

- **Effect lifecycle**: `IDirectInputDevice8::CreateEffect` builds an
  `IDirectInputEffect` from a `DIEFFECT` struct; `SetParameters` updates an
  existing effect (optionally starting it via the `DIEP_START` flag, which
  the docs note is "slightly faster than calling Start separately"); `Start`
  begins playback; `Stop` halts it without releasing the effect object. —
  [DIEFFECT Structure](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416616(v=vs.85)),
  [IDirectInputEffect::SetParameters](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417952(v=vs.85))
- **`DIEFFECT` fields, confirmed exactly**: `dwDuration` is in microseconds
  (`INFINITE` = infinite duration, or infinite sustain after an envelope
  attack); `dwGain` is `0..10000`; `rgdwAxes` allows at most 32 axes and,
  once set on an effect, the axis list/count "cannot be changed"; for
  `DIEFT_CONDITION` effects (spring/damper), `lpvTypeSpecificParams` points
  at one or more `DICONDITION` structs, and if a single struct is supplied
  (as RVWheel's code does), `rglDirection` must be `{1, 0}` under
  `DIEFF_CARTESIAN` for a 1-axis effect — this exact convention is already
  followed in `DirectInputDevice.cpp`. — same DIEFFECT source as above.
- **Exclusive access is required for force feedback — confirmed by two
  independent Microsoft sources, not an inference**:
  - "To use force-feedback effects, an application must have exclusive
    access to the device." —
    [Cooperative Levels](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416848(v=vs.85))
  - Microsoft's own official DirectX SDK force-feedback sample
    (`FFConst.cpp`) sets `DISCL_EXCLUSIVE | DISCL_FOREGROUND` with the
    inline comment *"Exclusive access is required in order to perform force
    feedback."* —
    [walbourn/directx-sdk-samples-reworked, FFConst.cpp](https://github.com/walbourn/directx-sdk-samples-reworked/blob/main/DirectInput/FFConst/ffconst.cpp)
  - `SendForceFeedbackCommand` and `GetForceFeedbackState` both explicitly
    document: "The device must be acquired at the exclusive cooperative
    level for this method to succeed." —
    [SendForceFeedbackCommand](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417918(v=vs.85)),
    [GetForceFeedbackState](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417902(v=vs.85))
  - **Important nuance, also confirmed**: exclusive access blocks other
    applications from *also* acquiring exclusively, but "nonexclusive access
    to the device is always permitted, even if another application has
    obtained exclusive access" —
    [IDirectInputDevice8::SetCooperativeLevel](http://doc.51windows.net/Directx9_SDK/input/ref/ifaces/idirectinputdevice9/setcooperativelevel.htm).
    This means RVWheel's bridge holding `DISCL_EXCLUSIVE` for FFB would not,
    by itself, prevent G HUB from reading the same device nonexclusively —
    but it would prevent G HUB (or anything else) from *also* driving force
    feedback on it at the same time. This has not been tested against real
    G HUB behavior (see §5, open questions).
  - `DIDC_FORCEFEEDBACK` in `DIDEVCAPS.dwFlags` is the documented way to
    detect FFB support at enumeration time — already implemented correctly
    in `DirectInputDeviceEnumerator.cpp` (see §3).
- **Device-wide stop/reset commands, confirmed exact flags** (not currently
  used by RVWheel's code — see §3 gap):
  - `SendForceFeedbackCommand(DISFFC_STOPALL)` — "equivalent to calling
    `IDirectInputEffect::Stop` for each effect playing", but device-wide:
    stops effects RVWheel did not itself create/track.
  - `SendForceFeedbackCommand(DISFFC_RESET)` — "puts the force-feedback
    system in its startup state. All effects are removed... device's
    actuators are disabled." Strongest available stop primitive.
  - `SendForceFeedbackCommand(DISFFC_SETACTUATORSOFF)` — mutes actuators
    while leaving effects "valid" (per docs, effects "continue to play but
    are ignored by the device").
  - `GetForceFeedbackState` reports `DIGFFS_ACTUATORSON/OFF`,
    `DIGFFS_DEVICELOST`, `DIGFFS_SAFETYSWITCHON/OFF` (a hardware safety
    switch some wheels expose), `DIGFFS_USERFFSWITCHON/OFF`. All flags are
    "ignore if not defined" — a device may report none of them.
  - Both APIs warn: after sending a command, a subsequent state read "may
    not match the expected state" if the command is still pending — any
    safety controller relying on read-back must tolerate a short delay, not
    treat a stale read as failure.

## 2. Unreal Engine force feedback — confirmed, and why Strategy A is weak

- Unreal's force feedback system is built around `UForceFeedbackEffect`
  assets played through a `PlayerController`, targeting four fixed
  gamepad-vibration channels (`LeftLarge`, `LeftSmall`, `RightLarge`,
  `RightSmall`) via `FForceFeedbackValues`, each driven by an intensity
  curve over time. — [Force Feedback in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/force-feedback-in-unreal-engine)
- This is a **vibration-intensity model for gamepads, not a directional
  torque model for a wheel**. Even in the best case (the game does call
  this system, and UE4SS could somehow intercept it), the four channel
  values do not carry the sign/direction information a steering wheel
  effect needs — they would need to be reinterpreted, not just relayed.
- No documented mechanism exists for a third-party plugin to *observe*
  force-feedback values the game produces; Epic's own documentation "does
  not describe any publicly documented method" for this (checked directly
  against the official page above).
- **Conclusion for Strategy A ("capture FFB the game already produces")**:
  not recommended. It targets the wrong data shape (vibration intensity,
  not torque) and has no documented, stable interception point. Building on
  it would mean depending on an unofficial, binary-fragile hook into
  Unreal's private controller-vibration dispatch — exactly the "fragile
  binary patch" risk this task was told to avoid.

## 3. What RVWheel's own code already does — observed directly in this repository

Traced end to end, not inferred from names (see the Etapa 1 diagnosis
presented in-session):

- `IWheelDevice::ApplyForceFeedback`/`StopForceFeedback` and
  `ForceFeedbackCommand{constantForce, spring, damper, gain}` — real,
  backend-agnostic contract. [`IWheelDevice.hpp`](../../src/Core/include/rvwheel/dal/IWheelDevice.hpp),
  [`WheelTypes.hpp`](../../src/Core/include/rvwheel/dal/WheelTypes.hpp)
- `DirectInputDevice::ApplyConstantForce/ApplySpring/ApplyDamper/ApplyGain` —
  genuinely call `CreateEffect(GUID_ConstantForce/GUID_Spring/GUID_Damper)`,
  `Start`, `SetParameters(..., DIEP_START)`, and
  `SetProperty(DIPROP_FFGAIN)`; effects are created once and reused, matching
  the DIEFFECT "axis list cannot be changed once set" constraint correctly.
  [`DirectInputDevice.cpp`](../../src/Devices/DirectInput/src/DirectInputDevice.cpp)
- `StopForceFeedback` calls `Stop()` on each of the three effect COM
  pointers it tracks, and the destructor does the same before
  `Unacquire()`. **Gap**: this only stops effects RVWheel itself created; it
  never calls `SendForceFeedbackCommand(DISFFC_STOPALL)` or `DISFFC_RESET`,
  so a rogue/leftover effect from a previous crashed process would not be
  covered. See Etapa 7/Etapa 6 in the implementation plan.
- `DirectInputDeviceEnumerator` detects `DIDC_FORCEFEEDBACK` correctly and
  only requests `DISCL_EXCLUSIVE` when both the device has FFB **and**
  `DeviceManagerInitParams::requestExclusiveForceFeedbackAccess` is `true`
  (default `false`). [`DirectInputDeviceEnumerator.cpp`](../../src/Devices/DirectInput/src/DirectInputDeviceEnumerator.cpp),
  [`DeviceManagerFactory.hpp`](../../src/Core/include/rvwheel/dal/DeviceManagerFactory.hpp)
- **No caller anywhere in `tools/` ever invokes `ApplyForceFeedback` with a
  nonzero command.** The only two FFB references outside `src/Devices` are
  passing `requestExclusiveForceFeedbackAccess = false` and printing
  `hasForceFeedback` in `--list`. Confirmed by exhaustive grep of `tools/`.
  **No force has ever been sent to hardware by any existing RVWheel code
  path.**
- Zero automated tests exercise this code (no `DirectInputDevice` FFB test,
  fake or real).
- The G923's FFB capability was observed once, read-only, during earlier
  hardware validation ("reported FFB capability" in the README's status
  list) — it was never exercised. [`G923_DIRECTINPUT_CAPTURE.md`](../hardware/G923_DIRECTINPUT_CAPTURE.md)
  documents "Force feedback: not exercised."

## 4. UE4SS/Lua telemetry access — confirmed capability, unconfirmed vehicle data

- Confirmed from UE4SS's own Lua API reference: generic property read
  (`__index`/`GetPropertyValue`) and UFunction invocation (`__call`/
  `CallFunction`) work on any `UObject`/`UStruct` already in memory. —
  [UE4SS Lua API](https://docs.ue4ss.com/lua-api.html)
- **No documented performance guidance exists** for calling reflection
  every tick. This matches RVWheel's own prior, hard-won experience: the
  task brief explicitly warns "já houve instabilidade com reflection
  pesada," and `mods/RVWheel/Scripts/main.lua` already reflects this lesson
  — it hooks one specific `TickInputs` UFunction and reads a small, fixed
  set of already-known properties (`SteeringInput`, `CurrentGear`,
  `GearInput`, `Gears`), rather than scanning.
- **What vehicle data is actually confirmed reachable today**: exactly the
  properties `mods/RVWheel/Scripts/main.lua` and
  [`UE4SS_FIRST_TEST.md`](../game-integration/UE4SS_FIRST_TEST.md) already
  use/list: `SteeringInput`, `LocalSteeringInput`, `Steering`,
  `ThrottleInput`, `BrakeInput`, `CurrentGear`, `GearInput`, `Gears` (array),
  plus the setter functions and RPCs listed there. **Speed, wheel load,
  suspension travel, lateral slip, and surface/collision data have never
  been queried or confirmed accessible in this game** — the one F9
  reflection pass performed to date was explicitly scoped to "input-related
  functions and properties" only (`UE4SS_FIRST_TEST.md`, Controls section).
- The vehicle's actual physics implementation is the marketplace "Advanced
  Vehicle System" (AVS) plugin
  (`/VehicleSystemPlugin/AVS_Vehicle.AVS_Vehicle_C`). Public search results
  describe AVS at a marketing level (modular wheels, suspension simulation,
  arcade-style torque) but did not surface a public per-property API
  reference (speed/velocity/suspension property names) that could be cited
  as confirmed without an in-game check. — [Advanced Vehicle System — Fab listing](https://www.unrealengine.com/marketplace/en-US/product/advanced-vehicle-system)
- **This is the single largest open question for Strategy B**, and it is
  explicitly *not* resolved by this research pass, because resolving it
  requires a new, scoped reflection probe against the running game — which
  this task's safety rules (no aggressive reflection during gameplay, and
  no live-game session without authorization in this working session)
  correctly prevented from happening today. See §6.

## 5. Prior art for telemetry/memory-derived FFB in games without native support

- [gplaps/GP2FFB](https://github.com/gplaps/GP2FFB) — force feedback for a
  game with no native FFB, driven by reading the game's own process memory.
  Confirms the general pattern (external force computation fed from
  whatever telemetry/state is actually observable) is a proven approach for
  exactly this class of problem, though its data source (raw memory reads)
  is a different mechanism than UE4SS/Lua reflection.
- [Mhytee/Trueforce-For-All](https://github.com/Mhytee/Trueforce-For-All) —
  explicitly combines telemetry-derived effects with "audio-derived
  effects that let you feel things telemetry doesn't expose," and is
  designed to still function "even for games which do not output telemetry
  data" by falling back to weaker signals. Reinforces the fallback
  philosophy this document recommends in §6 (declare the limitation and
  degrade gracefully rather than inventing physics).
- [Ultrawipf/OpenFFBoard](https://github.com/Ultrawipf/OpenFFBoard) — a
  DIY universal FFB interface; relevant mainly as a reference for
  effect-mixing/prioritization design on the firmware side, not for the
  Unreal integration question.

## 6. Strategy comparison and recommendation

| | Fidelity | Risk | Maintenance | Game-version coupling | Latency | Testability | Wheel compatibility |
|---|---|---|---|---|---|---|---|
| **A. Capture game-produced FFB** | Low (wrong data shape — vibration channels, not torque) | High (depends on an undocumented interception point) | High (no stable API to target) | Very high | N/A (blocked) | Low | N/A |
| **B. Compute FFB from vehicle telemetry** | Potentially high, but **unverified today** — only steering/gear confirmed reachable | Medium (reflection-heavy queries repeated last session's instability if not scoped/cached) | Medium | Medium (depends on AVS's own property names) | Low if properties are cheap to read | High (once real values are known, math is pure/testable) | High (backend-agnostic once computed) |
| **C. Hybrid (profile-driven spring/damper baseline + telemetry-derived SAT later)** | Medium now, can grow to high | Low for the baseline; Medium for the SAT half until B is verified | Low for the baseline | Low for the baseline; Medium for the SAT half | Low | High | High |

**Recommendation: Strategy C, but the two halves are not equally ready.**
The baseline half (profile-configured centering spring + damper, no
telemetry required at all) is fully buildable today with confirmed APIs and
zero open questions. The telemetry-derived half (Self-Aligning Torque from
real vehicle state) is architecturally prepared for in this pass (a
`VehicleTelemetry` type and an `IForceFeedbackSource` seam that a future Lua
telemetry hook can feed) but its actual numeric output is **not
implemented** until §4's open question is resolved. This matches the task's
own priority order (centering spring first, SAT "only if there is enough
telemetry," collision impulses and terrain/engine vibration "only after the
base is stable") and avoids inventing "realistic" effects from unverified
data.

Strategy A is rejected outright per §2 — not a close call.

## 7. Open questions (unresolved by this research pass)

1. **Does AVS_Vehicle_C (or a component on it) expose speed, lateral
   velocity, per-wheel load, or suspension compression as a Lua-readable
   property?** Requires a scoped, snapshot-style reflection probe against
   the running game (similar to the existing `RVWheelDiscovery` F8/F9 mod,
   but targeted specifically at physics/telemetry properties, and run only
   once, not per-tick) — **not performed in this session**; needs its own
   explicit go-ahead since it requires the game running.
2. **Does the G923 actually grant `DISCL_EXCLUSIVE` cleanly while G HUB is
   running, and does input polling continue to work for RVWheel's own
   bridge while exclusive?** Documented behavior says nonexclusive readers
   remain unaffected, but this has not been tested against this specific
   device/driver. This is answerable by a diagnostic (enumerate + attempt
   exclusive acquisition + report result) **without ever creating an
   effect or applying force** — planned as part of the DirectInput backend
   hardening (Etapa 7), safe to run without further authorization since it
   sends no force.
3. **Does the G923 report `DIGFFS_SAFETYSWITCHON/OFF` or
   `DIGFFS_USERFFSWITCHON/OFF`?** Unknown until queried; if it does, the
   safety controller's diagnostics should surface it, since per Microsoft's
   own docs the device may simply not report these at all.
4. **What happens to an in-progress effect when the game (or Windows) takes
   focus away from nothing in particular — i.e., does `DISCL_BACKGROUND`
   vs `DISCL_FOREGROUND` change anything for a wheel (not mouse/keyboard)
   once exclusive?** The cooperative-level table marks
   `DISCL_EXCLUSIVE | DISCL_BACKGROUND` as valid for non-mouse/keyboard
   devices, but Microsoft's own canonical FFB sample uses `FOREGROUND`.
   This document's safety design (§ in `docs/FORCE_FEEDBACK.md`) treats
   focus loss as a stop-worthy event regardless, which sidesteps needing a
   definitive answer here — but the question is left open for anyone tuning
   that behavior later.

### Cooperative-level investigation update (implementation prepared, hardware result pending)

The official `IDirectInputDevice8::SetCooperativeLevel` documentation adds
two constraints directly relevant to the reproducible two-second failure:

- its `HWND` must be a valid top-level window owned by the calling process;
- a `DISCL_FOREGROUND` device is automatically unacquired when that window
  moves to the background;
- an application using `DISCL_EXCLUSIVE | DISCL_BACKGROUND` is explicitly
  **not guaranteed to retain access** if another application requests
  exclusive access; the documented recovery is to unacquire and acquire
  again.

Sources: [SetCooperativeLevel](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417921(v=vs.85))
and [Acquiring Devices](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee415221(v=vs.85)).

This strengthens the interpretation that the existing background-exclusive
path can legitimately be displaced, but it does **not** identify which
process/driver/firmware behavior does so on the G923 and therefore is not a
root-cause claim.

The code now models `Background` versus `Foreground` as an explicit policy.
The ordinary input path still resolves unconditionally to
`DISCL_NONEXCLUSIVE | DISCL_BACKGROUND`; only explicit real-FFB diagnostic
modes can select foreground. The enumerator now logs the exact cooperative
level and HRESULT from both `SetCooperativeLevel` and the initial `Acquire()`.

The first experiment uses a process-owned top-level window that is
deliberately invisible and unfocused, and creates no effect. This is more
precise than reusing the normal `HWND_MESSAGE` window: message-only windows
do not satisfy the documented top-level-window contract, so a failure with
one would conflate an invalid HWND shape with foreground-priority behavior.
The first hardware experiment is now complete: with the valid top-level
window deliberately unfocused, initial acquisition and all retries failed
immediately with `0x80070005 = DIERR_OTHERAPPHASPRIO`; no effect was created
or started, the process exited `1`, and the operator confirmed no apparent
physical change. This **confirms the focus prerequisite** on the real G923
but leaves the larger `DISCL_FOREGROUND` hypothesis **inconclusive** until a
focused top-level window is tested. A dedicated visible tool-window mode is
implemented for that next stop-only experiment and refuses to initialize
DirectInput unless `GetForegroundWindow()` confirms ownership. It has not
yet been run. Exact commands and observations are in
[FORCE_FEEDBACK_HARDWARE_TEST.md](../FORCE_FEEDBACK_HARDWARE_TEST.md).

The focused follow-up has now also run: Windows confirmed the dedicated
tool window as foreground, initial `Acquire()` succeeded under
`DISCL_EXCLUSIVE | DISCL_FOREGROUND`, input remained readable, the stop-only
path returned `Ok`, and the operator reported no behavior beyond the G923's
pre-existing idle autocenter. This **confirms foreground acquisition is
viable** on the device. It still does not answer retention: the run lasted
roughly 200 ms, well below the prior ~2-second failure point. A no-effect
five-second retention probe is the next lowest-risk experiment.

That retention probe has now passed: `EXCLUSIVE | FOREGROUND` remained
acquired for 5.01 seconds with the HWND continuously confirmed foreground,
245/245 input polls readable, successful `GetForceFeedbackState` reads
(`ACTUATORSOFF | EMPTY | POWERON`), and successful device-wide STOPALL at
the end. The operator reported no physical change beyond the wheel's
pre-existing idle autocenter. This is **strong evidence that foreground
ownership prevents the timed exclusive-access loss in the no-effect case**.
It is not yet a confirmed fix for the original effect-update failure,
because no effect was created or updated in this probe.

The separately authorized focused weak-spring follow-up has now falsified
that candidate fix for the real failure. Effect creation/start succeeded, but
at approximately 2.0 seconds the controller faulted and spring
`SetParameters` returned `0x80040205 = DIERR_NOTEXCLUSIVEACQUIRED`, matching
the background-exclusive runs. Therefore focus/cooperative level explains the
idle acquisition behavior but **does not explain or fix the effect-active
failure**. The remaining investigation must isolate behavior specific to the
active effect/update path (including redundant parameter restarts, device-wide
gain writes, autocenter ownership, or driver/firmware behavior); it must not
claim foreground mode as a production solution.

The immediate code review then confirmed two active-effect-path violations of
Microsoft's documented contract. First, type-specific effect buffers were
function locals even though DirectInput requires them to remain valid for the
effect lifetime. Second, updates passed `DIEP_START` on every tick, explicitly
restarting the effect, rather than sending the minimal
`DIEP_TYPESPECIFICPARAMS` update used by Microsoft's own example. The backend
now owns persistent buffers, does not explicitly restart updates, and skips
unchanged gain/effect values. These are generic DirectInput fixes, not a G923
special case. Hardware validation remains pending, so neither defect is yet
claimed as the root cause of the two-second loss.

A subsequent instrumented run exposed the actual ownership transition. The
effect stayed logically active for five seconds, but the process logged a
second successful exclusive Acquire. The weak-effect loop was calling
`DeviceManager::RefreshIfDue()`; discovery creates/acquires a candidate before
the manager compares its `DeviceId` with the preserved instance. Because the
first acquisition occurs before a three-second countdown and the refresh
interval is five seconds, the duplicate acquired the wheel almost exactly two
seconds after effect activation. It thereby revoked the original instance's
exclusive FFB ownership; final STOPALL returned
`DIERR_NOTEXCLUSIVEACQUIRED`. This accounts for the repeatable timing without a
firmware-watchdog theory.

The operator also felt resistance decrease below the normal G923 baseline and
then quickly return. This corroborates both halves of the trace: device-wide
gain reached the motor, and the duplicate acquisition ended that altered state
near the refresh boundary.

The diagnostic now prohibits periodic rediscovery during an exclusive effect
session, while retaining per-frame polling for disconnect detection. Production
integration must model the same invariant (stop/release the exclusive owner
before a hotplug rescan). Hardware confirmation of the corrected ownership
lifecycle is still pending.

The first separately authorized G923 validation run after that change passed
technically: one exclusive Acquire, five full seconds `Active`, readable input,
foreground retained, successful final STOPALL, and no exclusive-access error.
The operator confirmed the resistance remained altered for the session and
returned to the normal baseline after stopping, with no residual abnormal
force. This strongly confirms the duplicate refresh acquisition as the cause.
Per the hardware procedure, one successful run is evidence but not yet
sufficient for final validation; consecutive runs remain pending.

## Update — first real hardware test (2026-08-09)

Step 4 of [FORCE_FEEDBACK_HARDWARE_TEST.md](../FORCE_FEEDBACK_HARDWARE_TEST.md)
passed, resolving open question 2 above: exclusive FFB acquisition on the
G923 does not break input polling. Step 6 (a weak spring effect) surfaced a
new, unresolved open question and two real bugs (now fixed) in RVWheel's
own code — see the hardware test doc's Incident Log for full detail. The
new open question:

5. **Why did `IDirectInputEffect::SetParameters`/`Stop` start failing after
   roughly two seconds of a real, weak spring effect running? Partially
   answered.** Reproduced on a second run with HRESULT diagnostics in
   place: the failure is `0x80040205` = `DIERR_NOTEXCLUSIVEACQUIRED` --
   the device's acquisition was downgraded from exclusive to nonexclusive
   partway through, at a similar elapsed time in both runs, independent of
   the unrequested-extra-effects bug (which was already fixed for the
   second run, ruling out hypothesis (c) below). **What remains open is
   *why* the acquisition is downgraded -- and Logitech G HUB plus foreground
   cooperative mode, two leading hypotheses, have since been ruled out**:
   a focused foreground run retained idle exclusivity for five seconds but
   lost it at ~2 seconds as soon as a weak spring was actively updated; and
   a run with
   `lghub_agent.exe`/`lghub_system_tray.exe` terminated reproduced the
   identical failure at the identical elapsed time. Instrumentation also
   ruled out `DirectInputDevice::Poll()`'s reacquire path: input never lost
   acquisition before the effect call failed. Remaining candidates, none
   confirmed, include the two application-side defects now corrected
   (dangling type-specific parameter buffers and explicit restart/redundant
   updates), autocenter ownership, a driver/firmware timeout, or another
   process such as `lghub_updater.exe`. The hardware impact of the code fixes
   remains untested. The next instrumented run must report foreground
   retention and input-poll status separately from the effect failure.

## Risks carried forward into implementation

- Enabling FFB changes the device's cooperative level for *this device
  instance* from the always-nonexclusive input path RVWheel has shipped and
  validated so far. This must stay strictly opt-in (already true via
  `requestExclusiveForceFeedbackAccess`) and never activate silently.
- Any safety controller built on top of per-effect `Stop()` alone is
  incomplete per §1/§3 — it must also reach for
  `SendForceFeedbackCommand(DISFFC_STOPALL)`/`DISFFC_RESET` to cover effects
  it did not itself create (e.g. left over from a crashed prior process).
- Strategy B's telemetry half must not ship numeric "physics" derived from
  unverified properties. Until open question 1 is resolved, RVWheel's SAT
  source must either be absent or explicitly report
  `InsufficientTelemetry` rather than guess.
