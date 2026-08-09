#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
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

enum class FfbTestCooperativeLevel {
    Background,
    ForegroundUnfocused,
    ForegroundFocused,
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

    // Hardware-test-only DirectInput acquisition policy. Foreground uses a
    // valid top-level but deliberately invisible/unfocused window in the
    // first experiment; it still never creates an effect in stop-only mode.
    FfbTestCooperativeLevel ffbTestCooperativeLevel = FfbTestCooperativeLevel::Background;

    // --bridge --enable-force-feedback: the runtime gate for the bridge's
    // first real force feedback integration. False (default) preserves
    // --bridge's exact existing behavior (no force, NONEXCLUSIVE |
    // BACKGROUND, periodic refresh). This is one of two independent gates
    // -- see BridgeForceFeedbackSession and RunBridge -- the resolved
    // profile's own forceFeedback.enabled must ALSO be true before any
    // force is ever applied.
    bool enableForceFeedback = false;

    // --bridge --duration <seconds>: an optional, explicit bound on how
    // long --bridge runs before stopping itself through the exact same
    // graceful shutdown path Ctrl+C already takes (EmergencyStop,
    // StopForceFeedback, final state write). std::nullopt (the default)
    // preserves --bridge's existing infinite-until-Ctrl+C behavior exactly
    // -- this field is only ever set when --duration is explicitly given
    // alongside --bridge, so a first real force-feedback test does not
    // have to depend solely on an operator's own Ctrl+C timing. Distinct
    // from `duration` above (which always has a concrete default and is
    // used by --monitor/--capture/--ffb-simulate) precisely so bridge's
    // "unbounded by default" behavior can be represented at all.
    std::optional<std::chrono::seconds> bridgeDuration;
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
