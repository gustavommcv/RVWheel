#pragma once

#ifndef RVWHEEL_ENABLE_LOGITECH_SDK
#error "LogitechGamingSdkAdapter.hpp must only be included when RVWHEEL_ENABLE_LOGITECH_SDK is defined."
#endif

#include "rvwheel/devices/ILogitechSdk.hpp"

namespace rvwheel::devices {

// The ONLY translation unit in this project allowed to depend on the real,
// proprietary Logitech Gaming SDK.
//
// IMPORTANT: every method below currently returns an explicit failure
// status rather than calling into the vendor SDK. This project's
// engineering guidelines forbid presuming/guessing vendor function
// signatures that cannot be verified against a real, user-supplied SDK
// header, and no such header was available while writing this adapter.
// Implementing this class for real is intentionally left as the single,
// isolated next step for whoever has the actual SDK installed: include the
// header from the directory pointed at by RVWHEEL_LOGITECH_SDK_INCLUDE_DIR
// (see cmake/FindLogitechSteeringWheelSDK.cmake) here, and replace each
// body in LogitechGamingSdkAdapter.cpp with real calls against that
// header's *actual* API, updating this comment with the SDK version
// validated against. Everything else in the Logitech backend
// (LogitechDevice, LogitechDeviceEnumerator, DeviceManager dedup) already
// works against the ILogitechSdk interface and needs no changes once this
// adapter is completed.
class LogitechGamingSdkAdapter final : public ILogitechSdk {
public:
    LogitechGamingSdkAdapter();
    ~LogitechGamingSdkAdapter() override;

    rvwheel::dal::Status Initialize() override;
    void Shutdown() noexcept override;

    [[nodiscard]] std::size_t DeviceCount() const override;
    [[nodiscard]] bool IsConnected(std::size_t index) const override;
    [[nodiscard]] std::optional<LogitechDeviceIdentity> GetIdentity(std::size_t index) const override;
    [[nodiscard]] std::optional<rvwheel::dal::WheelDeviceCapabilities> GetCapabilities(std::size_t index) const override;

    rvwheel::dal::Status GetState(std::size_t index, LogitechRawWheelState& outState) const override;

    rvwheel::dal::Status PlayConstantForce(std::size_t index, float normalizedForce) override;
    rvwheel::dal::Status PlaySpringForce(std::size_t index, float normalizedStrength) override;
    rvwheel::dal::Status PlayDamperForce(std::size_t index, float normalizedStrength) override;
    rvwheel::dal::Status SetGain(std::size_t index, float normalizedGain) override;
    rvwheel::dal::Status StopAllForces(std::size_t index) override;

private:
    bool initialized_ = false;
};

} // namespace rvwheel::devices
