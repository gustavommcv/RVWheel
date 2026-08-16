#pragma once

#include "rvwheel/dal/Status.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::devices {

[[nodiscard]] bool IsLogitechG923PsPc(const rvwheel::dal::DeviceInfo& info) noexcept;

// Sends the physically validated classic-Logitech firmware autocenter
// command, restricted to the exact G923 PS/PC identity and HID layout.
// Diagnostic-only: do not mix this raw HID writer with an active DirectInput
// effect session. The real G923 test showed that combination is unreliable.
[[nodiscard]] rvwheel::dal::Status SetLogitechG923AutocenterEnabled(bool enabled) noexcept;

} // namespace rvwheel::devices
