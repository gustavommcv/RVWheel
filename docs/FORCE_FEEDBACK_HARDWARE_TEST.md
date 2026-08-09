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

## Recording results

For each step, record: date, exact command/config used, hardware
(manufacturer/model/VID:PID), what you observed, and pass/fail. Add this to
[FORCE_FEEDBACK.md](FORCE_FEEDBACK.md)'s status table (or a dedicated
results log) once real steps have actually passed — do not mark force
feedback "validated" anywhere in this repository's documentation until
this full procedure has been run and recorded.
