#pragma once

#include <iosfwd>
#include <atomic>

namespace rvwheel::tools::probe {

// Enumerates Logitech HID top-level collections using metadata-only handles.
// This diagnostic never writes an output/feature report and never applies FFB.
[[nodiscard]] int InspectLogitechHidDevices(std::ostream& output, std::ostream& errors);

// Sends the two documented classic-Logitech autocenter commands, but only
// after exact G923 PS/PC identity and HID-layout validation. This is a real
// hardware test and must only be called after explicit per-run authorization.
[[nodiscard]] int RunLogitechG923AutocenterHardwareTest(std::atomic<bool>& stopRequested,
                                                        std::ostream& output,
                                                        std::ostream& errors);

} // namespace rvwheel::tools::probe
