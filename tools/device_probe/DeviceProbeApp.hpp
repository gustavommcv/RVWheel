#pragma once

#include <atomic>

#include "CliOptions.hpp"

namespace rvwheel::tools::probe {

// Orchestrates the five real modes (List/Profiles/Calibrate/Monitor/
// Capture) against the real DAL, profile system, and Win32. Not unit
// tested directly -- everything it does that CAN be tested without
// hardware/Win32 (argument parsing, formatting, device-selection policy,
// JSONL serialization, profile parsing/matching, the calibration state
// machine) has already been factored out into the pure components it
// calls into.
class DeviceProbeApp {
public:
    DeviceProbeApp(CliOptions options, std::atomic<bool>& stopRequested);

    [[nodiscard]] int Run();

private:
    [[nodiscard]] int RunList();
    [[nodiscard]] int RunProfiles();
    [[nodiscard]] int RunCalibrate();
    [[nodiscard]] int RunMonitor();
    [[nodiscard]] int RunCapture();

    CliOptions options_;
    std::atomic<bool>& stopRequested_;
};

} // namespace rvwheel::tools::probe
