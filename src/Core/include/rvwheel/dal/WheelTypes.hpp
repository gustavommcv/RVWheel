#pragma once

#include <array>
#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "rvwheel/dal/DeviceId.hpp"
#include "rvwheel/dal/ReadinessState.hpp"

namespace rvwheel::dal {

// Which driver/SDK backs a given IWheelDevice. Never leaks vendor types
// (DirectInput COM interfaces, Logitech SDK structs) into public headers;
// this is metadata only.
enum class DeviceBackend : std::uint8_t {
    DirectInput,
    Logitech,
};

// Fixed capacities for the no-allocation, per-frame state snapshot. Chosen
// to match DirectInput's own limits (128 buttons, 4 POVs), which comfortably
// covers every known consumer wheel/pedal/shifter combo.
inline constexpr std::size_t kMaxButtons = 128;
inline constexpr std::size_t kMaxPovCount = 4;

using ButtonMask = std::bitset<kMaxButtons>;

// 8-way POV/hat direction plus "centered" (no input). Backends are
// responsible for converting their native POV representation into this
// enum; see DirectInputDevice for the conversion used for DirectInput's
// hundredths-of-a-degree convention.
enum class PovDirection : std::uint8_t {
    Centered = 0,
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
    NorthWest,
};

// Static capabilities of a device, discovered once at enumeration time.
//
// Named WheelDeviceCapabilities rather than the more obvious
// "DeviceCapabilities" because <windows.h> defines DeviceCapabilities as an
// ANSI/Unicode macro (expanding to DeviceCapabilitiesA/W). Macro expansion
// happens even for namespace-qualified identifiers, so
// `rvwheel::dal::DeviceCapabilities` would silently become
// `rvwheel::dal::DeviceCapabilitiesA` in any translation unit that had
// included <windows.h> first — a real collision, not a hypothetical one.
// This name must stay collision-free with Win32 regardless of include
// order; do not rename it back without checking WinUser.h/wingdi.h first.
struct WheelDeviceCapabilities {
    bool hasSteering = false;
    bool hasThrottle = false;
    bool hasBrake = false;
    bool hasClutch = false;
    bool hasForceFeedback = false;
    std::uint16_t buttonCount = 0;
    std::uint8_t povCount = 0;
};

// Identity/metadata for a device. `id`, `name`, `manufacturer`, `backend`,
// `vendorId`, `productId`, and the hardware-fixed parts of `capabilities`
// (`hasForceFeedback`, `buttonCount`, `povCount`) are set once at
// enumeration and never change afterwards. The four role flags inside
// `capabilities` (`hasSteering`/`hasThrottle`/`hasBrake`/`hasClutch`) are
// the one deliberate exception: they start false (no role is bound until
// a profile/layout says otherwise -- see IWheelDevice::ApplyLayout) and
// are updated, in place, only by ApplyLayout(). This is a rare,
// explicit reconfiguration event, not a per-frame mutation, so
// IWheelDevice::Info() can keep returning a plain const reference.
struct DeviceInfo {
    DeviceId id;
    std::string name;
    std::string manufacturer;
    DeviceBackend backend = DeviceBackend::DirectInput;
    std::optional<std::uint16_t> vendorId;
    std::optional<std::uint16_t> productId;
    WheelDeviceCapabilities capabilities;
};

// Normalized, per-frame snapshot of a wheel's full input state. Contains no
// dynamically-sized members, so copying/reading it never allocates.
struct WheelState {
    float steering = 0.0f; // [-1, 1], 0 = centered.
    float throttle = 0.0f; // [0, 1], 0 = released.
    float brake = 0.0f;    // [0, 1], 0 = released.
    float clutch = 0.0f;   // [0, 1], 0 = released (fully engaged, no clutch pedal pressure).

    ButtonMask buttons{};
    std::array<PovDirection, kMaxPovCount> povs{};
    std::uint8_t povCount = 0;

    // Monotonically increasing counter, incremented on every successful
    // Poll(). Lets consumers detect a stale/unchanged snapshot without
    // relying on wall-clock time.
    std::uint64_t sampleCounter = 0;
    std::chrono::steady_clock::time_point timestamp{};

    // True only when the last Poll() succeeded, the device was connected,
    // AND readiness == Ready (see below). When false, the other fields
    // hold the last known-good values (never garbage), but should not be
    // treated as current input -- this is deliberately the case during
    // WarmingUp/Stabilizing even though connected can be true, so a
    // device's own startup transient (e.g. the G923's ~2.05s midpoint
    // read) is never mistaken for real input.
    bool valid = false;
    bool connected = false;

    // Full readiness detail behind `valid`; see ReadinessState and
    // DeviceReadinessTracker. Unconfigured until a WheelInputLayout has
    // been applied via IWheelDevice::ApplyLayout().
    ReadinessState readiness = ReadinessState::Unconfigured;
};

// Force feedback request. Any component the target backend/device cannot
// honor is reported back via the Status returned from
// IWheelDevice::ApplyForceFeedback rather than silently ignored.
struct ForceFeedbackCommand {
    float constantForce = 0.0f; // [-1, 1], signed constant force effect.
    float spring = 0.0f;        // [0, 1], spring (centering) condition strength.
    float damper = 0.0f;        // [0, 1], damper condition strength.
    float gain = 1.0f;          // [0, 1], global output gain applied by the device.
};

} // namespace rvwheel::dal
