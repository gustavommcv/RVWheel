#pragma once

#include <cstddef>

namespace rvwheel::tools::probe {

enum class DeviceSelectionOutcome {
    NoDevices,
    SingleDevice,
    MultipleDevicesUsingFirst,
};

struct DeviceSelectionResult {
    DeviceSelectionOutcome outcome = DeviceSelectionOutcome::NoDevices;
    std::size_t selectedIndex = 0; // Only meaningful when outcome != NoDevices.
};

// Pure device-selection policy for --monitor/--capture, which have no
// --device selector flag in this MVP. Kept separate from DeviceProbeApp so
// it is testable with a plain device count, without constructing a
// DeviceManager or touching any hardware.
[[nodiscard]] DeviceSelectionResult SelectDeviceForMonitoring(std::size_t deviceCount) noexcept;

} // namespace rvwheel::tools::probe
