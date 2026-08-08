#include "DeviceProbeApp.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "CalibrationWizard.hpp"
#include "CaptureWriter.hpp"
#include "ConsoleRenderer.hpp"
#include "DeviceSelection.hpp"
#include "HiddenWindow.hpp"
#include "JsonlFormatter.hpp"
#include "MonitorFrameFormatter.hpp"
#include "ProbeFormatting.hpp"
#include "ProfileLocations.hpp"

#include "rvwheel/dal/DeviceManagerFactory.hpp"
#include "rvwheel/dal/ICalibratableWheelDevice.hpp"
#include "rvwheel/profiles/ProfileLoader.hpp"
#include "rvwheel/profiles/ProfileRepository.hpp"
#include "rvwheel/profiles/ProfileResolver.hpp"

namespace rvwheel::tools::probe {

namespace {

using rvwheel::dal::CreateDefaultDeviceManager;
using rvwheel::dal::DeviceManager;
using rvwheel::dal::DeviceManagerInitParams;
using rvwheel::dal::DeviceReadinessPolicy;
using rvwheel::dal::ICalibratableWheelDevice;
using rvwheel::dal::IWheelDevice;
using rvwheel::dal::LogLevel;
using rvwheel::dal::RawAxisSnapshot;
using rvwheel::dal::Status;
using rvwheel::dal::WheelInputLayout;
using rvwheel::profiles::DeviceProfile;
using rvwheel::profiles::ProfileLoader;
using rvwheel::profiles::ProfileOrigin;
using rvwheel::profiles::ProfileRepository;
using rvwheel::profiles::ProfileResolver;
using rvwheel::profiles::ProfileWithOrigin;

void PrintDiagnostic(LogLevel level, std::string_view message) {
    const char* label = level == LogLevel::Error ? "error" : (level == LogLevel::Warning ? "warning" : "info");
    std::cerr << "[dal-" << label << "] " << message << "\n";
}

[[nodiscard]] std::unique_ptr<DeviceManager> CreateManager(const HiddenWindow& window) {
    DeviceManagerInitParams params{};
    params.instance = window.Instance();
    params.window = window.Handle();
    params.refreshInterval = std::chrono::seconds{5};
    params.diagnostics = &PrintDiagnostic;
    return CreateDefaultDeviceManager(params);
}

void PrintNoDevicesHelp() {
    std::cerr << "No devices were found. If a wheel is physically connected and shows as working in Windows' "
                  "joy.cpl, this can still happen because this process cannot reach DirectInput in the current "
                  "session (e.g. a sandboxed/service/CI context with no interactive desktop), or because the "
                  "device does not expose an axis this backend maps to steering/throttle/brake/clutch.\n"
                  "Try running this exact command from a normal interactive desktop PowerShell window.\n";
}

[[nodiscard]] IWheelDevice* FindDeviceByIndex(DeviceManager& manager, std::size_t index) {
    std::size_t i = 0;
    for (IWheelDevice* candidate : manager.Devices()) {
        if (i == index) {
            return candidate;
        }
        ++i;
    }
    return nullptr;
}

[[nodiscard]] ProfileRepository LoadRepository(const CliOptions& options) {
    const std::filesystem::path builtInDir = ResolveBuiltInProfilesDirectory();
    const std::filesystem::path userDir = options.profilesDirOverride.empty() ? ResolveUserProfilesDirectory() : options.profilesDirOverride;
    return ProfileRepository(builtInDir, userDir);
}

// Best-effort, lossy narrowing: profileIds are always plain ASCII by this
// project's own schema convention, so this is exact for every valid id
// and simply never matches for anything else -- never a crash.
[[nodiscard]] std::string NarrowAscii(const std::wstring& text) {
    std::string result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        result.push_back((ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '?');
    }
    return result;
}

struct OverrideResolution {
    bool found = false;
    DeviceProfile profile;
    bool isUserProfile = true;
    std::string error;
};

[[nodiscard]] OverrideResolution ResolveOverrideProfile(const std::wstring& selector, const std::vector<ProfileWithOrigin>& merged) {
    OverrideResolution result;

    std::error_code ec;
    const std::filesystem::path asPath(selector);
    if (!asPath.empty() && std::filesystem::exists(asPath, ec) && !ec) {
        const auto parseResult = ProfileLoader::ParseFromFile(asPath);
        if (!parseResult.IsOk()) {
            std::string message = "Failed to load profile from \"" + asPath.string() + "\": ";
            for (const auto& error : parseResult.errors) {
                message += (error.path.empty() ? "" : (error.path + ": ")) + error.message + "; ";
            }
            result.error = message;
            return result;
        }
        result.found = true;
        result.profile = *parseResult.profile;
        result.isUserProfile = true;
        return result;
    }

    const std::string selectorNarrow = NarrowAscii(selector);
    for (const auto& entry : merged) {
        if (entry.profile.profileId == selectorNarrow) {
            result.found = true;
            result.profile = entry.profile;
            result.isUserProfile = entry.isUserProfile;
            return result;
        }
    }
    result.error = "No profile found matching \"" + selectorNarrow + "\" (not an existing file path, and no profileId matches).";
    return result;
}

struct AppliedProfileInfo {
    std::string profileId;
    ProfileOrigin origin = ProfileOrigin::Unconfigured;
    std::string reason;
};

[[nodiscard]] AppliedProfileInfo ResolveAndApplyProfile(IWheelDevice& device, const CliOptions& options, const ProfileRepository& repo) {
    AppliedProfileInfo info;

    std::vector<rvwheel::dal::AxisSource> knownAxes;
    if (auto* calibratable = dynamic_cast<ICalibratableWheelDevice*>(&device)) {
        for (const auto& axis : calibratable->EnumerateRawAxes()) {
            knownAxes.push_back(axis.source);
        }
    }

    const auto merged = repo.MergedProfiles();

    if (!options.profileSelector.empty()) {
        const OverrideResolution overrideResult = ResolveOverrideProfile(options.profileSelector, merged);
        if (!overrideResult.found) {
            info.reason = overrideResult.error;
            device.ApplyLayout(WheelInputLayout{}, DeviceReadinessPolicy{});
            return info;
        }
        const Status applyStatus = device.ApplyLayout(overrideResult.profile.layout, overrideResult.profile.readiness);
        if (!applyStatus.IsOk()) {
            info.reason = "Profile \"" + overrideResult.profile.profileId + "\" failed to apply: " + applyStatus.Message();
            device.ApplyLayout(WheelInputLayout{}, DeviceReadinessPolicy{});
            return info;
        }
        info.profileId = overrideResult.profile.profileId;
        info.origin = overrideResult.isUserProfile ? ProfileOrigin::UserProfile : ProfileOrigin::BuiltInProfile;
        info.reason = "Forced via --profile.";
        return info;
    }

    const auto resolution = ProfileResolver::Resolve(merged, device.Info(), knownAxes);
    info.origin = resolution.origin;
    info.reason = resolution.reason;
    if (resolution.profile) {
        info.profileId = resolution.profile->profileId;
    }

    const bool shouldApply = resolution.origin == ProfileOrigin::BuiltInProfile || resolution.origin == ProfileOrigin::UserProfile ||
                              resolution.origin == ProfileOrigin::ProvisionalFallback;
    if (shouldApply) {
        const Status applyStatus = device.ApplyLayout(resolution.layout, resolution.readiness);
        if (!applyStatus.IsOk()) {
            info.reason += " (ApplyLayout failed unexpectedly: " + applyStatus.Message() + ")";
            info.origin = ProfileOrigin::Unconfigured;
            device.ApplyLayout(WheelInputLayout{}, DeviceReadinessPolicy{});
        }
    } else {
        // AmbiguousMatch / InvalidExactMatch / Unconfigured: never guess.
        device.ApplyLayout(WheelInputLayout{}, DeviceReadinessPolicy{});
    }

    return info;
}

void PrintDeviceSummary(IWheelDevice& device, const AppliedProfileInfo& profileInfo) {
    const auto& info = device.Info();
    std::cout << "  id           " << FormatDeviceIdHex(info.id) << "\n";
    std::cout << "  name         " << info.name << "\n";
    std::cout << "  manufacturer " << (info.manufacturer.empty() ? "unknown" : info.manufacturer) << "\n";
    std::cout << "  backend      " << FormatBackend(info.backend) << "\n";
    std::cout << "  " << FormatVendorProductId(info.vendorId, info.productId) << "\n";
    std::cout << "  connected    " << (device.IsConnected() ? "true" : "false") << "\n";
    std::cout << "  profile      " << (profileInfo.profileId.empty() ? "(none)" : profileInfo.profileId) << "  origin="
              << FormatProfileOrigin(profileInfo.origin) << "\n";
    std::cout << "  reason       " << profileInfo.reason << "\n";
    std::cout << "  readiness    " << FormatReadinessState(device.State().readiness) << "\n";
    std::cout << "  capabilities steering=" << info.capabilities.hasSteering << " throttle=" << info.capabilities.hasThrottle
              << " brake=" << info.capabilities.hasBrake << " clutch=" << info.capabilities.hasClutch
              << " forceFeedback=" << info.capabilities.hasForceFeedback << "\n";
    std::cout << "  buttons      " << info.capabilities.buttonCount << "\n";
    std::cout << "  povs         " << static_cast<int>(info.capabilities.povCount) << "\n";
}

} // namespace

DeviceProbeApp::DeviceProbeApp(CliOptions options, std::atomic<bool>& stopRequested)
    : options_(std::move(options)), stopRequested_(stopRequested) {}

int DeviceProbeApp::Run() {
    switch (options_.mode) {
        case ProbeMode::Help:
            std::cout << CliParser::UsageText() << "\n";
            return 0;
        case ProbeMode::List:
            return RunList();
        case ProbeMode::Profiles:
            return RunProfiles();
        case ProbeMode::Calibrate:
            return RunCalibrate();
        case ProbeMode::Monitor:
            return RunMonitor();
        case ProbeMode::Capture:
            return RunCapture();
    }
    return 1;
}

int DeviceProbeApp::RunList() {
    HiddenWindow window;
    if (!window.IsValid()) {
        std::cerr << "Failed to create the hidden Win32 window required for DirectInput. This is an "
                      "operational/environment problem, not a code defect; try running this exact command from a "
                      "normal interactive desktop PowerShell session (not a headless/service context).\n";
        return 1;
    }

    auto manager = CreateManager(window);
    manager->RefreshIfDue();
    window.PumpMessages();

    const std::size_t count = manager->DeviceCount();
    std::cout << "Enumerated " << count << " wheel device(s).\n\n";

    if (count == 0) {
        PrintNoDevicesHelp();
        return 1;
    }

    const ProfileRepository repo = LoadRepository(options_);

    std::size_t index = 0;
    for (IWheelDevice* device : manager->Devices()) {
        const AppliedProfileInfo profileInfo = ResolveAndApplyProfile(*device, options_, repo);
        // IWheelDevice::IsConnected()/State().readiness only reflect the
        // last Poll() outcome (never queried live on their own -- see
        // IWheelDevice.hpp); enumeration/ApplyLayout alone never polls
        // anything, so a fresh, genuinely connected device would
        // otherwise misleadingly print "connected false".
        device->Poll();

        std::cout << "Device #" << index << ":\n";
        PrintDeviceSummary(*device, profileInfo);
        std::cout << "\n";
        ++index;
    }
    return 0;
}

int DeviceProbeApp::RunProfiles() {
    const ProfileRepository repo = LoadRepository(options_);

    std::cout << "Built-in profiles directory: "
              << (repo.BuiltInDirectory().empty() ? "(not found)" : repo.BuiltInDirectory().string()) << "\n";
    std::cout << "User profiles directory:     " << repo.UserDirectory().string() << "\n\n";

    const auto merged = repo.MergedProfiles();
    std::cout << merged.size() << " profile(s) available:\n\n";
    for (const auto& entry : merged) {
        std::cout << "  " << entry.profile.profileId << "  (" << (entry.isUserProfile ? "user" : "built-in") << ")\n";
        std::cout << "    displayName: " << entry.profile.displayName << "\n";
        std::cout << "    match:       backend=" << FormatBackend(entry.profile.match.backend);
        if (entry.profile.match.vendorId && entry.profile.match.productId) {
            std::cout << " " << FormatVendorProductId(entry.profile.match.vendorId, entry.profile.match.productId);
        } else {
            std::cout << " (generic fallback profile for this backend)";
        }
        std::cout << "\n";
    }

    if (!repo.Issues().empty()) {
        std::cout << "\n" << repo.Issues().size() << " issue(s) while loading profiles:\n";
        for (const auto& issue : repo.Issues()) {
            std::cout << "  " << issue.path.string() << ": " << issue.message << "\n";
        }
    }

    return 0;
}

int DeviceProbeApp::RunCalibrate() {
    HiddenWindow window;
    if (!window.IsValid()) {
        std::cerr << "Failed to create the hidden Win32 window required for DirectInput.\n";
        return 1;
    }

    auto manager = CreateManager(window);
    manager->RefreshIfDue();
    window.PumpMessages();

    const auto selection = SelectDeviceForMonitoring(manager->DeviceCount());
    if (selection.outcome == DeviceSelectionOutcome::NoDevices) {
        PrintNoDevicesHelp();
        return 1;
    }
    if (selection.outcome == DeviceSelectionOutcome::MultipleDevicesUsingFirst) {
        std::cout << manager->DeviceCount() << " devices were found; calibrating device #0. Run --list to see all of them.\n";
    }

    IWheelDevice* device = FindDeviceByIndex(*manager, selection.selectedIndex);
    if (device == nullptr) {
        std::cerr << "Internal error: selected device index was not found after enumeration.\n";
        return 1;
    }

    auto* calibratable = dynamic_cast<ICalibratableWheelDevice*>(device);
    if (calibratable == nullptr) {
        std::cerr << "This device's backend does not support calibration (only DirectInput does in this build).\n";
        return 1;
    }

    const auto axes = calibratable->EnumerateRawAxes();
    if (axes.empty()) {
        std::cerr << "No raw axes were discovered on this device; nothing to calibrate.\n";
        return 1;
    }

    std::cout << "Calibrating: " << device->Info().name << "\n";
    std::cout << "Discovered axes (source: rawMin..rawMax, current):\n";
    for (const auto& axis : axes) {
        RawAxisSnapshot snapshot{};
        calibratable->PollRawAxes(snapshot);
        std::int32_t current = 0;
        for (std::uint8_t i = 0; i < snapshot.count; ++i) {
            if (snapshot.samples[i].source == axis.source) {
                current = snapshot.samples[i].rawValue;
                break;
            }
        }
        std::cout << "  " << rvwheel::dal::ToString(axis.source) << ": " << axis.rawMin << ".." << axis.rawMax << ", "
                  << current << "\n";
    }
    std::cout << "\n";

    CalibrationWizard wizard(axes);

    const auto runStep = [&](const char* prompt) -> CalibrationStepOutcome {
        for (;;) {
            if (stopRequested_.load()) {
                return CalibrationStepOutcome::NoMovement; // Caller checks stopRequested_ separately and aborts.
            }
            std::cout << prompt << " Press Enter when ready...";
            std::cout.flush();
            std::string line;
            std::getline(std::cin, line);
            if (stopRequested_.load()) {
                return CalibrationStepOutcome::NoMovement;
            }

            window.PumpMessages();
            RawAxisSnapshot snapshot{};
            calibratable->PollRawAxes(snapshot);

            const CalibrationStepOutcome outcome = wizard.SubmitSnapshot(snapshot);
            switch (outcome) {
                case CalibrationStepOutcome::Recorded:
                    return outcome;
                case CalibrationStepOutcome::Ambiguous:
                    std::cout << "  -> More than one axis moved; move ONLY the requested control and try again.\n";
                    break;
                case CalibrationStepOutcome::NoMovement:
                    std::cout << "  -> No axis moved enough; actually move the requested control and try again.\n";
                    break;
                case CalibrationStepOutcome::Inconsistent:
                    std::cout << "  -> A different axis moved than in the previous step; use the same control and try again.\n";
                    break;
            }
        }
    };

    const struct {
        const char* prompt;
    } kSteps[] = {
        {"Release ALL controls (steering centered, no pedals pressed)."},
        {"Keep steering centered."},
        {"Turn the wheel FULLY LEFT and hold it."},
        {"Turn the wheel FULLY RIGHT and hold it."},
        {"Release the throttle pedal fully."},
        {"Press the throttle pedal FULLY and hold it."},
        {"Release the brake pedal fully."},
        {"Press the brake pedal FULLY and hold it."},
    };
    for (const auto& stepInfo : kSteps) {
        runStep(stepInfo.prompt);
        if (stopRequested_.load()) {
            std::cout << "\nCancelled.\n";
            return 1;
        }
    }

    std::cout << "Does this device have a clutch pedal you want to calibrate? [y/N] ";
    std::cout.flush();
    std::string clutchAnswer;
    std::getline(std::cin, clutchAnswer);
    const bool wantsClutch = !clutchAnswer.empty() && (clutchAnswer.front() == 'y' || clutchAnswer.front() == 'Y');
    if (!wantsClutch) {
        wizard.SkipClutch();
    } else {
        const struct {
            const char* prompt;
        } kClutchSteps[] = {
            {"Release the clutch pedal fully."},
            {"Press the clutch pedal FULLY and hold it."},
        };
        for (const auto& stepInfo : kClutchSteps) {
            runStep(stepInfo.prompt);
            if (stopRequested_.load()) {
                std::cout << "\nCancelled.\n";
                return 1;
            }
        }
    }

    const CalibrationResult result = wizard.Finish();
    if (!result.success) {
        std::cerr << "Calibration failed: " << result.failureReason << "\n";
        return 1;
    }

    std::cout << "\nCalibration summary:\n";
    for (const auto& line : result.summaryLines) {
        std::cout << "  " << line << "\n";
    }

    std::cout << "\nSave this profile? [y/N] ";
    std::cout.flush();
    std::string saveAnswer;
    std::getline(std::cin, saveAnswer);
    if (saveAnswer.empty() || (saveAnswer.front() != 'y' && saveAnswer.front() != 'Y')) {
        std::cout << "Not saved.\n";
        return 0;
    }

    DeviceProfile profile;
    profile.schemaVersion = 1;
    if (device->Info().vendorId && device->Info().productId) {
        std::ostringstream idStream;
        idStream << "generated-" << std::hex << std::setfill('0') << std::setw(4) << *device->Info().vendorId << "-"
                 << std::setw(4) << *device->Info().productId;
        profile.profileId = idStream.str();
    } else {
        profile.profileId = "generated-" + FormatDeviceIdHex(device->Info().id);
    }
    profile.displayName = device->Info().name + " (generated by --calibrate)";
    profile.match.backend = device->Info().backend;
    profile.match.vendorId = device->Info().vendorId;
    profile.match.productId = device->Info().productId;
    profile.layout = result.layout;
    // The wizard does not measure a startup transient duration (that
    // requires observing the device across a cold acquire, not a single
    // interactive session); use the same conservative default the
    // provisional fallback uses rather than guessing a number.
    profile.readiness = DeviceReadinessPolicy::ConservativeDefault();
    if (device->Info().capabilities.buttonCount > 0) {
        profile.expectedButtonCount = device->Info().capabilities.buttonCount;
    }
    if (device->Info().capabilities.povCount > 0) {
        profile.expectedPovCount = device->Info().capabilities.povCount;
    }

    std::filesystem::path outputPath = options_.calibrateOutputPath;
    if (outputPath.empty()) {
        const std::filesystem::path userDir =
            options_.profilesDirOverride.empty() ? ResolveUserProfilesDirectory() : options_.profilesDirOverride;
        if (userDir.empty()) {
            std::cerr << "Could not determine a user profiles directory (LOCALAPPDATA not set); pass --output <path> "
                         "explicitly.\n";
            return 1;
        }
        outputPath = userDir / (profile.profileId + ".json");
    }

    std::error_code existsEc;
    if (std::filesystem::exists(outputPath, existsEc) && !existsEc) {
        std::cout << "\"" << outputPath.string() << "\" already exists. Overwrite? [y/N] ";
        std::cout.flush();
        std::string overwriteAnswer;
        std::getline(std::cin, overwriteAnswer);
        if (overwriteAnswer.empty() || (overwriteAnswer.front() != 'y' && overwriteAnswer.front() != 'Y')) {
            std::cout << "Not saved (existing file kept).\n";
            return 0;
        }
    }

    std::error_code mkdirEc;
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path(), mkdirEc);
    }

    std::filesystem::path tempPath = outputPath;
    tempPath += L".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "Failed to open \"" << tempPath.string() << "\" for writing.\n";
            return 1;
        }
        out << ProfileLoader::Serialize(profile);
    }
    if (!MoveFileExW(tempPath.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::cerr << "Failed to finalize profile save to \"" << outputPath.string() << "\".\n";
        return 1;
    }

    std::cout << "Saved: " << outputPath.string() << "\n";

    const auto reloadResult = ProfileLoader::ParseFromFile(outputPath);
    if (!reloadResult.IsOk()) {
        std::cerr << "Warning: the just-saved profile failed to reload cleanly:\n";
        for (const auto& error : reloadResult.errors) {
            std::cerr << "  " << error.path << ": " << error.message << "\n";
        }
        return 1;
    }
    std::cout << "Reload check: OK (profileId=\"" << reloadResult.profile->profileId << "\").\n";

    return 0;
}

