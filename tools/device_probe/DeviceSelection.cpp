#include "DeviceSelection.hpp"

namespace rvwheel::tools::probe {

DeviceSelectionResult SelectDeviceForMonitoring(std::size_t deviceCount) noexcept {
    if (deviceCount == 0) {
        return DeviceSelectionResult{DeviceSelectionOutcome::NoDevices, 0};
    }
    if (deviceCount == 1) {
        return DeviceSelectionResult{DeviceSelectionOutcome::SingleDevice, 0};
    }
    return DeviceSelectionResult{DeviceSelectionOutcome::MultipleDevicesUsingFirst, 0};
}

} // namespace rvwheel::tools::probe
