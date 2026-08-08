#pragma once

// Depends only on ILogitechSdk (our own abstraction), never on the real
// vendor SDK, so this file compiles and is unit-testable with a fake SDK
// regardless of RVWHEEL_ENABLE_LOGITECH_SDK. Only LogitechGamingSdkAdapter
// (a concrete ILogitechSdk, gated behind that option) touches the real SDK.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "rvwheel/dal/Diagnostics.hpp"
#include "rvwheel/dal/IWheelDevice.hpp"
#include "rvwheel/devices/ILogitechSdk.hpp"

namespace rvwheel::devices {

class LogitechDevice final : public rvwheel::dal::IWheelDevice {
public:
    LogitechDevice(rvwheel::dal::DeviceInfo info,
                   std::shared_ptr<ILogitechSdk> sdk,
                   std::size_t sdkIndex,
                   rvwheel::dal::DiagnosticSink diagnostics);

    [[nodiscard]] const rvwheel::dal::DeviceInfo& Info() const noexcept override { return info_; }
    [[nodiscard]] bool IsConnected() const noexcept override { return state_.connected; }

    rvwheel::dal::Status Poll() noexcept override;
    [[nodiscard]] const rvwheel::dal::WheelState& State() const noexcept override { return state_; }

    // Logitech's ILogitechSdk abstraction reports steering/throttle/
    // brake/clutch already role-mapped (see LogitechRawWheelState), so
    // there is no raw "source" for this backend to remap -- only
    // `direction` is honored here. Existing hardware capabilities
    // (Info().capabilities, set at construction from the SDK) are never
    // cleared by this call: unlike DirectInputDevice, a Logitech device's
    // role availability is a hardware fact reported once by the SDK, not
    // something plug-and-play matching resolves. Not exercised against
    // real hardware in this project (RVWHEEL_ENABLE_LOGITECH_SDK is OFF
    // throughout); see LogitechGamingSdkAdapter's own limitations.
    rvwheel::dal::Status ApplyLayout(const rvwheel::dal::WheelInputLayout& layout,
                                      const rvwheel::dal::DeviceReadinessPolicy& readinessPolicy) noexcept override;

    rvwheel::dal::Status ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand& command) noexcept override;
    rvwheel::dal::Status StopForceFeedback() noexcept override;

private:
    rvwheel::dal::DeviceInfo info_;
    std::shared_ptr<ILogitechSdk> sdk_;
    std::size_t sdkIndex_;
    rvwheel::dal::DiagnosticSink diagnostics_;

    bool steeringInverted_ = false;
    bool throttleInverted_ = false;
    bool brakeInverted_ = false;
    bool clutchInverted_ = false;

    rvwheel::dal::WheelState state_{};
    std::uint64_t sampleCounter_ = 0;
};

} // namespace rvwheel::devices
