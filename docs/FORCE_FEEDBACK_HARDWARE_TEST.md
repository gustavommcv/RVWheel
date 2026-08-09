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

## Recording results

For each step, record: date, exact command/config used, hardware
(manufacturer/model/VID:PID), what you observed, and pass/fail. Add this to
[FORCE_FEEDBACK.md](FORCE_FEEDBACK.md)'s status table (or a dedicated
results log) once real steps have actually passed — do not mark force
feedback "validated" anywhere in this repository's documentation until
this full procedure has been run and recorded.
