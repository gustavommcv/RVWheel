#include "LogitechGamingSdkAdapter.hpp"

// Deliberately does NOT #include the vendor SDK header: no method below
// calls into vendor code yet, so nothing here needs it. When implementing
// this adapter for real, add the include here (path resolved via the
// RVWHEEL_LOGITECH_SDK_INCLUDE_DIR CMake cache variable / imported target
// in cmake/FindLogitechSteeringWheelSDK.cmake) and replace the bodies below
// with real, header-verified calls. See LogitechGamingSdkAdapter.hpp for
// the full rationale.

namespace rvwheel::devices {

using rvwheel::dal::Status;

namespace {
constexpr const char* kNotImplementedMessage =
    "LogitechGamingSdkAdapter is a structural skeleton; vendor SDK calls have not been implemented "
    "(no verified SDK header was available when this backend was written). "
    "See src/Devices/Logitech/src/LogitechGamingSdkAdapter.cpp.";
} // namespace

LogitechGamingSdkAdapter::LogitechGamingSdkAdapter() = default;

LogitechGamingSdkAdapter::~LogitechGamingSdkAdapter() { Shutdown(); }

Status LogitechGamingSdkAdapter::Initialize() {
    if (initialized_) {
        return Status::Ok();
    }
    // Deterministically fails so DeviceManagerFactory's "prefer Logitech
    // only when usable" policy falls back to DirectInput-only, rather than
    // silently pretending to be initialized with no real device access.
    return Status::BackendError(kNotImplementedMessage);
}

void LogitechGamingSdkAdapter::Shutdown() noexcept { initialized_ = false; }

std::size_t LogitechGamingSdkAdapter::DeviceCount() const { return 0; }

bool LogitechGamingSdkAdapter::IsConnected(std::size_t /*index*/) const { return false; }

std::optional<LogitechDeviceIdentity> LogitechGamingSdkAdapter::GetIdentity(std::size_t /*index*/) const {
    return std::nullopt;
}

std::optional<rvwheel::dal::WheelDeviceCapabilities> LogitechGamingSdkAdapter::GetCapabilities(std::size_t /*index*/) const {
    return std::nullopt;
}

Status LogitechGamingSdkAdapter::GetState(std::size_t /*index*/, LogitechRawWheelState& /*outState*/) const {
    return Status::BackendError(kNotImplementedMessage);
}

Status LogitechGamingSdkAdapter::PlayConstantForce(std::size_t /*index*/, float /*normalizedForce*/) {
    return Status::NotSupported(kNotImplementedMessage);
}

Status LogitechGamingSdkAdapter::PlaySpringForce(std::size_t /*index*/, float /*normalizedStrength*/) {
    return Status::NotSupported(kNotImplementedMessage);
}

Status LogitechGamingSdkAdapter::PlayDamperForce(std::size_t /*index*/, float /*normalizedStrength*/) {
    return Status::NotSupported(kNotImplementedMessage);
}

Status LogitechGamingSdkAdapter::SetGain(std::size_t /*index*/, float /*normalizedGain*/) {
    return Status::NotSupported(kNotImplementedMessage);
}

Status LogitechGamingSdkAdapter::StopAllForces(std::size_t /*index*/) {
    return Status::NotSupported(kNotImplementedMessage);
}

} // namespace rvwheel::devices
