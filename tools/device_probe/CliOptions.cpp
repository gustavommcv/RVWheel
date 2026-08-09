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
           "  rvwheel_device_probe --bridge [--rate <hz>] [--profile <id-or-path>]\n"
           "\n"
           "Options:\n"
           "  --duration <seconds>   How long --monitor/--capture run. Range [1, 3600], default 30.\n"
           "  --rate <hz>            Poll rate for --monitor/--capture/--bridge. Range [1, 250], default 60.\n"
           "  --profile <id-or-path> Force a specific profile (by profileId or a .json path) instead of automatic\n"
           "                         resolution. Only valid with --monitor/--capture/--bridge.\n"
           "  --output <path>        Where --calibrate saves the generated profile. Defaults to a name derived\n"
           "                         from the device under the user profiles directory. Only valid with --calibrate.\n"
           "  --profiles-dir <path>  Overrides the user profiles directory (where --calibrate saves and user\n"
           "                         overrides load from) instead of %LOCALAPPDATA%\\RVWheel\\profiles. Valid with\n"
           "                         any mode.\n"
           "\n"
           "Notes:\n"
           "  - Hardware is re-enumerated at most once every 5 seconds; not configurable here.\n"
           "  - Force feedback is never applied by this tool.\n"
           "  - --bridge runs until Ctrl+C and publishes the latest safe input snapshot under LOCALAPPDATA.\n"
           "  - Exactly one of --help/--list/--profiles/--calibrate/--monitor/--capture/--bridge must be given.\n";
}

CliParseResult CliParser::Parse(const std::vector<std::wstring>& args) {
    constexpr const char* kConflictingModeMessage =
        "Conflicting mode flags: only one of --help/--list/--profiles/--calibrate/--monitor/--capture/--bridge may be given.";

    CliParseResult result;
    bool modeSet = false;
    bool durationSet = false;
    bool rateSet = false;
    bool outputSet = false;
    bool profileSet = false;
    long long durationSeconds = kDefaultDurationSeconds;
    long long rateHz = kDefaultRateHz;
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
        return Fail("No mode given. Specify exactly one of --help/--list/--profiles/--calibrate/--monitor/--capture/--bridge.");
    }

    if (durationSet && result.options.mode != ProbeMode::Monitor && result.options.mode != ProbeMode::Capture) {
        return Fail("--duration only applies to --monitor and --capture.");
    }
    if (rateSet && result.options.mode != ProbeMode::Monitor && result.options.mode != ProbeMode::Capture &&
        result.options.mode != ProbeMode::Bridge) {
        return Fail("--rate only applies to --monitor, --capture, and --bridge.");
    }
    if (outputSet && result.options.mode != ProbeMode::Calibrate) {
        return Fail("--output only applies to --calibrate.");
    }
    if (profileSet && result.options.mode != ProbeMode::Monitor && result.options.mode != ProbeMode::Capture &&
        result.options.mode != ProbeMode::Bridge) {
        return Fail("--profile only applies to --monitor, --capture, and --bridge.");
    }

    result.options.duration = std::chrono::seconds{durationSeconds};
    result.options.rateHz = static_cast<int>(rateHz);
    result.options.capturePath = std::move(capturePath);
    result.options.calibrateOutputPath = std::move(calibrateOutputPath);
    result.options.profilesDirOverride = std::move(profilesDirOverride);
    result.options.profileSelector = std::move(profileSelector);
    result.success = true;
    return result;
}

} // namespace rvwheel::tools::probe
