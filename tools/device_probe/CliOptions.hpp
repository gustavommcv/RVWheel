#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rvwheel::tools::probe {

enum class ProbeMode {
    Help,
    List,
    Monitor,
    Capture,
    Bridge,
    Profiles,
    Calibrate,
    FfbSimulate,
    FfbHardwareTestStopOnly,
    FfbHardwareTestWeakEffect,
};

// Which single condition effect --ffb-hw-test-weak-effect exercises. Only
// one at a time, on purpose: the gated hardware test procedure
// (docs/FORCE_FEEDBACK_HARDWARE_TEST.md) isolates spring from damper so a
// problem can be attributed to one specific effect type.
enum class FfbTestEffect {
    Spring,
    Damper,
};

struct CliOptions {
    ProbeMode mode = ProbeMode::Help;
    std::chrono::seconds duration{30};
    int rateHz = 60;
    // Optional launcher supervision for --bridge. When non-zero, the bridge
    // exits cleanly as soon as this process no longer exists.
    std::uint32_t parentProcessId = 0;
    std::filesystem::path capturePath;

    // --calibrate [--output <path>]: empty means the wizard picks a
    // default filename under the user profiles directory.
    std::filesystem::path calibrateOutputPath;

    // --profiles-dir <path>: overrides the user profiles directory (the
    // one --calibrate saves into and user overrides load from) instead of
    // %LOCALAPPDATA%\RVWheel\profiles. Valid with any mode; this is the
    // explicit override the task asks for so tests/manual runs never have
    // to touch the real LOCALAPPDATA. Built-in profile resolution is
    // unaffected -- it is always resolved relative to the executable.
    std::filesystem::path profilesDirOverride;

    // --monitor/--capture/--bridge ... --profile <profileId-or-path>: forces a
    // specific profile instead of automatic resolution. Empty means
    // auto-resolve via the profile system. Kept as a wide string (not
    // narrowed) since it may be a filesystem path with non-ASCII
    // characters; DeviceProbeApp decides whether to treat it as a path or
    // a profileId.
    std::wstring profileSelector;

    // --ffb-hw-test-weak-effect --effect <spring|damper>. Gain/strength/
    // duration are deliberately NOT configurable from the command line for
    // this mode -- see DeviceProbeApp::RunFfbHardwareTestWeakEffect -- so
    // this field only ever selects which single effect type is exercised.
    FfbTestEffect ffbTestEffect = FfbTestEffect::Spring;
};

struct CliParseResult {
    bool success = false;
    CliOptions options;
    std::string errorMessage; // Empty when success is true.
};

// Pure command-line parsing: no Win32, no DAL, no file/console I/O.
// Operates on already-split wide arguments (excluding argv[0]) so it is
// unit-testable without a real process command line, and so a --capture
// path round-trips Unicode exactly (it is never narrowed).
class CliParser {
public:
    static constexpr int kMinRateHz = 1;
    static constexpr int kMaxRateHz = 250;
    static constexpr long long kMinDurationSeconds = 1;
    static constexpr long long kMaxDurationSeconds = 3600; // Generous but bounded safety cap, not a hardware limit.
    static constexpr int kDefaultDurationSeconds = 30;
    static constexpr int kDefaultRateHz = 60;

    CliParser() = delete;

    [[nodiscard]] static CliParseResult Parse(const std::vector<std::wstring>& args);
    [[nodiscard]] static std::string UsageText();
};

} // namespace rvwheel::tools::probe
