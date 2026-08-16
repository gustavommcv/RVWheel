#include "CliOptions.hpp"

#include <utility>

namespace rvwheel::tools::probe {

namespace {

// Manual, digit-only parser: deliberately rejects a leading '-' or '+' (no
// sign support) so a negative value fails to parse rather than silently
// wrapping into range-check logic, and avoids depending on std::from_chars
// support for wchar_t (only the char overloads are universally available).
[[nodiscard]] bool TryParsePositiveInt(const std::wstring& text, long long& outValue) {
    if (text.empty()) {
        return false;
    }
    long long value = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        value = value * 10 + (ch - L'0');
        if (value > 1'000'000'000LL) {
            return false; // Guard against absurd/overflowing input early.
        }
    }
    outValue = value;
    return true;
}

// Best-effort, lossy narrowing for diagnostic messages only (an
// unrecognized-argument error). Non-ASCII characters become '?' rather
// than risking a throwing/deprecated conversion API for a code path that
// only needs to be readable, not exact.
[[nodiscard]] std::string NarrowForDisplay(const std::wstring& text) {
    std::string result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        result.push_back((ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '?');
    }
    return result;
}

[[nodiscard]] CliParseResult Fail(std::string message) {
    CliParseResult result;
    result.success = false;
    result.errorMessage = std::move(message);
    return result;
}

} // namespace

