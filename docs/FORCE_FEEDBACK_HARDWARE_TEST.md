# Force Feedback hardware test protocol

This is the gated procedure for the *first* time RVWheel ever sends real
force to a physical wheel. Nothing in this repository does that on its own;
every step below is manual, deliberate, and reversible until you choose to
proceed to the next one.

> [!WARNING]
> Do not run this alone the first time if you can avoid it. Have a way to
> immediately cut power to the wheel (unplug the USB cable, or a power
> switch) within reach at all times, independent of software.

## Before you start

- Read [FORCE_FEEDBACK.md](FORCE_FEEDBACK.md) fully, especially
  [Safety](FORCE_FEEDBACK.md#safety) and
  [Limitations](FORCE_FEEDBACK.md#limitations).
- Confirm you are running a build from a commit you have reviewed, not an
  arbitrary in-progress branch.
- Confirm `rvwheel_device_probe --ffb-simulate` (see
  [FORCE_FEEDBACK.md — Diagnostics](FORCE_FEEDBACK.md#diagnostics)) has been
  run against this exact wheel and produced sane, correctly-clamped output.
  Do not skip straight to real force without this.

## Stop criteria (read this before step 1)

Stop the test immediately, unplug the wheel if needed, and do not continue
to the next step if **any** of the following happen:

- The wheel moves, resists, or vibrates in a way you did not expect from
  the step you just ran.
- The applied force feels stronger than the gain you configured would
  suggest.
- The wheel does not stop within roughly one second of you releasing
  Ctrl+C, closing the tool, or unplugging it.
- Any diagnostic prints an unexpected fault, error, or a state other than
  what the step below says to expect.
- You are not confident, at any point, about what the tool is about to do
  next.

If you stop, capture the exact console output and file it as a bug before
trying again — do not simply retry with lower numbers and no explanation.

## Procedure

1. **Secure the wheel.** Mount or brace it exactly as you would for normal
   play — a loose wheel that can spin freely is not a safe test rig even at
   low force.
2. **Keep hands clear during the very first activation of any step below.**
   Only touch the wheel once you have confirmed (via console output) that
   the expected, and only the expected, effect is active.
3. **Start at the lowest possible gain.** Use a profile (or the
   `--ffb-simulate` demonstration config as a starting reference) with
   `masterGain` at 0.1 or lower for the first real run of every new effect
   type.
4. **Test Stop only, first.** Before enabling anything, confirm that
   whatever command you use to stop the tool (Ctrl+C, closing the process)
   visibly and immediately halts the wheel with no residual force. There is
   nothing to feel yet at this step — you are only confirming the stop path
   works before there is ever anything to stop.
5. **Test effect creation without Start.** If you add a diagnostic path
   that creates a DirectInput effect without starting it (not currently
   exposed by any CLI flag — this is a gap to close before this step is
   meaningful), confirm no force is felt at all. Skip this step with a
   documented note if no such path exists yet; do not substitute a real
   `Start()` call to "check" it.
6. **Test a very weak spring.** Enable only `springStrength` at a small
   value (e.g. 0.1) with `masterGain` at 0.1. Turn the wheel gently by hand
   and confirm it centers weakly and predictably, with no oscillation.
7. **Test a very weak damper**, same gain discipline as step 6, alone (no
   spring). Confirm resistance to fast movement only, no centering pull.
8. **Test the watchdog.** With an effect active from steps 6/7, stop
   sending fresh commands (e.g. suspend/kill the process controlling
   telemetry, if applicable, or simply let a deliberately-introduced stall
   happen) and confirm the wheel forces ramp to zero and stop within the
   configured watchdog timeout, without you touching anything.
9. **Kill the bridge process** while an effect is active and confirm the
   wheel stops. This exercises the destructor path
   (`DirectInputDevice::~DirectInputDevice`), not just `StopForceFeedback`.
10. **Close the game** (if integrated at this point) while an effect is
    active and confirm the same clean stop.
11. **Disconnect and reconnect the wheel** while the tool is running (no
    effect needs to be active for this one) and confirm the tool reports
    the disconnect/reconnect cleanly (`ReportDeviceUnavailable` path) with
    no crash and no force applied on reconnect until re-armed.
12. **Only after every step above has individually passed**, increase gain
    gradually — one small step at a time, re-confirming steps 6/7 still feel
    correct at each new gain level. Never jump directly to a "realistic"
    gain.

## Incident log

### Foreground acquisition investigation

The probe now accepts an explicit cooperative-level policy for real FFB
hardware-test modes. The first `DISCL_FOREGROUND` experiment is intentionally
stop-only and uses a valid top-level window that remains invisible and
unfocused:

```powershell
.\build\tools\device_probe\Release\rvwheel_device_probe.exe `
  --ffb-hw-test-stop-only `
  --ffb-cooperative-level foreground
```

**2026-08-09 — Unfocused foreground stop-only experiment: completed, no
physical change observed.** Exact command shown above, Release build. The
top-level diagnostic window was valid but deliberately invisible and was
confirmed not to equal `GetForegroundWindow()`. `SetCooperativeLevel` did
not report a failure, but the initial `Acquire()` and all ten retry attempts
failed immediately with **`0x80070005` = `DIERR_OTHERAPPHASPRIO`**. Input
was not readable (`connected=false`), no effect was created or started,
`StopForceFeedback()` returned `Ok`, and the diagnostic correctly exited
with code `1` because exclusive acquisition never succeeded. The operator
confirmed **no apparent movement, resistance change, or vibration**.

This confirms that a valid but unfocused top-level window cannot acquire the
G923 under `DISCL_EXCLUSIVE | DISCL_FOREGROUND`. It does not yet answer
whether a genuinely focused top-level window retains exclusivity beyond the
previous two-second failure point.

The next gated stop-only command shows a small tool window and verifies that
Windows actually granted it foreground ownership before touching DirectInput:

```powershell
.\build\tools\device_probe\Release\rvwheel_device_probe.exe `
  --ffb-hw-test-stop-only `
  --ffb-cooperative-level foreground-focused
```

**2026-08-09 — Focused foreground stop-only experiment: passed.** Exact
command shown above, Release build. The dedicated visible tool window was
confirmed as the exact `GetForegroundWindow()` HWND before DirectInput was
initialized. Initial `Acquire()` succeeded with
`DISCL_EXCLUSIVE | DISCL_FOREGROUND`; input remained readable and connected;
`StopForceFeedback()` returned `Ok`; and the diagnostic exited `0`. No effect
was created or started. The operator was not holding the wheel and reported
zero additional perceptible movement or behavior. The wheel's pre-existing
idle autocenter (it normally stays centered and returns toward center when
moved) remained, and is explicitly **not attributed to RVWheel**.

This proves foreground acquisition works on the real G923 while the owning
window has focus. The short check lasted only about 200 ms, so it does not
yet prove exclusivity remains beyond the prior ~2-second failure point.

### Five-second foreground retention probe

The focused stop-only mode holds the foreground window for five seconds,
polls input and `GetForceFeedbackState` throughout, and validates the actual
device-wide `DISFFC_STOPALL` HRESULT at the end. It still creates and starts
no effect. The exact command is unchanged from the successful short focused
test above, but must be run from the newly rebuilt binary. This retention
probe was executed only after a new explicit operator authorization.

Expected success: five seconds, every input poll readable, foreground held,
`GetForceFeedbackState` remaining successful, and final device-wide STOPALL
returning `Ok`. Any loss or HRESULT is a valid diagnostic result and blocks
the weak-effect foreground test.

**2026-08-09 — Five-second focused foreground retention: passed.** Release
build, same `foreground-focused` command. Initial acquisition succeeded;
the probe ran for **5.01 seconds** with **245/245 input polls readable**;
the diagnostic HWND retained foreground throughout; and
`GetForceFeedbackState()` remained successful, reporting
`ACTUATORSOFF | EMPTY | POWERON`. Final device-wide `DISFFC_STOPALL`
returned `Ok` and the process exited `0`. No effect was created or started.
The operator confirmed **no physical change beyond the pre-existing idle
autocenter**.

This is the first run to hold exclusive foreground access beyond the prior
~2-second background-exclusive failure point. It strongly supports the
cooperative-level hypothesis, but does not by itself prove that updating a
real effect remains stable; that requires a separately authorized weak-effect
run and must not be described as completed yet.

**2026-08-09 — Focused foreground weak spring: failed at the same ~2-second
boundary.** With separate explicit authorization, the Release diagnostic ran
one spring at fixed `gain=0.2`, `strength=0.2`, a slow ramp, and a five-second
limit under `DISCL_EXCLUSIVE | DISCL_FOREGROUND`. Creation/start succeeded and
the controller remained `Active` through approximately 2.0 seconds. It then
entered `Faulted`; subsequent spring `SetParameters` calls returned
**`0x80040205 = DIERR_NOTEXCLUSIVEACQUIRED`**, exactly as in the earlier
background-exclusive runs. This falsifies the proposed foreground-mode fix:
foreground ownership is sufficient to retain an idle exclusive acquisition,
but not sufficient to retain it while this real effect is being updated.

The run also exposed two diagnostic defects, now fixed before any further
hardware run: a faulted test returned process exit code `0`, and a failed stop
could re-arm the safety controller's stop edge on every tick, producing warning
spam. The test now stops at the first backend fault, reports focus/poll/fault
signals separately, preserves the first fault reason, and returns nonzero.
An automated regression test proves secondary failures while already
`Faulted` cannot generate repeated stop edges.

Post-run review against Microsoft's DirectInput contract found two additional
backend defects and fixed them before authorizing any repeat:

1. `DIEFFECT::lpvTypeSpecificParams` pointed at function-local
   `DICONDITION`/`DICONSTANTFORCE` values. DirectInput does not make a private
   copy and requires these buffers to remain valid for the effect lifetime, so
   the effect retained dangling pointers after the function returned. The
   buffers, axes, and directions now have device-owned lifetime and outlive
   their COM effect objects.
2. Every parameter update included `DIEP_START`, which explicitly restarts an
   already-playing effect, and unchanged commands were resent approximately
   60 times per second. Updates now use the minimal documented
   `DIEP_TYPESPECIFICPARAMS` flag and identical values are suppressed. A stopped
   effect is released and explicitly created/started on the next activation.

Both Debug and Release builds pass all 200 automated tests after these changes.
They are well-founded correctness fixes, but **their effect on the real
two-second failure is not yet known**. A new weak-spring run still requires
separate explicit authorization and observation.

**2026-08-09 — Focused weak spring after buffer/update fixes: active for five
seconds, unsafe refresh interaction identified; final stop failed.** The spring
controller remained `Active` for the full five-second window, with input polls
readable and foreground retained. The final device-wide STOPALL nevertheless
returned `0x80040205 = DIERR_NOTEXCLUSIVEACQUIRED`, so this run is a failure and
does not validate FFB.

The decisive diagnostic was a second
`Initial Acquire() succeeded with EXCLUSIVE | FOREGROUND` from the same process.
`CreateManager` uses a five-second discovery interval, and the weak-effect test
continued calling `RefreshIfDue()` while holding the effect. The initial device
was acquired before the three-second safety countdown; therefore the refresh
became due approximately two seconds after the effect began. Enumeration
constructed and exclusively acquired a duplicate DirectInput device before
`DeviceManager` could recognize its identical `DeviceId` and discard it. That
second acquisition revoked exclusivity from the original effect owner. The
timing identity (`3 s countdown + ~2 s active = 5 s refresh`) and the second
successful Acquire make this the confirmed cause of the previously mysterious
two-second downgrade.

The operator's physical observation matches that software timeline: resistance
became perceptibly **lower than the G923 baseline** for a short interval, then
rapidly returned to normal. The reduced resistance is consistent with this
test's device-wide `DIPROP_FFGAIN=0.2`; the rapid return is consistent with the
duplicate device's acquisition/destruction stopping the original effect near
the refresh boundary. The wheel was back to its normal baseline afterward.

The hardware diagnostic now performs initial discovery only and never
re-enumerates while its exclusive effect is active; `Poll()` still detects a
physical disconnect. A failed `GetForceFeedbackState()` is also latched and
causes the next apply call to fault immediately. The same ownership rule must
be enforced when FFB is eventually integrated into the production bridge:
hotplug discovery cannot acquire a duplicate device during an exclusive FFB
session. The correction has passed all automated tests but still requires a
new separately authorized G923 run before the incident can be closed.

**2026-08-09 — Corrected ownership lifecycle, weak spring validation run #1:
technical and physical pass.** With periodic rediscovery removed from the exclusive session,
the controller remained `Active` for the full five seconds. There was exactly
one successful exclusive foreground Acquire, input polls remained readable,
the diagnostic retained foreground throughout, final STOPALL returned `Ok`,
and the process exited `0`. No `DIERR_NOTEXCLUSIVEACQUIRED` occurred. This is
the first passing real-effect run after the ownership fix. The operator
confirmed the altered resistance remained present while the diagnostic window
was open and returned to the normal G923 baseline after the test stopped, with
no residual abnormal force. The incident is not yet considered closed until
consecutive authorized runs reproduce the pass.

**2026-08-09 — Step 4 (stop only): passed.** Real G923, exclusive
acquisition succeeded, input stayed readable, `StopForceFeedback()`
returned `Ok` with no effect ever created. No motion observed. See
[`docs/research/FORCE_FEEDBACK_FEASIBILITY.md`](research/FORCE_FEEDBACK_FEASIBILITY.md)
open question 2 — now resolved: exclusive FFB acquisition does not break
input polling on this device.

**2026-08-09 — Step 6 (weak spring, gain=0.1, strength=0.1): anomaly found,
no unsafe motion reported.** Real G923. The effect ramped up correctly at
first, but around t≈2.0s every subsequent `SetParameters`/`Stop` call
started failing (generic `BackendError` at the time, HRESULT not yet
captured), and the final `StopForceFeedback()` call also returned
`BackendError`. The operator disconnected the USB cable as a precaution.
**No unexpected motion, resistance, or vibration was reported** either
during or after the test; the wheel's centering behavior after
disconnecting was G HUB's own idle behavior, not an RVWheel-driven effect.

Two real bugs were found and fixed from this single run, before any retest:

1. `DirectInputDevice::ApplyForceFeedback` unconditionally called
   `ApplyConstantForce`/`ApplyDamper` even when those components were
   always zero and never requested, which silently created and started
   two extra zero-magnitude effects alongside the one real spring effect
   the operator asked for. Fixed: a channel is now only touched if it is
   genuinely nonzero or an effect for it already exists.
2. `ForceFeedbackSafetyController`'s internal ramp state initialized `gain`
   to `ForceFeedbackCommand`'s own default of `1.0` while
   `spring`/`damper`/`constantForce` initialized to `0`. Since
   `SpringDamperSource` always requests full-scale gain and relies on the
   controller to clamp it down, this meant **spring reached its (small)
   target quickly while gain was still ramping down from "full,"
   producing a stronger, uninted transient during the first ~1.6 seconds**
   before both converged to the configured low values. Fixed: the ramp
   state now starts at all-zero, including gain, so no field can ever lead
   another to an overshoot. A regression test now asserts the
   `spring * gain` product never exceeds its final steady-state value
   during ramp-up, at any slew rate.

**2026-08-09 — Step 6 retest (weak spring, with HRESULT diagnostics and
both fixes above applied): reproduced.** Same device, same command. Gain
now correctly ramped up from 0 in lockstep with spring (the overshoot bug
above did not recur), and only the spring effect was created (the
unrequested constant-force/damper effects were no longer created). Around
the same ~2 second mark, `SetParameters` started failing again, now with a
specific, confirmed HRESULT: **`0x80040205` = `DIERR_NOTEXCLUSIVEACQUIRED`
("The device is acquired, but not exclusively")**. The final
`StopForceFeedback()` also failed with `BackendError` for the same reason:
`IDirectInputEffect::Stop`/`SendForceFeedbackCommand` both require
exclusive acquisition per Microsoft's own documentation, so once
exclusivity is lost, this project's software-level stop calls cannot
succeed either. **No unsafe motion was reported after this run either**;
the wheel returned to G HUB's own idle behavior.

**2026-08-09 — Step 6 retest #3 (weak spring, G HUB closed): G HUB
hypothesis disproven.** `lghub_agent.exe` and `lghub_system_tray.exe` were
terminated (`lghub_updater.exe`, a background update checker, could not be
and was left running). Same command, same device. The `0x80040205` /
`DIERR_NOTEXCLUSIVEACQUIRED` failure recurred at the same elapsed time with
G HUB's main device-management process not running. **Logitech G HUB is
not the (sole) cause of the exclusive-acquisition downgrade.**

Remaining candidate causes at this point: (a) `DirectInputDevice::Poll()`'s
own `DIERR_INPUTLOST`/`DIERR_NOTACQUIRED` recovery path re-acquiring the
device without exclusive flags being what actually changes; (b) a
Windows-level or driver-level timeout specific to holding exclusive
force-feedback access without some form of periodic confirmation; (c)
`lghub_updater.exe` (left running) or another background process/service
still touching the device. No unsafe motion was reported in this run.

**2026-08-09 — Step 6 retest #4 (weak spring, gain raised to 0.2, with
`Poll()`-reacquire diagnostic logging): hypothesis (a) ruled out.** Same
command, gain/strength raised from 0.1 to 0.2 (still far below the safety
controller's 0.6 ceiling) after three prior runs at 0.1 showed no unsafe
motion. Zero "Input poll lost" diagnostic lines appeared during the run --
`GetDeviceState` never failed -- yet `SetParameters` still failed with the
same `DIERR_NOTEXCLUSIVEACQUIRED` at the same elapsed time. **This
project's own input-polling reacquire logic is not the cause**: the
exclusive FFB lock is being dropped independently of any input-read
failure ever occurring.

**2026-08-09 — Step 7 (weak damper, gain 0.2): new finding, working
hypothesis revised.** The operator reported that the wheel's own baseline
resistance (present even with no RVWheel code running at all, and
independent of G HUB, which was still closed) was felt to *decrease* for
about one second while the effect was active, then return once the
`DIERR_NOTEXCLUSIVEACQUIRED` failure hit. `DIPROP_FFGAIN`
(`DirectInputDevice::ApplyGain`) is a **device-wide** property -- it scales
every active force on the device, not only effects this project created.
This is consistent with the G923 (or its driver) already running some
ambient/default force feedback behavior independent of any PC software,
which this project's low test gain temporarily suppressed while exclusive
access lasted.

**Revised working hypothesis (not confirmed): the G923 may have a
firmware- or driver-level watchdog that reclaims the device's own default
force feedback behavior, including exclusive access, if no application
"renews" FFB activity within roughly two seconds.** This would explain the
timing, the `DIERR_NOTEXCLUSIVEACQUIRED` failure, and the ambient-effect
observation together, and -- if true -- is reassuring rather than
concerning: it would mean the hardware has its own independent safety
fallback on top of this project's software safety controller. This has not
been confirmed against official documentation (none was found describing
G923 firmware behavior at this level) and would require USB/HID-level
tracing to verify. Diagnosing further is not currently planned; see the
main text above for what would be needed.

### Background-mode confirmation (continuation session)

The ownership-lifecycle fix (no `RefreshIfDue()` while an exclusive effect
is active) was validated above only under
`DISCL_EXCLUSIVE | DISCL_FOREGROUND`. The practical goal is
`DISCL_EXCLUSIVE | DISCL_BACKGROUND` — the mode that lets an external
bridge hold FFB while the actual game window has focus — which had never
been retested after the fix. Before touching hardware, a fresh session
independently re-verified the current repository state (`git log`,
`git status`) rather than trusting this document's own claims, then
rebuilt and re-ran the full suite: **Release 200/200, Debug 200/200, both
with zero new warnings.** `--list` reconfirmed the G923 with
`forceFeedback=1`. Then, inspected the actual `RunFfbHardwareTestWeakEffect`
loop in `DeviceProbeApp.cpp` and confirmed directly in code (not just from
this document) that `manager->RefreshIfDue()` is called exactly once,
before the loop, never inside it while the exclusive effect is active.

**2026-08-09 — Background validation run #1 (weak spring, gain=0.2,
`--ffb-cooperative-level background`): technical and physical pass.**
Exact command:

```powershell
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --ffb-hw-test-weak-effect --effect spring --ffb-cooperative-level background
```

`[dal-info] Initial Acquire() succeeded with EXCLUSIVE | BACKGROUND` printed
exactly once. `state=Active` held continuously from t=0.2s through t=4.9s
with no `Faulted`, no `DIERR_NOTEXCLUSIVEACQUIRED`. `Input polls remained
readable: yes`. `Final StopForceFeedback(): Ok`. Exit code `0`. The operator
was not watching the console (both hands on the wheel) and reported: wheel
resistance was noticeably *lower* than the G923's own baseline for the
duration of the effect (consistent with the already-documented device-wide
`DIPROP_FFGAIN` interaction), returned to normal after the test ended, and
**no abnormal vibration, resistance, or behavior remained**.

**2026-08-09 — Background validation run #2 (same command, new explicit
authorization): technical and physical pass.** Identical outcome: single
`Initial Acquire() succeeded with EXCLUSIVE | BACKGROUND`, `Active` for the
full 5.0s window, no fault, no `DIERR_NOTEXCLUSIVEACQUIRED`, input readable
throughout, `Final StopForceFeedback(): Ok`, exit code `0`. Operator again
reported lower-than-baseline resistance during the run only, with nothing
abnormal afterward.

**Conclusion for this specific diagnostic**: the exclusive-access loss that
made `DISCL_EXCLUSIVE | DISCL_BACKGROUND` fail after ~2 seconds is fixed by
the same ownership-lifecycle correction that fixed foreground mode — it was
never a foreground/background distinction, only the periodic re-enumeration
bug. Background mode is now confirmed stable for a 5-second weak spring
effect across two consecutive authorized runs, on top of the prior
foreground-focused pass. This is still only the weak-spring/damper
diagnostic; it does **not** validate vehicle telemetry, gameplay
integration, or any gain/effect beyond what was actually tested here.

### Bridge integration first physical test

All prior entries above exercised the standalone `--ffb-hw-test-*`
diagnostics only. This entry is the first physical test of the actual
`--bridge --enable-force-feedback` integration path
(`BridgeForceFeedbackSession`), run after a hardening pass added: an
optional `--duration` bound so the run does not depend solely on Ctrl+C;
gating `Enable()` on the device reporting `connected && valid &&
readiness == Ready` (instead of arming immediately once the profile
resolved); a structured, observable result from `Stop()`
(`BridgeForceFeedbackStopResult`) instead of a discarded one; and
correcting the shipped profile's `slewRatePerSecond` to the actually
-validated `0.5` (it had been left at the struct default `2.0`, never
itself physically tested). Before this run: Debug and Release both
rebuilt clean, 221/221 tests passing in both, `--ffb-simulate` and
`--list` reconfirmed safe/read-only, `git diff --check` clean.

**2026-08-09 — Bridge, limited duration, weak spring (gain=0.2, slew=0.5/s):
technical and physical pass.** A profile copy with
`forceFeedback.enabled: true` and the validated values (`masterGain`/
`springStrength`/`maxTorqueNormalized` = `0.2`, `damperStrength` = `0.0`,
`slewRatePerSecond` = `0.5`, `watchdogTimeoutMilliseconds` = `200`) was
placed under an isolated `--profiles-dir` outside the repository
(`$env:TEMP\RVWheel-ffb-test\profiles`), never touching the real
`%LOCALAPPDATA%\RVWheel\profiles` or the shipped built-in JSON (which
still ships with `enabled: false`). Exact command:

```powershell
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --bridge --enable-force-feedback --profiles-dir "C:\Users\gugam\AppData\Local\Temp\RVWheel-ffb-test\profiles" --duration 5 --rate 60
```

`[dal-info] Initial Acquire() succeeded with EXCLUSIVE | BACKGROUND`
printed once. `Force feedback: armed (...)` printed immediately, followed
by the one-time `waiting for the device to report connected+valid+Ready`
message -- the operator then moved and released one wheel axis, and only
then did `Force feedback: device readiness reached; engine ENABLED.`
print, confirming the new readiness gate actually deferred `Enable()`
rather than arming at profile-resolution time.
`[dal-info] GetForceFeedbackState` showed the expected
`ACTUATORSOFF|EMPTY|POWERON` -> `ACTUATORSON|POWERON` transition. No fault
was reported at any point. After 5.0s, the run stopped itself
(`Stopping bridge (requested --duration elapsed)...`) without any Ctrl+C —
the new `--duration` path. `Force feedback engine stop signal: yes`,
`Final StopForceFeedback(): Ok`, exit code `0`. 300 polls at 60 Hz over
~5s, 0 publish failures. The operator (hands on the wheel throughout)
reported: resistance appeared **light**, stayed **stable** for the
duration, and stopped with **nothing abnormal** afterward.

**Conclusion for this specific test**: the hardened bridge integration
(readiness-gated `Enable()`, bounded `--duration`, confirmed `Stop()`)
behaves correctly end-to-end on real hardware for the same weak-spring
diagnostic already validated standalone, now delivered through
`--bridge` while input kept publishing normally throughout. This is one
run, one operator, one device, `springStrength` only (`damperStrength`
still `0.0`, still unvalidated); it does **not** validate multiple
consecutive runs through the bridge specifically, reconnect behavior, a
fault-recovery path in this integration, or anything inside the actual
game/UE4SS mod.

### First in-game physical test (launcher opt-in)

All prior entries ran the standalone probe/bridge directly. This entry is
the first test of force feedback delivered through the actual
`rvwheel_launcher.exe` player path, after adding: `--enable-force-feedback`/
`--profiles-dir <path>` launcher opt-ins (both off/absent by default --
`rvwheel_launcher.exe` with no arguments is unchanged); pure, tested
argument parsing and bridge-command-line construction
(`ParseLauncherArgs`/`BuildBridgeCommandLine` in `LauncherCore.hpp/.cpp`);
and a fail-closed check that refuses (rather than silently reusing or
killing) an already-running bridge process when `--enable-force-feedback`
is requested. Before this run: confirmed via `Get-Process` that no
`rvwheel_device_probe.exe`, `rvwheel_launcher.exe`, or
`Ride-Win64-Shipping.exe` was already running; Debug and Release both
rebuilt clean, 231/231 tests passing in both (0 hardware/game access from
the suite), `git diff --check` clean. A profile copy with the same
`axes`/`readiness` as the real, calibrated user profile
(`%LOCALAPPDATA%\RVWheel\profiles\logitech-g923-ps-pc-directinput.json`,
read but never modified) plus the validated `forceFeedback` block was
placed under an isolated `--profiles-dir`
(`$env:TEMP\RVWheel-ffb-game-test\profiles`).

**2026-08-09 — Launcher, in-game, weak spring (gain=0.2, slew=0.5/s):
technical and physical pass.** Exact command:

```powershell
& ".\build\tools\launcher\Release\rvwheel_launcher.exe" `
  --enable-force-feedback `
  --profiles-dir "C:\Users\gugam\AppData\Local\Temp\RVWheel-ffb-game-test\profiles"
```

The launcher installed/updated the mod, started the bridge with the
opt-in flags forwarded, and opened the game through Steam; the operator
then drove normally. From `bridge.log`:
`[dal-info] Initial Acquire() succeeded with EXCLUSIVE | BACKGROUND`,
`Force feedback: armed (...)`, then the one-time waiting-for-readiness
message, then -- after the operator moved and released the wheel in-game
-- `Force feedback: device readiness reached; engine ENABLED.` and
`GetForceFeedbackState` showing `ACTUATORSOFF|EMPTY|POWERON` ->
`ACTUATORSON|POWERON`. No `Force feedback backend faulted` line at any
point (one unrelated, non-FFB `failed to publish bridge frame` warning
occurred and self-recovered, as designed). Closing the game ended the
launcher's `WaitForSingleObject`, which ended the launcher process, which
the bridge's `--parent-pid` supervision detected: `Stopping bridge
(supervised parent process exited)...`, `Force feedback engine stop
signal: yes`, `Final StopForceFeedback(): Ok`. Launcher exit code `0`;
6577 bridge polls, 11 transient publish-frame failures (unrelated to
force feedback). This is the first time the parent-exit stop path (as
opposed to Ctrl+C or `--duration`) was exercised against real hardware.

The operator (playing normally, hands on the wheel) reported: steering,
throttle, brake, clutch, and the H-pattern shifter all worked correctly
throughout; the wheel felt **lighter** during the session (the weak
centering spring, consistent with prior standalone results); it **stayed
stable, with no oscillation, vibration, or unexpected force**; and
resistance **returned to normal** immediately after the game closed.

**Conclusion for this specific test**: force feedback now works
end-to-end through the actual player-facing launcher path, inside a real
game session, without disrupting steering/pedal/clutch/shifter input,
and stops safely and confirmedly when the game is closed normally. This
is one run, one operator, one device, one play session, spring only
(`damperStrength` still `0.0`, still unvalidated); it does **not**
validate multiple consecutive in-game sessions, a mid-session
disconnect/reconnect, a fault-recovery path while driving, or the
launcher's fail-closed check against an actually-conflicting running
bridge (that specific branch is unit-tested, not hardware-tested, since
triggering it deliberately would mean starting a second, uncontrolled
bridge instance). Vehicle telemetry (speed, terrain, collisions) is still
entirely unimplemented; this remains a static centering spring
regardless of what the vehicle is doing.

### Speed-sensitive spring: first physical test, and native autocenter follow-up

All prior entries used a static, telemetry-independent spring
(`SpringDamperSource`). This entry covers the first hardware test of
`SpeedSensitiveSpringSource` (opt-in via
`forceFeedback.speedSensitiveSpring.enabled`), which scales the same
already-validated spring by vehicle speed (RVT1 telemetry) via a
smoothstep curve, through the same `--bridge --enable-force-feedback`
path, with an isolated profile copy outside the repository (never
touching the shipped built-in JSON, which still ships with
`speedSensitiveSpring.enabled: false`) and the game opened directly via
Steam (not the launcher, so the operator could pass a specific
`--profile` file). Before these runs: Debug and Release both rebuilt
clean, 291/291 tests passing in both, `git diff --check` clean.

**Run 1 (20 s, `minimumScale=0.25`): no force ever applied.** The
device's readiness policy (`requireAxisActivation`) was never satisfied
within the run -- `"Force feedback: device readiness reached; engine
ENABLED."` never printed, so `Enable()` was never called and the wheel
felt unchanged throughout. A real bug was also found and fixed here,
independent of this readiness gap: the throttled (`<=4 Hz`) adaptive
diagnostic line never printed at all, due to
`std::chrono::steady_clock::time_point::min()` being subtracted from
`now()` for the "never printed yet" check, overflowing
`steady_clock::duration`'s representable range (undefined behavior).
Fixed by using `std::optional<time_point>` instead (the same pattern
`RunTelemetryMonitor` already used for an analogous "never observed
yet" case).

**Run 2 (30 s, `minimumScale=0.25`): technical and physical pass.** The
operator moved the wheel promptly; readiness was reached partway through
and the engine enabled. The printed diagnostic tracked a full drive: spring
≈0.05 at rest (matching `springStrength(0.2) * minimumScale(0.25)`),
smoothly increasing with speed, `scale=1`/`spring=0.2` sustained from
≈5.5 m/s through a peak of ≈8.1 m/s (never exceeding `springStrength`),
and back down to ≈0.05 on deceleration -- both directions of the curve
confirmed on real hardware, not just in unit tests. Clean, confirmed
stop at the end. The operator reported the pattern matched what was felt,
but raised a design objection: even the resting `minimumScale=0.25`
spring actively pulled the wheel toward center while the vehicle was
stationary, which does not match a real car (a parked car's wheel has
no active centering torque, only mechanical friction).

**Run 3 (30 s, `minimumScale=0.0`): curve confirmed, objection not resolved.**
No code change was needed -- `minimumScale` is already a validated,
configurable field, so the isolated test profile was edited to `0.0` and
reused. The diagnostic confirmed `spring≈0` at rest (values on the order
of `1e-5`-`1e-7`, i.e. a DirectInput coefficient of 0 after truncation)
and the same full-strength/no-overshoot behavior at speed. The operator
reported the wheel still returned to center at a standstill, unchanged
from before.

**Effect-retirement experiment (30 s, `minimumScale=0.0`,
`DirectInputDevice` temporarily modified): hypothesis tested and
disproven.** Before concluding this was a hardware characteristic, one
remaining software-side hypothesis was tested: that an *active* DirectInput
spring condition effect with a coefficient truncating to 0 might behave
differently from *no effect object existing at all* on this wheelbase.
`DirectInputDevice::ApplyConditionEffect` was temporarily changed to
`Stop()` and release the spring/damper effect entirely below a
`0.001`-normalized-strength threshold, recreating it once strength rose
back above that threshold (previously: created once, then always updated
in place via `SetParameters`, never released until session end). Debug
and Release rebuilt clean, 291/291 tests unaffected (this backend has no
hardware-independent coverage either way -- see below). The operator
drove near-standstill for the whole run (`springRequested`/`springApplied`
stayed below the retirement threshold throughout, so the spring effect
was destroyed for nearly the entire 30 s) and actively turned the wheel
to check: **no difference from the wheel's standard behavior was felt.**
The change was reverted immediately after (`git checkout --` on
`DirectInputDevice.cpp`/`.hpp`) since it added a new effect-lifecycle
path (destroy/recreate, distinct from "create once, then update in
place") without any confirmed benefit.

**Narrow conclusion from those runs:** the resting resistance is not produced
by RVWheel's `DICONDITION` spring. Three points of evidence support that
limited claim:
1. DirectInput's own documented condition-effect formula
   (`force = coefficient * (position - offset)`) means a coefficient of
   0 mathematically produces 0 force; there is no evidence of an error in
   this project's `[0,1] -> DICONDITION` coefficient conversion.
2. This document's own earlier entries (Step 7, and the background
   validation runs) already recorded "the wheel's own baseline
   resistance (present even with no RVWheel code running at all, and
   independent of G HUB, which was still closed)" and operators
   consistently describing the spring as "lighter," never "absent."
3. Today's dedicated experiment -- destroying the effect object itself,
   not just lowering its coefficient -- changed nothing the operator
   could feel.
That evidence did **not** justify the former broader conclusion that no
DirectInput control could remove it. Microsoft's official documentation
defines a separate device-wide property, `DIPROP_AUTOCENTER`, explicitly says
an application using force feedback should disable it before playing effects,
and distinguishes it from an application's own spring effect. Microsoft also
documents that it must be changed while the device is unacquired (only
`DIPROP_FFGAIN` is writable while acquired). The official FFConst sample sets
AUTOCENTER=OFF after configuring the cooperative level and before acquisition/effect
playback. Sources:
[SetProperty](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417929(v=vs.85)),
[Device Properties](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416595(v=vs.85)), and
[official FFConst sample](https://github.com/walbourn/directx-sdk-samples-reworked/blob/main/DirectInput/FFConst/ffconst.cpp).

RVWheel now models that as an ownership-session lifecycle: on the real
`BridgeForceFeedbackSession::Enable()` boundary, DirectInput unacquires,
snapshots `DIPROP_AUTOCENTER`, requests OFF, reads it back, then reacquires.
Transient watchdog/effect stops do not restore it. Final session teardown
stops effects, restores the exact snapshot while unacquired, and releases
exclusive ownership. Failure to begin leaves the engine disabled; failure to
restore makes shutdown explicitly unconfirmed. The generic lifecycle and its
error/idempotency behavior are unit-tested without COM; the actual property
call remains hardware-only.

**Isolated native-autocenter experiment (2026-08-10): physically no effect on
this G923.** The mode below creates no `IDirectInputEffect` and never calls
`ApplyForceFeedback`; it provides a three-second baseline, requests
AUTOCENTER=OFF for five seconds, restores the prior value, and exits.

```powershell
& ".\build\tools\device_probe\Release\rvwheel_device_probe.exe" `
  --ffb-hw-test-autocenter `
  --ffb-cooperative-level background
```

The command was run twice under `EXCLUSIVE | BACKGROUND`, with separate
operator authorization. Both executions passed technically:

- `DIPROP_AUTOCENTER` read as ON, changed to OFF, and read back as OFF;
- the exclusive device was reacquired and input remained readable (245 polls
  in the first run, 244 in the second, both over 5.01 seconds);
- `GetForceFeedbackState` reported `ACTUATORSOFF | EMPTY | POWERON`, confirming
  no effect was loaded or playing;
- `EndForceFeedbackSession(): Ok`, and the prior ON value was restored with
  read-back confirmation; both processes exited `0`.

The first run had no usable physical observation because the operator did not
see that the short window had begun. The immediately repeated run had a valid
observation: the operator reported **absolutely no perceptible change** during
or after the five-second OFF window. No self-motion, vibration, oscillation, or
abnormal residual force was reported.

Therefore the API/lifecycle experiment is a **technical pass but a physical
failure for its intended G923 effect**. This driver accepts and reports the
property transition without changing the wheel's resting centering resistance.
Combined with the zero-coefficient and effect-retirement runs above, the
remaining resistance is not an RVWheel spring effect and is not removed by
DirectInput's documented native-autocenter property on this G923/G HUB stack.
The exact lower layer responsible (G HUB driver state, firmware, or another
device behavior) remains unknown; this result does not identify it.

The lifecycle remains implemented because it follows Microsoft's documented
FFB setup contract and may prevent double-centering on other wheel drivers.
It must not be described as a G923 standstill-resistance fix. Future wheel
profiles/validation should record whether the property has a physical effect
per model rather than assuming all DirectInput wheels behave like this one.

For future hardware, technical pass requires the diagnostic line
`DIPROP_AUTOCENTER changed ON -> OFF ... (read-back confirmed)`, readable
input throughout, `EndForceFeedbackSession(): Ok`, and
`Restored pre-session DIPROP_AUTOCENTER value`. A warning that the property
is unsupported, ineffective, or not confirmed makes the result inconclusive,
even if exit code is zero. Physical pass requires noticeably lower/no
return-to-center resistance **only during the five-second window**, the prior
feel returning immediately afterward, and absolutely no self-motion,
oscillation, or vibration. Stop immediately if the wheel moves by itself.
This command still changes a real device-wide motor-control property and must
not be run without explicit per-run authorization. It should not be repeated
on this G923 merely to seek a different subjective result; the controlled test
already answered the question.

### Logitech G923 firmware-autocenter experiment

The DirectInput property experiment above established that this G923 driver
reports an ineffective state change. Read-only HID inspection then identified
the exact G923 PS/PC control collection (`046D:C266`, usage page/usage
`0001/0004`, report ID `0`, 16-byte output payload). The classic Logitech
firmware commands were restricted to that identity and full layout; no other
device can enter this diagnostic.

**2026-08-10 — control-transfer attempt: rejected, no physical change.** The
first separately authorized run used `HidD_SetOutputReport`. The exact device
was found, but the OFF request failed immediately with Win32 error 31; no
five-second window occurred. The operator confirmed normal resistance. The
path was replaced, not retried, because Windows HID also exposes the output
endpoint through `WriteFile` and the device had rejected the control transfer.
The failure path was hardened to attempt an immediate best-effort restore even
when OFF is not confirmed.

**2026-08-10 — HID `WriteFile`, five-second OFF/restore: technical and physical
pass.** With new explicit authorization, the exact command was:

```powershell
& ".\build\tools\device_probe\Release\rvwheel_device_probe.exe" `
  --ffb-hw-test-logitech-g923-autocenter
```

The OFF report was accepted, the complete five-second window elapsed, the ON
restore report was accepted, and the process exited `0`. The operator reported
that the wheel **stopped returning to center completely during the window**
and **returned to its normal centering behavior as soon as the timer ended**.
No residual abnormal behavior was reported. This directly answers the
standstill-resistance question for this exact hardware/driver stack.

The validated controller is now wired into the production FFB ownership
lifecycle for `046D:C266`: begin is fail-closed if firmware OFF is not
confirmed; DirectInput-acquire failure triggers an immediate restore attempt;
final end restores firmware ON and makes any failure part of the bridge's
unconfirmed-shutdown result.

**2026-08-10 — first production in-game run: partial pass, ordering hypothesis
raised.** The launcher/bridge used the same isolated test profile with
`minimumScale=0.0`. Steering, speed telemetry, and the speed-sensitive spring
worked: resistance increased as the vehicle accelerated. At a standstill,
however, the operator reported that the wheel still returned to center. The
bridge log showed that the accepted Logitech firmware OFF report was followed
by DirectInput `Acquire()`. Shutdown was clean and confirmed both the
DirectInput-property restore and the Logitech firmware ON restore; the wheel
had no abnormal residual behavior after the game closed.

This did not contradict the isolated `WriteFile` pass: that diagnostic never
acquired DirectInput or created an effect after sending OFF. The production
lifecycle was changed experimentally to acquire the exclusive DirectInput
device first and send the exact validated firmware OFF report afterward. Both
Debug and Release rebuilt without warnings, and 298/298 tests passed in each
configuration before a new separately authorized run.

**2026-08-10 — OFF after DirectInput acquire: speed curve passed, standstill
autocenter still failed.** The log confirmed the intended sequence:
`DIPROP_AUTOCENTER ... OFF`, then `Logitech G923 firmware autocenter disabled
after DirectInput Acquire via HID WriteFile`, then the FFB ownership session
and engine became active. Telemetry showed requested/applied spring values
near zero at rest, and the operator confirmed that resistance increased as the
vehicle accelerated. Nevertheless, the wheel still returned to center while
the vehicle was stopped. The ordering change alone was therefore disproven as
a production fix.

**2026-08-10 — concurrent five-second re-send: blocked and physically
ineffective.** With separate authorization and the game/bridge still active,
a second diagnostic process attempted the otherwise validated five-second HID
OFF/restore sequence. It did not complete within the bounded observation
period, emitted no confirmation before termination, and the operator reported
that the wheel continued returning to center throughout. The extra process was
terminated and confirmed absent; the main bridge remained healthy. Because
the output was buffered, this run cannot identify which individual write
blocked, but it does establish that this concurrent two-writer arrangement is
not operationally safe or useful.

**Conclusion:** the raw firmware command is a valid isolated diagnostic, not a
production supplement to DirectInput FFB. Its bridge hook was removed. The
diagnostic now fails closed whenever another `rvwheel_device_probe` process is
running, preventing a repeat of the known contention. The clean future design
for `046D:C266` is a single Logitech output backend that owns both effects and
autocenter while DirectInput remains input-only. That backend is not yet
implemented or claimed as validated.

## Recording results

For each step, record: date, exact command/config used, hardware
(manufacturer/model/VID:PID), what you observed, and pass/fail. Add this to
[FORCE_FEEDBACK.md](FORCE_FEEDBACK.md)'s status table (or a dedicated
results log) once real steps have actually passed — do not mark force
feedback "validated" anywhere in this repository's documentation until
this full procedure has been run and recorded.
