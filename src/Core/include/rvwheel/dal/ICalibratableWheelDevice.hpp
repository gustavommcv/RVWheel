#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "rvwheel/dal/AxisSource.hpp"
#include "rvwheel/dal/Status.hpp"

namespace rvwheel::dal {

// Metadata for one raw axis as discovered directly from the backend, with
// none of the four gameplay roles (steering/throttle/brake/clutch) applied
// yet. This is what a calibration tool needs and IWheelDevice/WheelState
// deliberately do not expose, since WheelState only ever reports
// already-role-mapped values.
struct RawAxisInfo {
    AxisSource source = AxisSource::Unknown;
    std::string name; // Backend-reported object name, when available; empty otherwise -- never guessed.
    std::int32_t rawMin = 0;
    std::int32_t rawMax = 0;
};

struct RawAxisSample {
    AxisSource source = AxisSource::Unknown;
    std::int32_t rawValue = 0;
};

// Matches AxisSource's enumerable set (kAllAxisSources); a fixed capacity
// keeps a raw snapshot allocation-free, consistent with WheelState.
inline constexpr std::size_t kMaxRawAxes = 8;

struct RawAxisSnapshot {
    std::array<RawAxisSample, kMaxRawAxes> samples{};
    std::uint8_t count = 0;
};

// Segregated, tooling-only interface: exposes backend axis discovery
// needed to build a WheelInputLayout (see the calibration wizard), kept
// off IWheelDevice so no gameplay consumer, and no per-frame game-loop
// performance budget, ever depends on it existing. A backend that
// implements this MUST reuse its existing acquired device handle rather
// than acquiring the hardware a second time -- see DirectInputDevice,
// which answers both this interface and IWheelDevice from the same
// IDirectInputDevice8A object.
class ICalibratableWheelDevice {
public:
    virtual ~ICalibratableWheelDevice() = default;

    // Every raw axis this backend found on the device, independent of
    // whether any role is currently bound to it.
    [[nodiscard]] virtual std::vector<RawAxisInfo> EnumerateRawAxes() const = 0;

    // Refreshes outSnapshot with the current raw value of every axis
    // EnumerateRawAxes() reported. Allocation-free on the success path.
    virtual Status PollRawAxes(RawAxisSnapshot& outSnapshot) noexcept = 0;
};

} // namespace rvwheel::dal