std::string CliParser::UsageText() {
    return "rvwheel_device_probe - RVWheel DirectInput/Logitech DAL hardware probe\n"
           "\n"
           "Usage:\n"
           "  rvwheel_device_probe --help\n"
           "  rvwheel_device_probe --list\n"
           "  rvwheel_device_probe --profiles\n"
           "  rvwheel_device_probe --calibrate [--output <profile.json>]\n"
           "  rvwheel_device_probe --monitor [--duration <seconds>] [--rate <hz>] [--profile <id-or-path>]\n"
           "  rvwheel_device_probe --capture <path.jsonl> [--duration <seconds>] [--rate <hz>] [--profile <id-or-path>]\n"
           "  rvwheel_device_probe --bridge [--rate <hz>] [--profile <id-or-path>] [--enable-force-feedback]\n"
           "                              [--duration <seconds>]\n"
           "  rvwheel_device_probe --ffb-simulate [--duration <seconds>] [--rate <hz>] [--profile <id-or-path>]\n"
           "  rvwheel_device_probe --ffb-hw-test-stop-only\n"
           "                              [--ffb-cooperative-level background|foreground|foreground-focused]\n"
           "  rvwheel_device_probe --ffb-hw-test-autocenter\n"
           "                              [--ffb-cooperative-level background|foreground|foreground-focused]\n"
           "  rvwheel_device_probe --ffb-hw-test-weak-effect [--effect spring|damper]\n"
           "                              [--ffb-cooperative-level background|foreground|foreground-focused]\n"
           "  rvwheel_device_probe --telemetry-monitor [--duration <seconds>] [--rate <hz>]\n"
           "  rvwheel_device_probe --logitech-hid-info\n"
           "  rvwheel_device_probe --ffb-hw-test-logitech-g923-autocenter\n"
           "\n"
           "Options:\n"
           "  --duration <seconds>   How long --monitor/--capture/--ffb-simulate/--telemetry-monitor run.\n"
           "                         Range [1, 3600], default 30.\n"
           "                         For --bridge specifically, this is OPTIONAL and OFF by default: --bridge\n"
           "                         alone still runs until Ctrl+C exactly as before. Only when explicitly given\n"
           "                         does --bridge stop itself after the given number of seconds, through the\n"
           "                         exact same graceful shutdown path (EmergencyStop/StopForceFeedback/final\n"
           "                         state write) Ctrl+C already takes.\n"
           "  --rate <hz>            Tick rate for --monitor/--capture/--bridge/--ffb-simulate/--telemetry-monitor.\n"
           "                         Range [1, 250], default 60.\n"
           "  --profile <id-or-path> Force a specific profile (by profileId or a .json path) instead of automatic\n"
           "                         resolution. Only valid with --monitor/--capture/--bridge/--ffb-simulate.\n"
           "  --output <path>        Where --calibrate saves the generated profile. Defaults to a name derived\n"
           "                         from the device under the user profiles directory. Only valid with --calibrate.\n"
           "  --profiles-dir <path>  Overrides the user profiles directory (where --calibrate saves and user\n"
           "                         overrides load from) instead of %LOCALAPPDATA%\\RVWheel\\profiles. Valid with\n"
           "                         any mode.\n"
           "  --parent-pid <pid>     Optional launcher process to supervise. Only valid with --bridge.\n"
           "  --effect spring|damper Which single condition effect --ffb-hw-test-weak-effect exercises.\n"
           "                         Default spring. Gain/strength/duration are fixed, conservative constants\n"
           "                         for this mode and are not configurable.\n"
           "  --ffb-cooperative-level background|foreground|foreground-focused\n"
           "                         Hardware-test-only DirectInput exclusive access policy. Default background\n"
           "                         preserves prior behavior. Foreground uses a top-level but invisible/unfocused\n"
           "                         diagnostic window; foreground-focused shows and activates a small tool window.\n"
           "  --enable-force-feedback\n"
           "                         Only valid with --bridge. Off by default. Requests EXCLUSIVE | BACKGROUND and\n"
           "                         arms the profile-configured spring/damper source, but ONLY if the resolved\n"
           "                         profile's own forceFeedback.enabled is also true -- this flag alone is never\n"
           "                         sufficient. See docs/FORCE_FEEDBACK.md.\n"
           "\n"
           "Notes:\n"
           "  - Hardware is re-enumerated at most once every 5 seconds; not configurable here, EXCEPT that\n"
           "    --bridge --enable-force-feedback disables periodic re-enumeration entirely while its exclusive\n"
           "    force-feedback session is active, matching the invariant validated in\n"
           "    docs/FORCE_FEEDBACK_HARDWARE_TEST.md (re-enumerating during an active exclusive effect was the\n"
           "    confirmed root cause of an earlier hardware failure).\n"
           "  - Force feedback is never applied by --list/--monitor/--capture/--calibrate, nor by plain --bridge.\n"
           "  - --ffb-simulate computes and prints force feedback commands using the resolved profile's\n"
           "    forceFeedback config, but ALWAYS routes them to an in-process recording sink -- it never calls\n"
           "    ApplyForceFeedback/StopForceFeedback on the real device, so no actuator is ever driven by it.\n"
           "  - --ffb-hw-test-stop-only is a REAL hardware test: it requests exclusive force-feedback access on\n"
           "    the first FFB-capable device and calls the real StopForceFeedback() exactly once. It never\n"
           "    creates or starts an effect. Only run this as part of the gated procedure in\n"
           "    docs/FORCE_FEEDBACK_HARDWARE_TEST.md, with the wheel secured and someone watching.\n"
           "  - --ffb-hw-test-autocenter is a REAL hardware test: it changes DIPROP_AUTOCENTER to OFF\n"
           "    for a fixed 5-second window, creates NO effect, then restores the exact prior value. It should\n"
           "    change resistance only; it must never move the wheel on its own. Explicit authorization required.\n"
           "  - --ffb-hw-test-weak-effect is a REAL hardware test: it applies ONE real, weak (gain 0.2) effect\n"
           "    for a fixed 5 seconds via the real ForceFeedbackSafetyController, then stops. Only run this\n"
           "    after --ffb-hw-test-stop-only has passed, per docs/FORCE_FEEDBACK_HARDWARE_TEST.md.\n"
           "  - --bridge runs until Ctrl+C (or, if --duration was explicitly given, until that many seconds\n"
           "    elapse) and publishes the latest safe input snapshot under LOCALAPPDATA.\n"
           "  - --telemetry-monitor only reads and prints the vehicle-telemetry.txt file that\n"
           "    mods/RVWheel/Scripts/main.lua writes; it NEVER enumerates or acquires a wheel device and NEVER\n"
           "    calls ApplyForceFeedback/StopForceFeedback -- it is safe to run with no wheel attached at all.\n"
           "  - --logitech-hid-info performs a read-only HID capability inspection for attached Logitech\n"
           "    devices. Handles are opened with desiredAccess=0; it never sends an input/output/feature\n"
           "    report and cannot apply force feedback.\n"
           "  - --ffb-hw-test-logitech-g923-autocenter is a REAL, G923-SPECIFIC hardware test. It sends\n"
           "    the firmware autocenter-disable command for a fixed 5-second window and then sends the\n"
           "    matching re-enable command. It is locked to VID 046D / PID C266 and the exact expected\n"
           "    HID report layout. Explicit per-run authorization is required. It refuses to run while\n"
           "    another rvwheel_device_probe process (including --bridge) is active.\n"
           "  - Exactly one of --help/--list/--profiles/--calibrate/--monitor/--capture/--bridge/--ffb-simulate/\n"
           "    --ffb-hw-test-stop-only/--ffb-hw-test-autocenter/--ffb-hw-test-weak-effect/--telemetry-monitor/\n"
           "    --logitech-hid-info/--ffb-hw-test-logitech-g923-autocenter\n"
           "    must be given.\n";
}

