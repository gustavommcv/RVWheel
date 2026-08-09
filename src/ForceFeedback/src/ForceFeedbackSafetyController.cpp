#include "rvwheel/ffb/ForceFeedbackSafetyController.hpp"

#include <algorithm>
#include <cmath>

namespace rvwheel::ffb {

namespace {

using rvwheel::dal::ForceFeedbackCommand;

constexpr float kZeroEpsilon = 1.0e-4f;

[[nodiscard]] float SanitizeAndClamp(float value, float lo, float hi) noexcept {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, lo, hi);
}

[[nodiscard]] bool IsAllZero(const ForceFeedbackCommand& command) noexcept {
    return std::fabs(command.constantForce) < kZeroEpsilon && command.spring < kZeroEpsilon &&
           command.damper < kZeroEpsilon;
}

[[nodiscard]] constexpr ForceFeedbackCommand ZeroCommand() noexcept {
    return ForceFeedbackCommand{0.0f, 0.0f, 0.0f, 0.0f};
}

} // namespace

ForceFeedbackSafetyController::ForceFeedbackSafetyController(ForceFeedbackConfig config) noexcept {
    Configure(config);
}

void ForceFeedbackSafetyController::Configure(const ForceFeedbackConfig& config) noexcept {
    config_ = config;
}

void ForceFeedbackSafetyController::Enable() noexcept {
    if (state_ == ForceFeedbackState::Faulted) {
        return; // A fault must be cleared explicitly; Enable() never papers over it.
    }
    if (!config_.enabled) {
        return; // The profile's own static switch is off; runtime Enable() cannot override it.
    }
    if (state_ != ForceFeedbackState::Disabled) {
        return; // Already armed/active/stopping.
    }
    state_ = ForceFeedbackState::Armed;
    targetIdleState_ = ForceFeedbackState::Armed;
}

void ForceFeedbackSafetyController::Disable() noexcept {
    if (state_ == ForceFeedbackState::Disabled || state_ == ForceFeedbackState::Faulted) {
        return; // Already inert, or a fault takes precedence -- use ClearFault() first.
    }
    pendingStopDeviceEdge_ = true;
    targetIdleState_ = ForceFeedbackState::Disabled;
    state_ = ForceFeedbackState::Stopping;
}

void ForceFeedbackSafetyController::EmergencyStop() noexcept {
    appliedCommand_ = ZeroCommand();
    lastAppliedAt_.reset();
    lastFaultReason_.clear();
    pendingStopDeviceEdge_ = true;
    targetIdleState_ = ForceFeedbackState::Disabled;
    state_ = ForceFeedbackState::Disabled;
}

void ForceFeedbackSafetyController::ReportBackendFailure(std::string reason) noexcept {
    lastFaultReason_ = std::move(reason);
    appliedCommand_ = ZeroCommand();
    lastAppliedAt_.reset();
    pendingStopDeviceEdge_ = true;
    targetIdleState_ = ForceFeedbackState::Faulted;
    state_ = ForceFeedbackState::Faulted;
}

void ForceFeedbackSafetyController::ReportDeviceUnavailable() noexcept {
    if (state_ == ForceFeedbackState::Disabled || state_ == ForceFeedbackState::Faulted) {
        return;
    }
    appliedCommand_ = ZeroCommand();
    lastAppliedAt_.reset();
    pendingStopDeviceEdge_ = true;
    targetIdleState_ = ForceFeedbackState::Armed; // Recoverable: resume on reconnect without a manual clear.
    state_ = ForceFeedbackState::Stopping;
}

void ForceFeedbackSafetyController::ClearFault() noexcept {
    if (state_ != ForceFeedbackState::Faulted) {
        return;
    }
    lastFaultReason_.clear();
    pendingStopDeviceEdge_ = false; // Faulted always kept output at zero; nothing left to signal.
    targetIdleState_ = ForceFeedbackState::Disabled;
    state_ = ForceFeedbackState::Disabled;
}

ForceFeedbackDecision ForceFeedbackSafetyController::Update(const ForceFeedbackCommand& requested,
                                                              Clock::time_point telemetryTimestamp,
                                                              Clock::time_point now) noexcept {
    lastUpdateCallAt_ = now;
    ++updateCount_;

    // Disabled/Faulted/Stopping are all "committed to zero" states: once
    // entered, nothing this call requests can matter until a fresh
    // Enable()/ClearFault() (or the ramp-down finishes and settles into
    // Armed/Disabled on its own). Checking Stopping here as well as in
    // Tick() is what makes a Disable() mid-ramp keep advancing even if the
    // caller only ever calls Update(), never Tick().
    if (state_ == ForceFeedbackState::Disabled || state_ == ForceFeedbackState::Faulted ||
        state_ == ForceFeedbackState::Stopping) {
        return SettleTowardTarget(ZeroCommand(), now);
    }

    const auto watchdogTimeout = std::min(config_.watchdogTimeout, kAbsoluteMaxWatchdogTimeout);
    const bool fresh = (now >= telemetryTimestamp) && (now - telemetryTimestamp) <= watchdogTimeout;
    if (!fresh) {
        return Tick(now); // Stale telemetry is treated exactly like no new data at all.
    }
    lastTelemetryTimestamp_ = telemetryTimestamp;

    const ForceFeedbackCommand clamped = ClampToLimits(requested);
    const ForceFeedbackDecision decision = SettleTowardTarget(clamped, now);
    if (decision.applyCommand && !IsAllZero(decision.command)) {
        state_ = ForceFeedbackState::Active;
    }
    return decision;
}