int DeviceProbeApp::RunMonitor() {
    HiddenWindow window;
    if (!window.IsValid()) {
        std::cerr << "Failed to create the hidden Win32 window required for DirectInput.\n";
        return 1;
    }

    auto manager = CreateManager(window);
    manager->RefreshIfDue();
    window.PumpMessages();

    const auto selection = SelectDeviceForMonitoring(manager->DeviceCount());
    if (selection.outcome == DeviceSelectionOutcome::NoDevices) {
        PrintNoDevicesHelp();
        return 1;
    }
    if (selection.outcome == DeviceSelectionOutcome::MultipleDevicesUsingFirst) {
        std::cout << manager->DeviceCount()
                  << " devices were found; this tool has no --device selector yet, so device #0 is used. Run "
                     "--list to see all of them.\n";
    }

    IWheelDevice* device = FindDeviceByIndex(*manager, selection.selectedIndex);
    if (device == nullptr) {
        std::cerr << "Internal error: selected device index was not found after enumeration.\n";
        return 1;
    }

    const ProfileRepository repo = LoadRepository(options_);
    const AppliedProfileInfo profileInfo = ResolveAndApplyProfile(*device, options_, repo);
    std::cout << "Profile: " << (profileInfo.profileId.empty() ? "(none)" : profileInfo.profileId) << "  origin="
              << FormatProfileOrigin(profileInfo.origin) << "\n";
    std::cout << "Reason:  " << profileInfo.reason << "\n\n";

    ConsoleRenderer renderer;
    const auto start = std::chrono::steady_clock::now();
    const auto frameInterval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / options_.rateHz));

    std::uint64_t pollCount = 0;
    std::uint64_t failedPolls = 0;
    std::uint64_t droppedFrames = 0;
    bool hasBaseline = false;
    bool wasConnected = false;
    Status lastStatus = Status::Ok();

    auto nextTick = start;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - start;
        if (elapsed >= options_.duration || stopRequested_.load()) {
            break;
        }

        window.PumpMessages();
        manager->RefreshIfDue();

        lastStatus = device->Poll();
        ++pollCount;
        if (!lastStatus.IsOk()) {
            ++failedPolls;
        }

        const bool nowConnected = device->IsConnected();
        if (!hasBaseline) {
            wasConnected = nowConnected;
            hasBaseline = true;
        } else if (nowConnected != wasConnected) {
            renderer.RenderLine(nowConnected ? "[transition] device reconnected" : "[transition] device disconnected");
            wasConnected = nowConnected;
        }

        const auto& state = device->State();
        const auto& caps = device->Info().capabilities;

        MonitorFrameData frame{};
        frame.deviceName = device->Info().name;
        frame.backend = FormatBackend(device->Info().backend);
        frame.deviceIdHex = FormatDeviceIdHex(device->Info().id);
        frame.profileId = profileInfo.profileId;
        frame.profileOrigin = FormatProfileOrigin(profileInfo.origin);
        frame.readinessState = FormatReadinessState(state.readiness);
        frame.elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        frame.durationSeconds = std::chrono::duration<double>(options_.duration).count();
        frame.connected = state.connected;
        frame.valid = state.valid;
        frame.sampleCounter = state.sampleCounter;
        frame.steering = state.steering;
        frame.throttle = state.throttle;
        frame.brake = state.brake;
        frame.clutch = caps.hasClutch ? std::optional<float>(state.clutch) : std::nullopt;
        frame.pressedButtons = PressedButtonIndices(state.buttons, caps.buttonCount);
        frame.povs = FormatActivePovs(state.povs, state.povCount);
        frame.observedPollHz = frame.elapsedSeconds > 0.0 ? static_cast<double>(pollCount) / frame.elapsedSeconds : 0.0;
        frame.droppedFrames = droppedFrames;
        frame.failedPolls = failedPolls;
        frame.lastPollStatus = FormatStatusCode(lastStatus.Code());

        renderer.RenderFrame(MonitorFrameFormatter::FormatFrame(frame));

        nextTick += frameInterval;
        const auto afterWork = std::chrono::steady_clock::now();
        if (afterWork > nextTick) {
            ++droppedFrames; // Fell behind schedule; catch up without busy-spinning.
            nextTick = afterWork;
        } else {
            std::this_thread::sleep_until(nextTick);
        }
    }

    std::cout << "\nMonitor finished: " << pollCount << " polls, " << failedPolls << " failed, " << droppedFrames
              << " dropped.\n";
    return 0;
}