CliParseResult CliParser::Parse(const std::vector<std::wstring>& args) {
    constexpr const char* kConflictingModeMessage =
        "Conflicting mode flags: only one of "
        "--help/--list/--profiles/--calibrate/--monitor/--capture/--bridge/--ffb-simulate/--ffb-hw-test-stop-only/"
        "--ffb-hw-test-autocenter/--ffb-hw-test-weak-effect/--telemetry-monitor may be given.";

    CliParseResult result;
    bool modeSet = false;
    bool durationSet = false;
    bool rateSet = false;
    bool outputSet = false;
    bool profileSet = false;
    bool parentPidSet = false;
    bool effectSet = false;
    bool ffbCooperativeLevelSet = false;
    bool enableForceFeedbackSet = false;
    long long durationSeconds = kDefaultDurationSeconds;
    long long rateHz = kDefaultRateHz;
    long long parentProcessId = 0;
    FfbTestEffect effect = FfbTestEffect::Spring;
    FfbTestCooperativeLevel ffbCooperativeLevel = FfbTestCooperativeLevel::Background;
    std::filesystem::path capturePath;
    std::filesystem::path calibrateOutputPath;
    std::filesystem::path profilesDirOverride;
    std::wstring profileSelector;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];

        if (arg == L"--help" || arg == L"-h") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::Help;
            modeSet = true;
        } else if (arg == L"--list") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::List;
            modeSet = true;
        } else if (arg == L"--profiles") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::Profiles;
            modeSet = true;
        } else if (arg == L"--calibrate") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::Calibrate;
            modeSet = true;
        } else if (arg == L"--monitor") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::Monitor;
            modeSet = true;
        } else if (arg == L"--bridge") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::Bridge;
            modeSet = true;
        } else if (arg == L"--ffb-simulate") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::FfbSimulate;
            modeSet = true;
        } else if (arg == L"--ffb-hw-test-stop-only") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::FfbHardwareTestStopOnly;
            modeSet = true;
        } else if (arg == L"--ffb-hw-test-autocenter") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::FfbHardwareTestAutoCenter;
            modeSet = true;
        } else if (arg == L"--ffb-hw-test-weak-effect") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::FfbHardwareTestWeakEffect;
            modeSet = true;
        } else if (arg == L"--telemetry-monitor") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::TelemetryMonitor;
            modeSet = true;
        } else if (arg == L"--logitech-hid-info") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::LogitechHidInfo;
            modeSet = true;
        } else if (arg == L"--ffb-hw-test-logitech-g923-autocenter") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            result.options.mode = ProbeMode::FfbHardwareTestLogitechG923AutoCenter;
            modeSet = true;
        } else if (arg == L"--effect") {
            if (i + 1 >= args.size()) {
                return Fail("--effect requires \"spring\" or \"damper\".");
            }
            const std::wstring& value = args[++i];
            if (value == L"spring") {
                effectSet = true;
                effect = FfbTestEffect::Spring;
            } else if (value == L"damper") {
                effectSet = true;
                effect = FfbTestEffect::Damper;
            } else {
                return Fail("--effect must be \"spring\" or \"damper\".");
            }
        } else if (arg == L"--ffb-cooperative-level") {
            if (i + 1 >= args.size()) {
                return Fail("--ffb-cooperative-level requires \"background\", \"foreground\", or \"foreground-focused\".");
            }
            const std::wstring& value = args[++i];
            if (value == L"background") {
                ffbCooperativeLevel = FfbTestCooperativeLevel::Background;
            } else if (value == L"foreground") {
                ffbCooperativeLevel = FfbTestCooperativeLevel::ForegroundUnfocused;
            } else if (value == L"foreground-focused") {
                ffbCooperativeLevel = FfbTestCooperativeLevel::ForegroundFocused;
            } else {
                return Fail("--ffb-cooperative-level must be \"background\", \"foreground\", or \"foreground-focused\".");
            }
            ffbCooperativeLevelSet = true;
        } else if (arg == L"--enable-force-feedback") {
            enableForceFeedbackSet = true;
        } else if (arg == L"--capture") {
            if (modeSet) {
                return Fail(kConflictingModeMessage);
            }
            if (i + 1 >= args.size()) {
                return Fail("--capture requires a file path argument.");
            }
            capturePath = std::filesystem::path(args[++i]);
            if (capturePath.empty()) {
                return Fail("--capture requires a non-empty file path argument.");
            }
            result.options.mode = ProbeMode::Capture;
            modeSet = true;
        } else if (arg == L"--duration") {
            if (i + 1 >= args.size()) {
                return Fail("--duration requires a numeric seconds argument.");
            }
            long long parsed = 0;
            if (!TryParsePositiveInt(args[++i], parsed)) {
                return Fail("--duration must be a positive whole number of seconds.");
            }
            if (parsed < kMinDurationSeconds || parsed > kMaxDurationSeconds) {
                return Fail("--duration must be between " + std::to_string(kMinDurationSeconds) + " and " +
                            std::to_string(kMaxDurationSeconds) + " seconds.");
            }
            durationSeconds = parsed;
            durationSet = true;
        } else if (arg == L"--rate") {
            if (i + 1 >= args.size()) {
                return Fail("--rate requires a numeric Hz argument.");
            }
            long long parsed = 0;
            if (!TryParsePositiveInt(args[++i], parsed)) {
                return Fail("--rate must be a positive whole number of Hz.");
            }
            if (parsed < kMinRateHz || parsed > kMaxRateHz) {
                return Fail("--rate must be between " + std::to_string(kMinRateHz) + " and " + std::to_string(kMaxRateHz) +
                            " Hz.");
            }
            rateHz = parsed;
            rateSet = true;
        } else if (arg == L"--output") {
            if (i + 1 >= args.size()) {
                return Fail("--output requires a file path argument.");
            }
            calibrateOutputPath = std::filesystem::path(args[++i]);
            if (calibrateOutputPath.empty()) {
                return Fail("--output requires a non-empty file path argument.");
            }
            outputSet = true;
        } else if (arg == L"--parent-pid") {
            if (i + 1 >= args.size()) {
                return Fail("--parent-pid requires a numeric process ID argument.");
            }
            long long parsed = 0;
            if (!TryParsePositiveInt(args[++i], parsed) || parsed == 0) {
                return Fail("--parent-pid must be a positive process ID.");
            }
            parentProcessId = parsed;
            parentPidSet = true;
        } else if (arg == L"--profiles-dir") {
            if (i + 1 >= args.size()) {
                return Fail("--profiles-dir requires a directory path argument.");
            }
            profilesDirOverride = std::filesystem::path(args[++i]);
            if (profilesDirOverride.empty()) {
                return Fail("--profiles-dir requires a non-empty directory path argument.");
            }
        } else if (arg == L"--profile") {
            if (i + 1 >= args.size()) {
                return Fail("--profile requires a profileId or a .json path argument.");
            }
            profileSelector = args[++i];
            if (profileSelector.empty()) {
                return Fail("--profile requires a non-empty argument.");
            }
            profileSet = true;
        } else {
            return Fail("Unrecognized argument: " + NarrowForDisplay(arg));
        }
    }

    if (!modeSet) {
        return Fail("No mode given. Specify exactly one of "
                     "--help/--list/--profiles/--calibrate/--monitor/--capture/--bridge/--ffb-simulate/"
                     "--ffb-hw-test-stop-only/--ffb-hw-test-autocenter/--ffb-hw-test-weak-effect/"
                     "--telemetry-monitor/--logitech-hid-info/--ffb-hw-test-logitech-g923-autocenter.");
    }

    if (durationSet && result.options.mode != ProbeMode::Monitor && result.options.mode != ProbeMode::Capture &&
        result.options.mode != ProbeMode::FfbSimulate && result.options.mode != ProbeMode::Bridge &&
        result.options.mode != ProbeMode::TelemetryMonitor) {
        return Fail("--duration only applies to --monitor, --capture, --bridge, --ffb-simulate, and "
                     "--telemetry-monitor.");
    }
    if (rateSet && result.options.mode != ProbeMode::Monitor && result.options.mode != ProbeMode::Capture &&
        result.options.mode != ProbeMode::Bridge && result.options.mode != ProbeMode::FfbSimulate &&
        result.options.mode != ProbeMode::TelemetryMonitor) {
        return Fail("--rate only applies to --monitor, --capture, --bridge, --ffb-simulate, and "
                     "--telemetry-monitor.");
    }
    if (outputSet && result.options.mode != ProbeMode::Calibrate) {
        return Fail("--output only applies to --calibrate.");
    }
    if (profileSet && result.options.mode != ProbeMode::Monitor && result.options.mode != ProbeMode::Capture &&
        result.options.mode != ProbeMode::Bridge && result.options.mode != ProbeMode::FfbSimulate) {
        return Fail("--profile only applies to --monitor, --capture, --bridge, and --ffb-simulate.");
    }
    if (parentPidSet && result.options.mode != ProbeMode::Bridge) {
        return Fail("--parent-pid only applies to --bridge.");
    }
    if (effectSet && result.options.mode != ProbeMode::FfbHardwareTestWeakEffect) {
        return Fail("--effect only applies to --ffb-hw-test-weak-effect.");
    }
    if (ffbCooperativeLevelSet && result.options.mode != ProbeMode::FfbHardwareTestStopOnly &&
        result.options.mode != ProbeMode::FfbHardwareTestAutoCenter &&
        result.options.mode != ProbeMode::FfbHardwareTestWeakEffect) {
        return Fail("--ffb-cooperative-level only applies to real FFB hardware-test modes.");
    }
    if (enableForceFeedbackSet && result.options.mode != ProbeMode::Bridge) {
        return Fail("--enable-force-feedback only applies to --bridge.");
    }

    result.options.duration = std::chrono::seconds{durationSeconds};
    if (durationSet && result.options.mode == ProbeMode::Bridge) {
        // --bridge is the one mode where "no --duration given" must mean
        // "run forever", not "run for the default 30s" -- so the bounded
        // case is represented separately from `duration` above instead of
        // reusing its always-has-a-value default.
        result.options.bridgeDuration = std::chrono::seconds{durationSeconds};
    }
    result.options.rateHz = static_cast<int>(rateHz);
    result.options.parentProcessId = static_cast<std::uint32_t>(parentProcessId);
    result.options.capturePath = std::move(capturePath);
    result.options.calibrateOutputPath = std::move(calibrateOutputPath);
    result.options.profilesDirOverride = std::move(profilesDirOverride);
    result.options.profileSelector = std::move(profileSelector);
    result.options.ffbTestEffect = effect;
    result.options.ffbTestCooperativeLevel = ffbCooperativeLevel;
    result.options.enableForceFeedback = enableForceFeedbackSet;
    result.success = true;
    return result;
}

} // namespace rvwheel::tools::probe
