#pragma once

// Our own abstraction over "whatever the Logitech Gaming SDK provides",
// deliberately designed by this project rather than mirroring any specific
// vendor header. It exists so LogitechDevice (and DeviceManager's dedup
// logic, indirectly) can be written, compiled, and unit-tested with a fake
// implementation WITHOUT the real, proprietary SDK ever being present.
//
// The only place that is allowed to depend on the real vendor SDK is a
// concrete implementation of this interface — see LogitechGamingSdkAdapter,
// which is compiled only when RVWHEEL_ENABLE_LOGITECH_SDK is ON. Every
// field/method below is expressed in this project's own vocabulary
// (normalized floats, our Status type), not the vendor's; converting from
// whatever raw units/ranges the real SDK uses is the adapter's job,
// specifically so no vendor-specific numeric convention needs to be
// guessed here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "rvwheel/dal/Status.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::devices {

struct LogitechDeviceIdentity {
    std::string name;
    std::string manufacturer;
    std::optional<std::uint16_t> vendorId;
    std::optional<std::uint16_t> productId;
};

// Already normalized by the implementation (real adapter or fake) into the
// same domains WheelState uses, so LogitechDevice itself never needs to
// know the vendor SDK's raw ranges.
struct LogitechRawWheelState {
    float steering = 0.0f; // [-1, 1]
    float throttle = 0.0f; // [0, 1]
    float brake = 0.0f;    // [0, 1]
    float clutch = 0.0f;   // [0, 1]
    rvwheel::dal::ButtonMask buttons{};
    std::array<rvwheel::dal::PovDirection, rvwheel::dal::kMaxPovCount> povs{};
    std::uint8_t povCount = 0;
    bool connected = false;
};

class ILogitechSdk {
public:
    virtual ~ILogitechSdk() = default;

    // Idempotent; safe to call even if already initialized. Ownership of
    // the SDK's global init/shutdown lifecycle belongs entirely to whoever
    // holds the ILogitechSdk instance (see LogitechDeviceEnumerator).
    virtual rvwheel::dal::Status Initialize() = 0;
    virtual void Shutdown() noexcept = 0;

    // Number of wheel slots the SDK currently reports, connected or not.
    // Indices below this count are valid arguments to every method below.
    [[nodiscard]] virtual std::size_t DeviceCount() const = 0;
    [[nodiscard]] virtual bool IsConnected(std::size_t index) const = 0;
    [[nodiscard]] virtual std::optional<LogitechDeviceIdentity> GetIdentity(std::size_t index) const = 0;
    [[nodiscard]] virtual std::optional<rvwheel::dal::WheelDeviceCapabilities> GetCapabilities(std::size_t index) const = 0;

    virtual rvwheel::dal::Status GetState(std::size_t index, LogitechRawWheelState& outState) const = 0;

    virtual rvwheel::dal::Status PlayConstantForce(std::size_t index, float normalizedForce) = 0;
    virtual rvwheel::dal::Status PlaySpringForce(std::size_t index, float normalizedStrength) = 0;
    virtual rvwheel::dal::Status PlayDamperForce(std::size_t index, float normalizedStrength) = 0;
    virtual rvwheel::dal::Status SetGain(std::size_t index, float normalizedGain) = 0;
    virtual rvwheel::dal::Status StopAllForces(std::size_t index) = 0;
};

} // namespace rvwheel::devices