ForceFeedbackDecision ForceFeedbackSafetyController::Tick(Clock::time_point now) noexcept {
    if (state_ == ForceFeedbackState::Disabled || state_ == ForceFeedbackState::Faulted ||
        state_ == ForceFeedbackState::Stopping) {
        return SettleTowardTarget(ZeroCommand(), now);
    }

    // state_ is Armed or Active here: only now does watchdog staleness matter.
    const auto watchdogTimeout = std::min(config_.watchdogTimeout, kAbsoluteMaxWatchdogTimeout);
    const bool telemetryStale = !lastTelemetryTimestamp_ || (now < *lastTelemetryTimestamp_) ||
                                 (now - *lastTelemetryTimestamp_) > watchdogTimeout;
    const bool callStale =
        !lastUpdateCallAt_ || (now < *lastUpdateCallAt_) || (now - *lastUpdateCallAt_) > watchdogTimeout;

    if (!(telemetryStale || callStale)) {
        return {}; // Fresh enough; nothing new to do this tick.
    }
    if (IsAllZero(appliedCommand_)) {
        return {}; // Already idle; staleness of nothing is not an event worth reacting to.
    }

    pendingStopDeviceEdge_ = true;
    targetIdleState_ = ForceFeedbackState::Armed;
    state_ = ForceFeedbackState::Stopping;
    ++watchdogStopCount_;
    return SettleTowardTarget(ZeroCommand(), now);
}

ForceFeedbackDiagnostics ForceFeedbackSafetyController::Diagnostics(Clock::time_point now) const noexcept {
    ForceFeedbackDiagnostics diag;
    diag.state = state_;
    diag.lastAppliedCommand = appliedCommand_;
    diag.updateCount = updateCount_;
    diag.watchdogStopCount = watchdogStopCount_;
    diag.lastFaultReason = lastFaultReason_;
    if (lastTelemetryTimestamp_ && now >= *lastTelemetryTimestamp_) {
        diag.telemetryAge = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastTelemetryTimestamp_);
    }
    if (lastUpdateCallAt_ && now >= *lastUpdateCallAt_) {
        diag.commandAge = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastUpdateCallAt_);
    }
    return diag;
}

ForceFeedbackDecision ForceFeedbackSafetyController::SettleTowardTarget(const ForceFeedbackCommand& target,
                                                                         Clock::time_point now) noexcept {
    const ForceFeedbackCommand limited = SlewLimit(target, now);
    const bool isZero = IsAllZero(limited);

    ForceFeedbackDecision decision;
    if (!isZero) {
        decision.applyCommand = true;
        decision.command = limited;
        return decision;
    }

    if (pendingStopDeviceEdge_) {
        decision.applyCommand = true;
        decision.command = limited;
        decision.stopDevice = true;
        pendingStopDeviceEdge_ = false;
        state_ = targetIdleState_;
    }
    return decision;
}

ForceFeedbackCommand ForceFeedbackSafetyController::ClampToLimits(const ForceFeedbackCommand& raw) const noexcept {
    ForceFeedbackCommand out{};
    out.constantForce = SanitizeAndClamp(raw.constantForce, -1.0f, 1.0f);
    out.spring = SanitizeAndClamp(raw.spring, 0.0f, 1.0f);
    out.damper = SanitizeAndClamp(raw.damper, 0.0f, 1.0f);
    out.gain = SanitizeAndClamp(raw.gain, 0.0f, 1.0f);

    if (!config_.enabled) {
        return ZeroCommand();
    }

    const float ceiling = std::clamp(config_.maxTorqueNormalized, 0.0f, kAbsoluteMaxTorqueNormalized);
    out.constantForce = std::clamp(out.constantForce, -ceiling, ceiling);
    out.spring = std::min(out.spring, ceiling);
    out.damper = std::min(out.damper, ceiling);

    const float masterGainCeiling = SanitizeAndClamp(config_.masterGain, 0.0f, 1.0f);
    out.gain = std::min(out.gain, masterGainCeiling);

    if (out.gain <= kZeroEpsilon) {
        return ZeroCommand();
    }
    return out;
}

ForceFeedbackCommand ForceFeedbackSafetyController::SlewLimit(const ForceFeedbackCommand& target,
                                                                Clock::time_point now) noexcept {
    float dt = 0.0f;
    if (lastAppliedAt_ && now > *lastAppliedAt_) {
        dt = std::chrono::duration<float>(now - *lastAppliedAt_).count();
    }
    lastAppliedAt_ = now;

    const float rate = std::clamp(config_.slewRatePerSecond, kMinSlewRatePerSecond, kMaxSlewRatePerSecond);
    const float maxDelta = rate * dt;

    const auto step = [maxDelta](float current, float wanted) noexcept -> float {
        const float delta = wanted - current;
        if (std::fabs(delta) <= maxDelta) {
            return wanted;
        }
        return current + (delta > 0.0f ? maxDelta : -maxDelta);
    };

    appliedCommand_ = ForceFeedbackCommand{
        step(appliedCommand_.constantForce, target.constantForce),
        step(appliedCommand_.spring, target.spring),
        step(appliedCommand_.damper, target.damper),
        step(appliedCommand_.gain, target.gain),
    };
    return appliedCommand_;
}

} // namespace rvwheel::ffb
