#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "rvwheel/dal/WheelTypes.hpp"
#include "rvwheel/ffb/ForceFeedbackTypes.hpp"

namespace rvwheel::ffb {

// What the caller (ForceFeedbackEngine) must do as a result of one
// Update()/Tick() call. The two actions are independent so a caller never
// has to guess: `applyCommand` says "call IWheelDevice::ApplyForceFeedback
// with this value", `stopDevice` says "call IWheelDevice::StopForceFeedback
// now", and either, both, or neither may be set on a given call.
// `stopDevice` is edge-triggered: it is true exactly once per
// transition into a stopped condition, never repeated on every idle tick,
// so a caller that always obeys it can never "forget" to stop but also
// never spams StopForceFeedback needlessly.
struct ForceFeedbackDecision {
    bool applyCommand = false;
    rvwheel::dal::ForceFeedbackCommand command{};
    bool stopDevice = false;
};

// The single point every force feedback command must pass through before
// reaching a real device. Independent of any backend, mixer, or telemetry
// source -- it only ever sees already-computed ForceFeedbackCommand values
// and decides whether/how much of them may actually reach hardware.
//
// Responsibilities (see docs/FORCE_FEEDBACK.md for the full state diagram):
//   - starts Disabled and stays there until Enable() is called explicitly;
//   - rejects NaN/Inf and clamps every field to its documented domain;
//   - enforces a hard, config-independent absolute ceiling on top of
//     whatever a profile's maxTorqueNormalized requests;
//   - rate-limits how fast the applied command may change (slew rate),
//     so enabling/disabling and any step change in requested force is
//     always a ramp, never a jump;
//   - runs a watchdog: if Update() is not called with fresh data within
//     the configured timeout, the next Tick() forces a ramp-down to zero
//     and a StopForceFeedback(), even if the caller keeps calling Tick()
//     with no new data at all;
//   - enters Faulted on a reported backend error and stays there,
//     producing only stop decisions, until ClearFault() is called.
//
// Never throws. Never itself touches a device, DirectInput, or Lua -- it
// only classifies numbers and time.
class ForceFeedbackSafetyController {
public:
    using Clock = std::chrono::steady_clock;

    explicit ForceFeedbackSafetyController(ForceFeedbackConfig config = {}) noexcept;

    // Replaces the tuning/limit configuration. Safe to call at any time,
    // including while Active; takes effect on the next Update()/Tick().
    void Configure(const ForceFeedbackConfig& config) noexcept;

    // Disabled -> Armed. No-op from Armed/Active/Stopping. From Faulted,
    // this is a no-op: a fault must be cleared explicitly via ClearFault()
    // first, so enabling can never silently paper over an unresolved error.
    void Enable() noexcept;

    // Any state -> Disabled, after one final ramp-down decision. This is
    // the ordinary "turn FFB off" path (profile toggle, user preference).
    void Disable() noexcept;

    // Any state, including Faulted -> Disabled immediately, with an
    // instantaneous (not ramped) stop decision on the very next call.
    // Distinguished from Disable() specifically for a single, unambiguous
    // call site an operator or a hardware test procedure can trust to cut
    // output right now.
    void EmergencyStop() noexcept;

    // Marks the controller Faulted: only ClearFault() (Faulted -> Disabled)
    // can leave this state. Call when the backend reports BackendError.
    void ReportBackendFailure(std::string reason) noexcept;

    // Call when the device is known-bad right now (disconnected, poll
    // failure) even before the watchdog would otherwise notice. Unlike
    // ReportBackendFailure, this is an expected/recoverable condition:
    // it forces an immediate stop but returns to Armed, not Faulted, so
    // input coming back on reconnect can resume without a manual
    // ClearFault().
    void ReportDeviceUnavailable() noexcept;

    void ClearFault() noexcept;

    // Feeds a newly computed command. `telemetryTimestamp` is the time the
    // data the command was computed from was captured (not `now`); passing
    // a value older than the watchdog timeout is treated the same as no
    // update at all for freshness purposes, even though the command itself
    // still gets clamped/slew-limited if it is otherwise the freshest data
    // this call has seen.
    [[nodiscard]] ForceFeedbackDecision Update(const rvwheel::dal::ForceFeedbackCommand& requested,
                                                Clock::time_point telemetryTimestamp, Clock::time_point now) noexcept;

    // Must be called periodically even when no new command is available
    // (e.g. every engine tick), so the watchdog can fire on its own even if
    // the caller/telemetry source stalls completely.
    [[nodiscard]] ForceFeedbackDecision Tick(Clock::time_point now) noexcept;

    [[nodiscard]] ForceFeedbackState State() const noexcept { return state_; }
    [[nodiscard]] ForceFeedbackDiagnostics Diagnostics(Clock::time_point now) const noexcept;

    // Hard ceilings no ForceFeedbackConfig can exceed, regardless of what a
    // profile requests. Defense in depth: a malformed or overly aggressive
    // profile can only ever make output weaker/slower than these, never
    // stronger/faster/longer-lived-without-a-heartbeat.
    static constexpr float kAbsoluteMaxTorqueNormalized = 0.6f;
    static constexpr float kMinSlewRatePerSecond = 0.05f;
    static constexpr float kMaxSlewRatePerSecond = 20.0f;
    static constexpr std::chrono::milliseconds kAbsoluteMaxWatchdogTimeout{500};

private:
    // Ramps appliedCommand_ toward `target` (slew-limited) and, once it
    // actually reaches all-zero, delivers the single pending stop edge (if
    // any) and settles state_ into targetIdleState_. This is the one place
    // that turns "what we'd like to send" into "what we're actually allowed
    // to send this tick" -- every public entry point funnels through it.
    [[nodiscard]] ForceFeedbackDecision SettleTowardTarget(const rvwheel::dal::ForceFeedbackCommand& target,
                                                            Clock::time_point now) noexcept;
    [[nodiscard]] rvwheel::dal::ForceFeedbackCommand ClampToLimits(const rvwheel::dal::ForceFeedbackCommand& raw) const noexcept;
    [[nodiscard]] rvwheel::dal::ForceFeedbackCommand SlewLimit(const rvwheel::dal::ForceFeedbackCommand& target,
                                                                Clock::time_point now) noexcept;

    ForceFeedbackConfig config_{};
    ForceFeedbackState state_ = ForceFeedbackState::Disabled;
    ForceFeedbackState targetIdleState_ = ForceFeedbackState::Disabled; // Where SettleTowardTarget lands once zero+edge fire.
    bool pendingStopDeviceEdge_ = false;

    // Last command actually returned via applyCommand -- and the ramp
    // origin SlewLimit steps from. Explicitly all-zero INCLUDING gain: the
    // aggregate-init `{}` would instead pick up ForceFeedbackCommand::gain's
    // own default of 1.0, which meant the very first activation ramped
    // spring/damper up to their target quickly while gain was still
    // ramping down from "full" -- a real, stronger-than-configured
    // transient window found during the first hardware test. Zero here is
    // the one starting point that can never overshoot regardless of how
    // any other field ramps.
    rvwheel::dal::ForceFeedbackCommand appliedCommand_{0.0f, 0.0f, 0.0f, 0.0f};
    std::optional<Clock::time_point> lastAppliedAt_;
    std::optional<Clock::time_point> lastTelemetryTimestamp_;
    std::optional<Clock::time_point> lastUpdateCallAt_;

    std::uint64_t updateCount_ = 0;
    std::uint64_t watchdogStopCount_ = 0;
    std::string lastFaultReason_;
};

} // namespace rvwheel::ffb
