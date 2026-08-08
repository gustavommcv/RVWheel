#include "rvwheel/devices/LogitechDevice.hpp"

#include <array>
#include <chrono>
#include <utility>

namespace rvwheel::devices {

using rvwheel::dal::LogLevel;
using rvwheel::dal::Status;
using rvwheel::dal::StatusCode;

LogitechDevice::LogitechDevice(rvwheel::dal::DeviceInfo info,
                                std::shared_ptr<ILogitechSdk> sdk,
                                std::size_t sdkIndex,
                                rvwheel::dal::DiagnosticSink diagnostics)
    : info_(std::move(info)), sdk_(std::move(sdk)), sdkIndex_(sdkIndex), diagnostics_(std::move(diagnostics)) {}

Status LogitechDevice::Poll() noexcept {
    if (!sdk_) {
        state_.valid = false;
        state_.connected = false;
        state_.readiness = rvwheel::dal::ReadinessState::Unconfigured;
        return Status::NotConnected("Logitech SDK adapter is unavailable");
    }

    LogitechRawWheelState raw{};
    const Status status = sdk_->GetState(sdkIndex_, raw);
    if (!status.IsOk()) {
        state_.valid = false;
        state_.connected = false;
        state_.readiness = rvwheel::dal::ReadinessState::Unconfigured;
        return status;
    }

    if (!raw.connected) {
        state_.valid = false;
        state_.connected = false;
        state_.readiness = rvwheel::dal::ReadinessState::Unconfigured;
        return Status::NotConnected("Logitech device reported not connected");
    }

    state_.steering = info_.capabilities.hasSteering ? (steeringInverted_ ? -raw.steering : raw.steering) : 0.0f;
    state_.throttle = info_.capabilities.hasThrottle ? (throttleInverted_ ? 1.0f - raw.throttle : raw.throttle) : 0.0f;
    state_.brake = info_.capabilities.hasBrake ? (brakeInverted_ ? 1.0f - raw.brake : raw.brake) : 0.0f;
    state_.clutch = info_.capabilities.hasClutch ? (clutchInverted_ ? 1.0f - raw.clutch : raw.clutch) : 0.0f;
    state_.buttons = raw.buttons;
    state_.povs = raw.povs;
    state_.povCount = raw.povCount;
    state_.connected = true;
    state_.valid = true;
    // No warmup/readiness state machine for this backend (see
    // ApplyLayout's doc comment): Ready mirrors the pre-existing `valid`
    // semantics exactly, purely for display consistency with
    // DirectInputDevice's richer readiness field.
    state_.readiness = rvwheel::dal::ReadinessState::Ready;
    state_.sampleCounter = ++sampleCounter_;
    state_.timestamp = std::chrono::steady_clock::now();

    return Status::Ok();
}

Status LogitechDevice::ApplyLayout(const rvwheel::dal::WheelInputLayout& layout,
                                    const rvwheel::dal::DeviceReadinessPolicy& /*readinessPolicy*/) noexcept {
    using rvwheel::dal::AxisDirection;

    if (layout.steering) {
        steeringInverted_ = (layout.steering->direction == AxisDirection::Inverted);
    }
    if (layout.throttle) {
        throttleInverted_ = (layout.throttle->direction == AxisDirection::Inverted);
    }
    if (layout.brake) {
        brakeInverted_ = (layout.brake->direction == AxisDirection::Inverted);
    }
    if (layout.clutch) {
        clutchInverted_ = (layout.clutch->direction == AxisDirection::Inverted);
    }
    return Status::Ok();
}

Status LogitechDevice::ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand& command) noexcept {
    if (!sdk_ || !state_.connected) {
        return Status::NotConnected("Cannot apply force feedback to a disconnected device");
    }
    if (!info_.capabilities.hasForceFeedback) {
        return Status::NotSupported("Device has no force feedback capability");
    }

    const Status gainStatus = sdk_->SetGain(sdkIndex_, command.gain);
    const Status constantStatus = sdk_->PlayConstantForce(sdkIndex_, command.constantForce);
    const Status springStatus = sdk_->PlaySpringForce(sdkIndex_, command.spring);
    const Status damperStatus = sdk_->PlayDamperForce(sdkIndex_, command.damper);

    const std::array<const Status*, 4> results{&gainStatus, &constantStatus, &springStatus, &damperStatus};

    for (const Status* result : results) {
        if (!result->IsOk()) {
            diagnostics_(LogLevel::Warning, result->Message());
        }
    }
    for (const Status* result : results) {
        if (result->Code() == StatusCode::BackendError) {
            return *result;
        }
    }
    for (const Status* result : results) {
        if (result->Code() == StatusCode::NotSupported) {
            return *result;
        }
    }
    return Status::Ok();
}

Status LogitechDevice::StopForceFeedback() noexcept {
    if (!sdk_) {
        return Status::NotConnected("Cannot stop force feedback on a disconnected device");
    }
    return sdk_->StopAllForces(sdkIndex_);
}

} // namespace rvwheel::devices