int DeviceProbeApp::RunCapture() {
    HiddenWindow window;
    if (!window.IsValid()) {
        std::cerr << "Failed to create the hidden Win32 window required for DirectInput.\n";
        return 1;
    }

    auto manager = CreateManager(window);
    manager->RefreshIfDue();
    window.PumpMessages();

    const auto selection = SelectDeviceForMonitoring(manager->DeviceCount());
    if (selection.outcome == DeviceSelectionOutcome::NoDevices) {
        PrintNoDevicesHelp();
        return 1;
    }
    if (selection.outcome == DeviceSelectionOutcome::MultipleDevicesUsingFirst) {
        std::cout << manager->DeviceCount() << " devices were found; capturing device #0 (no --device selector yet).\n";
    }

    IWheelDevice* device = FindDeviceByIndex(*manager, selection.selectedIndex);
    if (device == nullptr) {
        std::cerr << "Internal error: selected device index was not found after enumeration.\n";
        return 1;
    }

    const ProfileRepository repo = LoadRepository(options_);
    const AppliedProfileInfo profileInfo = ResolveAndApplyProfile(*device, options_, repo);
    std::cout << "Profile: " << (profileInfo.profileId.empty() ? "(none)" : profileInfo.profileId) << "  origin="
              << FormatProfileOrigin(profileInfo.origin) << "\n";
    std::cout << "Reason:  " << profileInfo.reason << "\n\n";

    CaptureWriter writer(options_.capturePath);
    if (!writer.IsOpen()) {
        std::cerr << "Failed to open capture sidecar file for writing.\n";
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto frameInterval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / options_.rateHz));
    constexpr std::chrono::milliseconds kFlushInterval{1000};

    std::uint64_t pollCount = 0;
    std::uint64_t failedPolls = 0;
    std::uint64_t droppedFrames = 0;
    bool hasBaseline = false;
    bool wasConnected = false;

    auto nextTick = start;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - start;
        if (elapsed >= options_.duration || stopRequested_.load()) {
            break;
        }

        window.PumpMessages();
        manager->RefreshIfDue();

        const Status pollStatus = device->Poll();
        ++pollCount;
        if (!pollStatus.IsOk()) {
            ++failedPolls;
        }

        const bool nowConnected = device->IsConnected();
        if (!hasBaseline) {
            wasConnected = nowConnected;
            hasBaseline = true;
        } else if (nowConnected != wasConnected) {
            std::cout << (nowConnected ? "[transition] device reconnected\n" : "[transition] device disconnected\n");
            wasConnected = nowConnected;
        }

        const auto& state = device->State();
        const auto& caps = device->Info().capabilities;

        WheelSampleRecord record{};
        record.schemaVersion = 2;
        record.elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        record.deviceId = device->Info().id.Value();
        record.backend = FormatBackend(device->Info().backend);
        record.connected = state.connected;
        record.valid = state.valid;
        record.sampleCounter = state.sampleCounter;
        record.steering = state.steering;
        record.throttle = state.throttle;
        record.brake = state.brake;
        record.clutch = caps.hasClutch ? std::optional<float>(state.clutch) : std::nullopt;
        record.pressedButtons = PressedButtonIndices(state.buttons, caps.buttonCount);
        record.povs = FormatActivePovs(state.povs, state.povCount);
        record.pollStatus = FormatStatusCode(pollStatus.Code());
        record.profileId = profileInfo.profileId;
        record.profileOrigin = FormatProfileOrigin(profileInfo.origin);
        record.readinessState = FormatReadinessState(state.readiness);

        writer.WriteLine(JsonlFormatter::FormatLine(record));
        writer.FlushIfDue(now, kFlushInterval);

        nextTick += frameInterval;
        const auto afterWork = std::chrono::steady_clock::now();
        if (afterWork > nextTick) {
            ++droppedFrames;
            nextTick = afterWork;
        } else {
            std::this_thread::sleep_until(nextTick);
        }
    }

    writer.Finalize();

    std::cout << "\nCapture finished: " << pollCount << " samples (" << failedPolls << " failed polls, " << droppedFrames
              << " dropped frames) written to " << writer.FinalPath().string() << "\n";
    return 0;
}

} // namespace rvwheel::tools::probe
