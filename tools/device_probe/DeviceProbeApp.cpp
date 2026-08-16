#include "DeviceProbeApp.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "CalibrationWizard.hpp"
#include "BridgeForceFeedbackSession.hpp"
#include "BridgePolicies.hpp"
#include "BridgeStateFormatter.hpp"
#include "CaptureWriter.hpp"
#include "ConsoleRenderer.hpp"
#include "DeviceSelection.hpp"
#include "HiddenWindow.hpp"
#include "LogitechHidInspector.hpp"
#include "JsonlFormatter.hpp"
#include "MonitorFrameFormatter.hpp"
#include "ProbeFormatting.hpp"
#include "ProfileLocations.hpp"
#include "StableRawAxisSampler.hpp"
#include "VehicleTelemetryTransport.hpp"

#include "rvwheel/dal/DeviceManagerFactory.hpp"
#include "rvwheel/dal/ICalibratableWheelDevice.hpp"
#include "rvwheel/ffb/ForceFeedbackEngine.hpp"
#include "rvwheel/ffb/SpeedSensitiveSpringSource.hpp"
#include "rvwheel/ffb/SpringDamperSource.hpp"
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

// The structural guarantee behind --ffb-simulate: this class implements
// IWheelDevice by forwarding every read-only/layout method to a real
// device, but ApplyForceFeedback/StopForceFeedback are intercepted here and
// never call through to `real_`. The force feedback engine only ever talks
// to whatever IWheelDevice reference it is given -- by giving it this sink
// instead of the real device, "never sends a real force" is true by
// construction, not by a runtime flag that could be forgotten or bypassed.
class SimulatedForceFeedbackSink final : public IWheelDevice {
public:
    explicit SimulatedForceFeedbackSink(IWheelDevice& real) : real_(real) {}

    [[nodiscard]] const rvwheel::dal::DeviceInfo& Info() const noexcept override { return real_.Info(); }
    [[nodiscard]] bool IsConnected() const noexcept override { return real_.IsConnected(); }
    Status Poll() noexcept override { return real_.Poll(); }
    [[nodiscard]] const rvwheel::dal::WheelState& State() const noexcept override { return real_.State(); }
    Status ApplyLayout(const WheelInputLayout& layout, const DeviceReadinessPolicy& policy) noexcept override {
        return real_.ApplyLayout(layout, policy);
    }

    Status ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand& command) noexcept override {
        ++applyCount;
        lastCommand = command;
        return Status::Ok();
    }
    Status StopForceFeedback() noexcept override {
        ++stopCount;
        return Status::Ok();
    }

    int applyCount = 0;
    int stopCount = 0;
    rvwheel::dal::ForceFeedbackCommand lastCommand{};

private:
    IWheelDevice& real_;
};

void PrintDiagnostic(LogLevel level, std::string_view message) {
    const char* label = level == LogLevel::Error ? "error" : (level == LogLevel::Warning ? "warning" : "info");
    std::cerr << "[dal-" << label << "] " << message << "\n";
}

[[nodiscard]] std::unique_ptr<DeviceManager> CreateManager(const HiddenWindow& window,
                                                            bool requestExclusiveForceFeedbackAccess = false,
                                                            rvwheel::dal::ForceFeedbackCooperativeLevel forceFeedbackLevel =
                                                                rvwheel::dal::ForceFeedbackCooperativeLevel::Background) {
    DeviceManagerInitParams params{};
    params.instance = window.Instance();
    params.window = window.Handle();
    params.refreshInterval = std::chrono::seconds{5};
    params.diagnostics = &PrintDiagnostic;
    // Every mode except the explicit real-hardware FFB test keeps this
    // false, matching the input-preserving default this project has always
    // shipped: reading the wheel must not block G HUB or the game from
    // reading it too. See DirectInputDeviceEnumerator.cpp.
    params.requestExclusiveForceFeedbackAccess = requestExclusiveForceFeedbackAccess;
    params.forceFeedbackCooperativeLevel = forceFeedbackLevel;
    return CreateDefaultDeviceManager(params);
}

[[nodiscard]] bool RequestsForegroundFfb(const CliOptions& options) noexcept {
    return options.ffbTestCooperativeLevel != FfbTestCooperativeLevel::Background;
}

[[nodiscard]] bool RequestsFocusedForegroundFfb(const CliOptions& options) noexcept {
    return options.ffbTestCooperativeLevel == FfbTestCooperativeLevel::ForegroundFocused;
}

[[nodiscard]] rvwheel::dal::ForceFeedbackCooperativeLevel RequestedFfbCooperativeLevel(
    const CliOptions& options) noexcept {
    return RequestsForegroundFfb(options) ? rvwheel::dal::ForceFeedbackCooperativeLevel::Foreground
                                          : rvwheel::dal::ForceFeedbackCooperativeLevel::Background;
}

[[nodiscard]] HiddenWindowMode RequestedFfbWindowMode(const CliOptions& options) noexcept {
    if (RequestsFocusedForegroundFfb(options)) {
        return HiddenWindowMode::TopLevelFocused;
    }
    return RequestsForegroundFfb(options) ? HiddenWindowMode::TopLevelUnfocused : HiddenWindowMode::MessageOnly;
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

    // The actually-applied profile's own forceFeedback block, carried
    // forward directly from whichever DeviceProfile ApplyLayout() was
    // called with (below) -- never re-derived by searching mergedProfiles
    // for profileId again. ProfileRepository::MergeProfiles() collapses a
    // built-in and a same-profileId user override into ONE entry (the user
    // one wins outright; there is no second, built-in candidate to fall
    // back to), so a second lookup by profileId alone would either be
    // redundant or, for a --profile <path> override pointing outside the
    // merged set entirely, silently miss the override's own block. Absent
    // whenever no profile was actually applied to the device.
    std::optional<rvwheel::ffb::ForceFeedbackConfig> forceFeedback;
};

enum class CalibrationConsoleKey {
    Enter,
    Yes,
    No,
};

// Reads Win32 console input records without blocking the thread that owns
// the hidden window and DirectInput device. This lets calibration keep
// pumping messages and polling hardware while the user prepares a control.
class NonBlockingConsoleInput {
public:
    NonBlockingConsoleInput() : input_(GetStdHandle(STD_INPUT_HANDLE)) {
        DWORD mode = 0;
        interactive_ = input_ != nullptr && input_ != INVALID_HANDLE_VALUE && GetConsoleMode(input_, &mode) != FALSE;
    }

    [[nodiscard]] bool IsInteractive() const noexcept { return interactive_; }

    void DiscardPendingEvents() const noexcept {
        if (interactive_) {
            FlushConsoleInputBuffer(input_);
        }
    }

    [[nodiscard]] std::optional<CalibrationConsoleKey> TryReadKey() const noexcept {
        if (!interactive_) {
            return std::nullopt;
        }

        DWORD available = 0;
        if (!GetNumberOfConsoleInputEvents(input_, &available) || available == 0) {
            return std::nullopt;
        }

        INPUT_RECORD records[32]{};
        DWORD read = 0;
        const DWORD requested = (std::min)(available, static_cast<DWORD>(std::size(records)));
        if (!ReadConsoleInputW(input_, records, requested, &read)) {
            return std::nullopt;
        }

        for (DWORD i = 0; i < read; ++i) {
            if (records[i].EventType != KEY_EVENT || !records[i].Event.KeyEvent.bKeyDown) {
                continue;
            }
            const KEY_EVENT_RECORD& key = records[i].Event.KeyEvent;
            if (key.wVirtualKeyCode == VK_RETURN) {
                return CalibrationConsoleKey::Enter;
            }
            if (key.uChar.UnicodeChar == L'y' || key.uChar.UnicodeChar == L'Y') {
                return CalibrationConsoleKey::Yes;
            }
            if (key.uChar.UnicodeChar == L'n' || key.uChar.UnicodeChar == L'N') {
                return CalibrationConsoleKey::No;
            }
        }
        return std::nullopt;
    }

private:
    HANDLE input_ = INVALID_HANDLE_VALUE;
    bool interactive_ = false;
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
        info.forceFeedback = overrideResult.profile.forceFeedback;
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
        } else if (resolution.profile) {
            // ProvisionalFallback never carries a DeviceProfile (see
            // ProfileResolution::profile's doc comment), so it can never
            // reach here with forceFeedback set -- an unverified generic
            // axis guess must never arm force feedback.
            info.forceFeedback = resolution.profile->forceFeedback;
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

[[nodiscard]] bool WriteBridgeStateAtomically(const std::filesystem::path& outputPath,
                                              const BridgeStateRecord& record) {
    std::filesystem::path temporaryPath = outputPath;
    temporaryPath += L".tmp";

    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        const std::string encoded = BridgeStateFormatter::Format(record);
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.close();
        if (!output) {
            return false;
        }
    }

    return MoveFileExW(temporaryPath.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
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
        case ProbeMode::Bridge:
            return RunBridge();
        case ProbeMode::FfbSimulate:
            return RunFfbSimulate();
        case ProbeMode::FfbHardwareTestStopOnly:
            return RunFfbHardwareTestStopOnly();
        case ProbeMode::FfbHardwareTestAutoCenter:
            return RunFfbHardwareTestAutoCenter();
        case ProbeMode::FfbHardwareTestWeakEffect:
            return RunFfbHardwareTestWeakEffect();
        case ProbeMode::TelemetryMonitor:
            return RunTelemetryMonitor();
        case ProbeMode::LogitechHidInfo:
            return RunLogitechHidInfo();
        case ProbeMode::FfbHardwareTestLogitechG923AutoCenter:
            return RunFfbHardwareTestLogitechG923AutoCenter();
    }
    return 1;
}

int DeviceProbeApp::RunLogitechHidInfo() {
    return InspectLogitechHidDevices(std::cout, std::cerr);
}

int DeviceProbeApp::RunFfbHardwareTestLogitechG923AutoCenter() {
    return RunLogitechG923AutocenterHardwareTest(stopRequested_, std::cout, std::cerr);
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

    NonBlockingConsoleInput consoleInput;
    if (!consoleInput.IsInteractive()) {
        std::cerr << "Guided calibration requires an interactive Windows console. Run this command directly in "
                     "PowerShell or Command Prompt instead of redirecting stdin.\n";
        return 1;
    }

    std::cout << "Calibrating: " << device->Info().name << "\n";
    std::cout << "Discovered axes (source: rawMin..rawMax, current):\n";
    RawAxisSnapshot currentSnapshot{};
    const Status initialPollStatus = calibratable->PollRawAxes(currentSnapshot);
    for (const auto& axis : axes) {
        std::int32_t current = 0;
        for (std::uint8_t i = 0; i < currentSnapshot.count; ++i) {
            if (currentSnapshot.samples[i].source == axis.source) {
                current = currentSnapshot.samples[i].rawValue;
                break;
            }
        }
        std::cout << "  " << rvwheel::dal::ToString(axis.source) << ": " << axis.rawMin << ".." << axis.rawMax << ", "
                  << (initialPollStatus.IsOk() ? std::to_string(current) : std::string{"unavailable"}) << "\n";
    }
    std::cout << "\n";

    CalibrationWizard wizard(axes);

    constexpr std::chrono::milliseconds kPollInterval{17}; // Approximately 60 Hz, without a busy loop.
    constexpr std::chrono::milliseconds kInitialAcquisition{2500};
    constexpr std::chrono::milliseconds kStableWindow{500};
    constexpr std::chrono::milliseconds kCaptureTimeout{10000};

    const auto pollWithoutCapture = [&]() {
        window.PumpMessages();
        RawAxisSnapshot ignored{};
        calibratable->PollRawAxes(ignored);
    };

    const auto waitForYesNo = [&](std::string_view prompt) -> std::optional<bool> {
        consoleInput.DiscardPendingEvents();
        std::cout << prompt << " [y/N] ";
        std::cout.flush();
        auto nextTick = std::chrono::steady_clock::now();
        while (!stopRequested_.load()) {
            pollWithoutCapture();
            const auto key = consoleInput.TryReadKey();
            if (key == CalibrationConsoleKey::Yes) {
                std::cout << "y\n";
                return true;
            }
            if (key == CalibrationConsoleKey::No || key == CalibrationConsoleKey::Enter) {
                std::cout << "n\n";
                return false;
            }
            nextTick += kPollInterval;
            const auto afterWork = std::chrono::steady_clock::now();
            if (afterWork >= nextTick) {
                nextTick = afterWork;
            } else {
                std::this_thread::sleep_until(nextTick);
            }
        }
        return std::nullopt;
    };

    const auto printRawSnapshot = [&](const RawAxisSnapshot& snapshot) {
        std::cout << "  -> Stable raw values:";
        for (std::uint8_t i = 0; i < snapshot.count; ++i) {
            std::cout << " " << rvwheel::dal::ToString(snapshot.samples[i].source) << "=" << snapshot.samples[i].rawValue;
        }
        std::cout << "\n";
    };

    const auto runStep = [&](const char* prompt, std::chrono::milliseconds minimumAcquisition,
                             bool submitToWizard = true) -> bool {
        for (;;) {
            StableRawAxisSamplerConfig samplerConfig;
            samplerConfig.minimumAcquisition = minimumAcquisition;
            samplerConfig.stableWindow = kStableWindow;
            samplerConfig.minimumSamples = 15;
            samplerConfig.relativeTolerance = 0.005f;
            samplerConfig.maximumSamples = 256;
            StableRawAxisSampler sampler(axes, samplerConfig);

            consoleInput.DiscardPendingEvents();
            std::cout << prompt << " Press Enter when the control is in position...";
            std::cout.flush();

            CalibrationCaptureGate captureGate(kCaptureTimeout);
            auto nextTick = std::chrono::steady_clock::now();
            std::string lastPollError;

            while (!stopRequested_.load()) {
                window.PumpMessages();
                const auto now = std::chrono::steady_clock::now();
                RawAxisSnapshot snapshot{};
                const Status pollStatus = calibratable->PollRawAxes(snapshot);
                if (pollStatus.IsOk()) {
                    (void)sampler.AddSample(now, snapshot);
                } else {
                    sampler.NotifyPollFailure();
                    lastPollError = pollStatus.Message().empty() ? "hardware poll failed" : pollStatus.Message();
                }

                if (captureGate.StateAt(now) == CalibrationCaptureState::WaitingForConfirmation &&
                    consoleInput.TryReadKey() == CalibrationConsoleKey::Enter) {
                    captureGate.Arm(now);
                    std::cout << "\n  -> Capturing a stable 500 ms window";
                    if (minimumAcquisition.count() > 0) {
                        std::cout << " after the initial 2.5 s warm-up";
                    }
                    std::cout << "; hold the control steady...\n";
                }

                if (captureGate.StateAt(now) == CalibrationCaptureState::Capturing) {
                    const StableRawAxisResult stable = sampler.Evaluate();
                    if (stable.IsStable()) {
                        printRawSnapshot(stable.snapshot);
                        if (!submitToWizard) {
                            std::cout << "  -> Device input initialized.\n";
                            return true;
                        }
                        const CalibrationStepOutcome outcome = wizard.SubmitSnapshot(stable.snapshot);
                        switch (outcome) {
                            case CalibrationStepOutcome::Recorded:
                                std::cout << "  -> Recorded.\n";
                                return true;
                            case CalibrationStepOutcome::BaselineChanged:
                                std::cout << "  -> The resting input changed after the previous capture. The baseline was "
                                             "refreshed; keep every control released/centered and confirm again.\n";
                                break;
                            case CalibrationStepOutcome::Ambiguous:
                                std::cout << "  -> More than one axis really moved; move ONLY the requested control and try again.\n";
                                break;
                            case CalibrationStepOutcome::NoMovement:
                                std::cout << "  -> No axis moved enough; move the requested control through its full range and try again.\n";
                                break;
                            case CalibrationStepOutcome::Inconsistent:
                                std::cout << "  -> A different axis moved than in the previous step; use the same control and try again.\n";
                                break;
                        }
                        break; // Retry with a fresh window; never reuse contaminated samples.
                    }

                    if (stable.status == StableRawAxisStatus::DegenerateRange ||
                        stable.status == StableRawAxisStatus::InvalidConfiguration) {
                        std::cerr << "\nCalibration cannot continue: " << stable.diagnostic << "\n";
                        return false;
                    }
                } else if (captureGate.StateAt(now) == CalibrationCaptureState::TimedOut) {
                    const StableRawAxisResult stable = sampler.Evaluate();
                    std::cout << "  -> Timed out waiting for stable input (" << stable.diagnostic;
                    if (!lastPollError.empty()) {
                        std::cout << "; last poll error: " << lastPollError;
                    }
                    std::cout << "). Release unintended controls, hold the requested control steady, and try again.\n";
                    break; // Retry with a fresh window.
                }

                nextTick += kPollInterval;
                const auto afterWork = std::chrono::steady_clock::now();
                if (afterWork >= nextTick) {
                    nextTick = afterWork;
                } else {
                    std::this_thread::sleep_until(nextTick);
                }
            }

            if (stopRequested_.load()) {
                captureGate.Cancel();
                return false;
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

    if (!runStep("Initialize the device: turn the wheel left/right and fully press/release EVERY pedal once. Then "
                 "center the wheel, release all pedals, and hold everything steady.",
                 kInitialAcquisition, false)) {
        std::cout << "\nCancelled.\n";
        return 1;
    }

    for (const auto& stepInfo : kSteps) {
        if (!runStep(stepInfo.prompt, std::chrono::milliseconds{0})) {
            std::cout << "\nCancelled.\n";
            return 1;
        }
    }

    const auto clutchAnswer = waitForYesNo("Does this device have a clutch pedal you want to calibrate?");
    if (!clutchAnswer) {
        std::cout << "\nCancelled.\n";
        return 1;
    }
    const bool wantsClutch = *clutchAnswer;
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
            if (!runStep(stepInfo.prompt, std::chrono::milliseconds{0})) {
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

    const auto saveAnswer = waitForYesNo("\nSave this profile?");
    if (!saveAnswer) {
        std::cout << "\nCancelled.\n";
        return 1;
    }
    if (!*saveAnswer) {
        std::cout << "Not saved.\n";
        return 0;
    }

    DeviceProfile profile;
    profile.schemaVersion = 1;
    std::vector<rvwheel::dal::AxisSource> discoveredSources;
    discoveredSources.reserve(axes.size());
    for (const auto& axis : axes) {
        discoveredSources.push_back(axis.source);
    }
    const ProfileRepository existingRepository = LoadRepository(options_);
    const auto existingResolution = ProfileResolver::Resolve(existingRepository.MergedProfiles(), device->Info(), discoveredSources);
    const bool canOverrideExisting = existingResolution.profile &&
                                     (existingResolution.origin == ProfileOrigin::BuiltInProfile ||
                                      existingResolution.origin == ProfileOrigin::UserProfile);
    if (canOverrideExisting) {
        // Reusing the matched profileId makes the generated user profile
        // an intentional override. A second exact-match id for the same
        // device would make future resolution correctly report ambiguity.
        profile.profileId = existingResolution.profile->profileId;
    } else if (device->Info().vendorId && device->Info().productId) {
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
    // Calibration observes roles/endpoints, but not a fresh process's
    // startup behavior. Preserve verified readiness metadata when this is
    // an override of an existing profile; otherwise use the generic safe
    // default. A built-in activation gate is safety-critical and remains
    // inherited even when an older user override predates that field.
    profile.readiness = canOverrideExisting ? existingResolution.profile->readiness
                                            : DeviceReadinessPolicy::ConservativeDefault();
    if (canOverrideExisting) {
        for (const auto& builtIn : existingRepository.BuiltInProfiles()) {
            if (builtIn.profileId == profile.profileId && builtIn.readiness.requireAxisActivation) {
                profile.readiness.requireAxisActivation = true;
                profile.readiness.activationThreshold = builtIn.readiness.activationThreshold;
                break;
            }
        }
    }
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
        const auto overwriteAnswer = waitForYesNo("\"" + outputPath.string() + "\" already exists. Overwrite?");
        if (!overwriteAnswer) {
            std::cout << "\nCancelled.\n";
            return 1;
        }
        if (!*overwriteAnswer) {
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

int DeviceProbeApp::RunBridge() {
    HiddenWindow window;
    if (!window.IsValid()) {
        std::cerr << "Failed to create the hidden Win32 window required for DirectInput.\n";
        return 1;
    }

    const std::filesystem::path bridgePath = ResolveBridgeStatePath();
    if (bridgePath.empty()) {
        std::cerr << "LOCALAPPDATA is unavailable; cannot resolve the bridge state path.\n";
        return 1;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(bridgePath.parent_path(), directoryError);
    if (directoryError) {
        std::cerr << "Failed to create bridge state directory: " << directoryError.message() << "\n";
        return 1;
    }

    // --enable-force-feedback requests exclusive DirectInput access up
    // front, before the profile (and therefore any forceFeedback config)
    // is even known -- see the ordering note below. This mirrors the
    // gated hardware test's own CreateManager call exactly, including the
    // cooperative level (EXCLUSIVE | BACKGROUND) validated there.
    auto manager = options_.enableForceFeedback
                       ? CreateManager(window, /*requestExclusiveForceFeedbackAccess=*/true,
                                       rvwheel::dal::ForceFeedbackCooperativeLevel::Background)
                       : CreateManager(window);
    manager->RefreshIfDue();
    window.PumpMessages();

    const auto selection = SelectDeviceForMonitoring(manager->DeviceCount());
    if (selection.outcome == DeviceSelectionOutcome::NoDevices) {
        PrintNoDevicesHelp();
        return 1;
    }
    if (selection.outcome == DeviceSelectionOutcome::MultipleDevicesUsingFirst) {
        std::cout << manager->DeviceCount() << " devices were found; bridge uses device #0 (no --device selector yet).\n";
    }

    IWheelDevice* device = FindDeviceByIndex(*manager, selection.selectedIndex);
    if (device == nullptr) {
        std::cerr << "Internal error: selected device index was not found after enumeration.\n";
        return 1;
    }

    const ProfileRepository repo = LoadRepository(options_);
    const AppliedProfileInfo profileInfo = ResolveAndApplyProfile(*device, options_, repo);
    std::cout << "RVWheel bridge host\n"
              << "Device:  " << device->Info().name << "\n"
              << "Profile: " << (profileInfo.profileId.empty() ? "(none)" : profileInfo.profileId) << "  origin="
              << FormatProfileOrigin(profileInfo.origin) << "\n"
              << "Reason:  " << profileInfo.reason << "\n"
              << "State:   " << bridgePath.string() << "\n"
              << "Rate:    " << options_.rateHz << " Hz\n\n"
              << "Move a wheel axis once if readiness is AwaitingActivation. Press Ctrl+C to stop safely.\n";

    // Two independent gates, both required: this flag (runtime) and the
    // resolved profile's own forceFeedback.enabled (data). Neither alone
    // is sufficient, and this integration never substitutes a
    // demonstration config the way --ffb-simulate does -- an absent or
    // disabled block simply means no force feedback this run, input-only,
    // exactly like a plain --bridge invocation. profileInfo.forceFeedback
    // is the actually-applied profile's own block, carried forward by
    // ResolveAndApplyProfile -- see AppliedProfileInfo's doc comment for
    // why this is not re-derived by searching for profileId a second time.
    //
    // Deliberately NOT calling Enable() here: the engine is only armed
    // once the device has actually reported connected+valid+Ready (inside
    // the loop below), never merely because a profile happened to resolve.
    std::unique_ptr<BridgeForceFeedbackSession> ffbSession;
    if (options_.enableForceFeedback) {
        if (profileInfo.forceFeedback && profileInfo.forceFeedback->enabled) {
            ffbSession = std::make_unique<BridgeForceFeedbackSession>(*device, *profileInfo.forceFeedback);
            std::cout << "Force feedback: armed (profile \"" << profileInfo.profileId
                      << "\", masterGain=" << profileInfo.forceFeedback->masterGain
                      << ", springStrength=" << profileInfo.forceFeedback->springStrength
                      << ", damperStrength=" << profileInfo.forceFeedback->damperStrength
                      << ", slewRatePerSecond=" << profileInfo.forceFeedback->slewRatePerSecond
                      << "). Exclusive DirectInput access is held for the rest of this run; periodic device "
                      << "re-enumeration is disabled while it is (see docs/FORCE_FEEDBACK_HARDWARE_TEST.md). "
                      << "Reconnecting the wheel requires restarting the bridge in this first version.\n"
                      << "Force feedback: waiting for the device to report connected+valid+Ready before enabling "
                      << "the engine. Input keeps publishing normally in the meantime; no force is applied until "
                      << "readiness is actually observed.\n";
        } else {
            std::cout << "Force feedback: --enable-force-feedback was passed, but the resolved profile \""
                      << (profileInfo.profileId.empty() ? "(none)" : profileInfo.profileId)
                      << "\" has no forceFeedback.enabled=true block. Running input-only this session -- no "
                         "demonstration/fallback configuration is ever substituted. Exclusive DirectInput access is "
                         "still held (requested up front), so periodic re-enumeration remains disabled.\n";
        }
    }
    bool ffbEnabledOnceReady = false;

    // Adaptive (speed-sensitive) spring is a construction-time choice
    // baked into ffbSession's own engine (see BuildEngine() in
    // BridgeForceFeedbackSession.cpp) -- this flag only decides whether
    // *this loop* also reads the RVT1 transport and feeds it in via
    // TickWithTelemetry(). When false (the default, and the only
    // possibility whenever ffbSession itself is null), the loop below
    // never touches VehicleTelemetryTransport at all, so the static path
    // never depends on RVT1 existing.
    const bool adaptiveSpring = ffbSession && profileInfo.forceFeedback->speedSensitiveSpring.enabled;
    const std::filesystem::path telemetryPath = adaptiveSpring ? ResolveVehicleTelemetryPath() : std::filesystem::path{};
    // Same generous-but-bounded 500ms staleness window --telemetry-monitor
    // uses (see RunTelemetryMonitor) -- intentionally independent of the
    // FFB profile's own (typically much stricter) watchdogTimeoutMilliseconds,
    // which ForceFeedbackSafetyController enforces separately once a
    // sample's own receivedAt is fed into it below.
    VehicleTelemetryFreshnessTracker telemetryTracker(std::chrono::milliseconds{500});
    if (adaptiveSpring) {
        std::cout << "Force feedback: speed-sensitive spring ENABLED (minimumScale="
                  << profileInfo.forceFeedback->speedSensitiveSpring.minimumScale
                  << ", fullStrengthSpeedMetersPerSecond="
                  << profileInfo.forceFeedback->speedSensitiveSpring.fullStrengthSpeedMetersPerSecond
                  << "). Reading: " << telemetryPath.string()
                  << "\nA baseline/leftover frame or invalid/non-local telemetry never activates force; stale "
                     "telemetry is handled by the safety controller's own watchdog.\n";
    }
    constexpr std::chrono::milliseconds kAdaptiveDiagnosticInterval{250}; // <= 4 times per second, never at poll rate.
    std::optional<std::chrono::steady_clock::time_point> lastAdaptiveDiagnosticAt;

    // --duration is optional and OFF by default for --bridge: std::nullopt
    // preserves the exact prior infinite-until-Ctrl+C behavior. Only when
    // explicitly given does the loop below stop itself once the deadline
    // passes, through the same graceful shutdown path Ctrl+C already
    // takes -- see CliOptions::bridgeDuration.
    const std::optional<std::chrono::steady_clock::time_point> bridgeDeadline =
        options_.bridgeDuration ? std::optional<std::chrono::steady_clock::time_point>(
                                       std::chrono::steady_clock::now() + *options_.bridgeDuration)
                                 : std::nullopt;
    const auto durationElapsed = [&]() noexcept {
        return bridgeDeadline.has_value() && std::chrono::steady_clock::now() >= *bridgeDeadline;
    };
    if (bridgeDeadline) {
        std::cout << "Duration: stopping automatically after " << options_.bridgeDuration->count()
                  << " s, through the same graceful shutdown path Ctrl+C takes.\n";
    }

    const auto frameInterval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / options_.rateHz));
    auto nextTick = std::chrono::steady_clock::now();
    std::uint64_t bridgeSequence = 0;
    std::uint64_t writeFailures = 0;
    std::uint64_t polls = 0;
    HANDLE parentProcess = nullptr;
    if (options_.parentProcessId != 0) {
        parentProcess = OpenProcess(SYNCHRONIZE, FALSE, options_.parentProcessId);
        if (parentProcess == nullptr) {
            std::cerr << "Could not supervise launcher process " << options_.parentProcessId
                      << "; refusing to leave an orphan bridge running.\n";
            return 1;
        }
    }

    const auto parentIsAlive = [&]() noexcept {
        return parentProcess == nullptr || WaitForSingleObject(parentProcess, 0) == WAIT_TIMEOUT;
    };

    while (!stopRequested_.load() && parentIsAlive() && !durationElapsed()) {
        window.PumpMessages();
        // Never re-enumerate while exclusive force-feedback access is held
        // (requested whenever --enable-force-feedback was passed, whether
        // or not a session actually armed) -- a second exclusive Acquire of
        // the same physical device is the confirmed root cause fixed in
        // docs/FORCE_FEEDBACK_HARDWARE_TEST.md's incident log, and this
        // bridge integration must not reintroduce it. ShouldRefreshDuringBridge
        // is a pure function of this run's flag alone, never of the
        // session's live state, so a session ending/faulting can never
        // flip this back on mid-run -- see BridgePolicies.hpp.
        if (rvwheel::tools::probe::ShouldRefreshDuringBridge(options_.enableForceFeedback)) {
            manager->RefreshIfDue();
        }
        const Status pollStatus = device->Poll();
        ++polls;

        const auto& state = device->State();

        if (ffbSession && !ffbEnabledOnceReady &&
            rvwheel::tools::probe::IsReadyToEnableForceFeedback(pollStatus.IsOk(), state)) {
            const Status enableStatus = ffbSession->Enable();
            if (enableStatus.IsOk()) {
                ffbEnabledOnceReady = true;
                std::cout << "Force feedback: device readiness reached; ownership session prepared; engine ENABLED.\n";
            } else {
                std::cerr << "Force feedback: ownership-session preparation failed; engine remains disabled: "
                          << FormatStatusCode(enableStatus.Code());
                if (!enableStatus.Message().empty()) {
                    std::cerr << " (" << enableStatus.Message() << ")";
                }
                std::cerr << "\n";
                static_cast<void>(ffbSession->Stop());
                ffbSession.reset();
            }
        }

        if (ffbSession) {
            const bool wasFaulted = ffbSession->IsFaulted();
            const auto tickNow = std::chrono::steady_clock::now();
            if (adaptiveSpring) {
                const auto parsedFrame = ReadVehicleTelemetryFile(telemetryPath);
                const auto freshSample = telemetryTracker.Observe(parsedFrame, tickNow);
                std::optional<rvwheel::ffb::VehicleTelemetry> telemetry;
                std::chrono::steady_clock::time_point telemetryTimestamp = tickNow;
                if (freshSample.has_value()) {
                    telemetry = ToVehicleTelemetry(*freshSample);
                    telemetryTimestamp = freshSample->receivedAt;
                }
                ffbSession->TickWithTelemetry(tickNow, telemetry, telemetryTimestamp);

                if (!lastAdaptiveDiagnosticAt.has_value() || tickNow - *lastAdaptiveDiagnosticAt >= kAdaptiveDiagnosticInterval) {
                    lastAdaptiveDiagnosticAt = tickNow;
                    const auto diagnostics = ffbSession->Diagnostics(tickNow);
                    std::cout << "[adaptive] state=" << rvwheel::ffb::ToString(diagnostics.state);
                    if (freshSample.has_value()) {
                        const float speed = freshSample->frame.speedMetersPerSecond;
                        const float scale = rvwheel::ffb::ComputeSpeedSensitiveSpringScale(
                            speed, profileInfo.forceFeedback->speedSensitiveSpring.minimumScale,
                            profileInfo.forceFeedback->speedSensitiveSpring.fullStrengthSpeedMetersPerSecond);
                        const float requestedSpring =
                            std::clamp(scale * std::clamp(profileInfo.forceFeedback->springStrength, 0.0f, 1.0f), 0.0f,
                                       std::clamp(profileInfo.forceFeedback->springStrength, 0.0f, 1.0f));
                        std::cout << " speed=" << speed << "m/s age="
                                  << std::chrono::duration_cast<std::chrono::milliseconds>(tickNow - telemetryTimestamp)
                                         .count()
                                  << "ms scale=" << scale << " springRequested=" << requestedSpring
                                  << " springApplied=" << diagnostics.lastAppliedCommand.spring;
                    } else {
                        std::cout << " speed=n/a age=n/a scale=n/a springRequested=n/a springApplied="
                                  << diagnostics.lastAppliedCommand.spring;
                    }
                    std::cout << "\n";
                }
            } else {
                ffbSession->Tick(tickNow);
            }
            if (!wasFaulted && ffbSession->IsFaulted()) {
                std::cerr << "Force feedback backend faulted; continuing input-only for the rest of this run "
                             "(no automatic re-arm).\n";
            }
        }

        const auto& capabilities = device->Info().capabilities;
        BridgeStateRecord record{};
        record.sequence = ++bridgeSequence;
        record.connected = state.connected;
        record.valid = pollStatus.IsOk() && state.valid;
        record.steering = state.steering;
        record.throttle = state.throttle;
        record.brake = state.brake;
        record.clutch = capabilities.hasClutch ? state.clutch : 0.0f;
        record.vendorId = device->Info().vendorId.value_or(0);
        record.productId = device->Info().productId.value_or(0);
        const std::size_t buttonLimit = std::min<std::size_t>(state.buttons.size(), capabilities.buttonCount);
        for (std::size_t buttonIndex = 0; buttonIndex < buttonLimit; ++buttonIndex) {
            if (state.buttons[buttonIndex]) {
                const std::size_t wordIndex = buttonIndex / 32;
                const std::size_t bitIndex = buttonIndex % 32;
                record.buttonWords[wordIndex] |= (std::uint32_t{1} << bitIndex);
            }
        }

        if (!WriteBridgeStateAtomically(bridgePath, record)) {
            ++writeFailures;
            if (writeFailures == 1 || writeFailures % 120 == 0) {
                std::cerr << "Warning: failed to publish bridge frame " << record.sequence
                          << " (will retry next poll).\n";
            }
        }

        nextTick += frameInterval;
        const auto afterWork = std::chrono::steady_clock::now();
        if (afterWork > nextTick) {
            nextTick = afterWork;
        } else {
            std::this_thread::sleep_until(nextTick);
        }
    }

    const bool stoppedByDuration = durationElapsed();
    std::cout << "\nStopping bridge (" << (stoppedByDuration ? "requested --duration elapsed"
                                            : stopRequested_.load() ? "Ctrl+C"
                                                                     : "supervised parent process exited")
              << ")...\n";

    // Explicit stop on every exit path this loop can take (Ctrl+C, parent
    // process gone, duration elapsed, or -- since ffbSession is destroyed
    // further down regardless -- this call is defense in depth, not the
    // only stop). BridgeForceFeedbackSession::Stop() is idempotent, so
    // calling it here and again implicitly via the destructor below is
    // safe; this explicit call is the one whose result is actually
    // reported and checked below.
    bool ffbStopConfirmed = true; // No session armed => nothing to confirm; exit code unaffected by FFB.
    if (ffbSession) {
        const BridgeForceFeedbackStopResult stopResult = ffbSession->Stop();
        std::cout << "Force feedback engine stop signal: " << (stopResult.engineStopRequested ? "yes" : "no")
                  << "\n";
        std::cout << "Final StopForceFeedback(): " << FormatStatusCode(stopResult.explicitStopStatus.Code());
        if (!stopResult.explicitStopStatus.Message().empty()) {
            std::cout << " (" << stopResult.explicitStopStatus.Message() << ")";
        }
        std::cout << "\n";
        std::cout << "EndForceFeedbackSession(): " << FormatStatusCode(stopResult.sessionEndStatus.Code());
        if (!stopResult.sessionEndStatus.Message().empty()) {
            std::cout << " (" << stopResult.sessionEndStatus.Message() << ")";
        }
        std::cout << "\n";
        ffbStopConfirmed = stopResult.Confirmed();
        if (!ffbStopConfirmed) {
            std::cerr << "Force feedback stop was NOT confirmed -- treat the wheel as potentially still under "
                         "force until physically verified, and investigate before the next run.\n";
        }
    }

    BridgeStateRecord stopped{};
    stopped.sequence = ++bridgeSequence;
    static_cast<void>(WriteBridgeStateAtomically(bridgePath, stopped));
    if (parentProcess != nullptr) {
        CloseHandle(parentProcess);
    }
    std::cout << "\nBridge stopped after " << polls << " polls (" << writeFailures << " publish failures); "
              << (ffbStopConfirmed ? "shutdown confirmed safe.\n" : "shutdown NOT confirmed safe -- see above.\n");
    return ffbStopConfirmed ? 0 : 1;
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

int DeviceProbeApp::RunFfbSimulate() {
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
                  << " devices were found; simulating device #0 (no --device selector yet).\n";
    }

    IWheelDevice* device = FindDeviceByIndex(*manager, selection.selectedIndex);
    if (device == nullptr) {
        std::cerr << "Internal error: selected device index was not found after enumeration.\n";
        return 1;
    }

    const ProfileRepository repo = LoadRepository(options_);
    const AppliedProfileInfo profileInfo = ResolveAndApplyProfile(*device, options_, repo);

    rvwheel::ffb::ForceFeedbackConfig config;
    std::string configSource;
    const bool foundConfig = profileInfo.forceFeedback.has_value();
    if (foundConfig) {
        config = *profileInfo.forceFeedback;
        configSource = "resolved from profile \"" + profileInfo.profileId + "\"";
    } else {
        // A demonstration-only config so the tool visibly produces
        // something even with no forceFeedback block resolved. This is
        // safe precisely because SimulatedForceFeedbackSink below makes it
        // structurally impossible for this to reach the real device,
        // regardless of `enabled`/gain values.
        config.enabled = true;
        config.masterGain = 0.5f;
        config.springStrength = 0.3f;
        config.damperStrength = 0.2f;
        configSource = "no forceFeedback block resolved on this profile; using a built-in demonstration config";
    }

    std::cout << "=== RVWheel Force Feedback SIMULATION MODE ===\n"
                 "No force is ever sent to the physical device in this mode: ApplyForceFeedback/StopForceFeedback\n"
                 "are always routed to an in-process recording sink, never to the real device.\n\n";
    std::cout << "Device:  " << device->Info().name << "\n";
    std::cout << "Profile: " << (profileInfo.profileId.empty() ? "(none)" : profileInfo.profileId) << "  origin="
              << FormatProfileOrigin(profileInfo.origin) << "\n";
    std::cout << "FFB cfg: " << configSource << "\n";
    std::cout << "         enabled=" << config.enabled << " masterGain=" << config.masterGain
              << " spring=" << config.springStrength << " damper=" << config.damperStrength
              << " maxTorqueNormalized=" << config.maxTorqueNormalized << "\n\n";

    SimulatedForceFeedbackSink sink(*device);

    std::vector<std::unique_ptr<rvwheel::ffb::IForceFeedbackSource>> sources;
    sources.push_back(std::make_unique<rvwheel::ffb::SpringDamperSource>(config));
    rvwheel::ffb::ForceFeedbackEngine engine(rvwheel::ffb::ForceFeedbackSafetyController(config),
                                              rvwheel::ffb::ForceFeedbackMixer{}, std::move(sources));
    engine.Enable();

    const auto start = std::chrono::steady_clock::now();
    const auto tickInterval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / options_.rateHz));
    constexpr std::chrono::milliseconds kPrintInterval{200};

    std::uint64_t tickCount = 0;
    auto nextTick = start;
    auto lastPrint = start - kPrintInterval;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - start;
        if (elapsed >= options_.duration || stopRequested_.load()) {
            break;
        }

        window.PumpMessages();
        manager->RefreshIfDue();
        device->Poll();
        ++tickCount;

        // No real vehicle telemetry source is wired yet (see
        // docs/research/FORCE_FEEDBACK_FEASIBILITY.md, open question 1);
        // SpringDamperSource does not need it, so an empty sample is
        // correct here, not a placeholder standing in for missing work.
        const rvwheel::ffb::VehicleTelemetry telemetry{};
        const auto decision = engine.Tick(sink, telemetry, device->State().steering, now, now);

        if (now - lastPrint >= kPrintInterval) {
            lastPrint = now;
            std::cout << "[t=" << std::fixed << std::setprecision(1) << std::chrono::duration<double>(elapsed).count()
                      << "s] state=" << rvwheel::ffb::ToString(engine.State())
                      << " command(constantForce=" << std::setprecision(3) << decision.command.constantForce
                      << ", spring=" << decision.command.spring << ", damper=" << decision.command.damper
                      << ", gain=" << decision.command.gain << ")"
                      << " sink.applyCount=" << sink.applyCount << " sink.stopCount=" << sink.stopCount << "\n";
        }

        nextTick += tickInterval;
        const auto afterWork = std::chrono::steady_clock::now();
        if (afterWork > nextTick) {
            nextTick = afterWork;
        } else {
            std::this_thread::sleep_until(nextTick);
        }
    }

    engine.Disable();
    // Drain the ramp-down so the final StopForceFeedback (observed only on
    // the sink -- see above) actually happens before this function returns.
    for (int i = 0; i < 200 && engine.State() != rvwheel::ffb::ForceFeedbackState::Disabled; ++i) {
        engine.TickWithoutTelemetry(sink, std::chrono::steady_clock::now());
    }

    std::cout << "\nSimulation finished: " << tickCount << " ticks. sink.applyCount=" << sink.applyCount
              << ", sink.stopCount=" << sink.stopCount
              << ". No ApplyForceFeedback/StopForceFeedback call ever reached the real device.\n";
    return 0;
}

int DeviceProbeApp::RunFfbHardwareTestStopOnly() {
    std::cout << "=== RVWheel Force Feedback REAL HARDWARE TEST -- Step 4: Stop only ===\n"
                 "This requests EXCLUSIVE force-feedback access on the first FFB-capable device and calls\n"
                 "the real StopForceFeedback() exactly once. No effect is ever created or started here.\n"
                 "If you feel ANY motion, resistance, or vibration at any point, disconnect the wheel now.\n\n";

    const bool foregroundExperiment = RequestsForegroundFfb(options_);
    const bool focusedForegroundExperiment = RequestsFocusedForegroundFfb(options_);
    HiddenWindow window(RequestedFfbWindowMode(options_));
    if (!window.IsValid()) {
        std::cerr << "Failed to create the hidden Win32 window required for DirectInput.\n";
        return 1;
    }

    std::cout << "Cooperative level: EXCLUSIVE | " << (foregroundExperiment ? "FOREGROUND" : "BACKGROUND") << "\n";
    if (foregroundExperiment) {
        if (focusedForegroundExperiment) {
            std::cout << "Showing and activating the dedicated foreground diagnostic window now. Do not switch "
                         "to another window until this short stop-only test finishes.\n";
            if (!window.ActivateForForegroundDiagnostic()) {
                std::cerr << "Windows did not grant foreground ownership to the diagnostic window. No DirectInput "
                             "device access was attempted; exiting safely.\n";
                return 1;
            }
            std::cout << "GetForegroundWindow() matches the diagnostic HWND: yes\n\n";
        } else {
            std::cout << "Foreground experiment window: valid top-level HWND, deliberately invisible/unfocused.\n"
                         "GetForegroundWindow() matches it: "
                      << (window.IsForeground() ? "yes (unexpected)" : "no (expected for this first experiment)")
                      << "\n\n";
        }
    }

    auto manager = CreateManager(window, /*requestExclusiveForceFeedbackAccess=*/true,
                                 RequestedFfbCooperativeLevel(options_));
    manager->RefreshIfDue();
    window.PumpMessages();

    IWheelDevice* device = nullptr;
    for (IWheelDevice* candidate : manager->Devices()) {
        if (candidate->Info().capabilities.hasForceFeedback) {
            device = candidate;
            break;
        }
    }
    if (device == nullptr) {
        std::cerr << "No force-feedback-capable device was found. Nothing to test; exiting safely.\n";
        return 1;
    }

    std::cout << "Target device: " << device->Info().name << " ("
              << FormatVendorProductId(device->Info().vendorId, device->Info().productId) << ")\n";
    std::cout << "Attempting exclusive acquisition without creating an effect. Exact SetCooperativeLevel/Acquire "
                 "HRESULT diagnostics follow on stderr.\n";

    // No FFB call yet: confirm input and GetForceFeedbackState stay usable
    // under exclusive acquisition. The focused experiment deliberately
    // runs beyond the prior ~2-second failure point while creating no
    // effect; other modes retain the original short acquisition check.
    const auto acquisitionProbeDuration = focusedForegroundExperiment ? std::chrono::seconds{5}
                                                                       : std::chrono::milliseconds{200};
    const auto acquisitionProbeStart = std::chrono::steady_clock::now();
    bool anyPollOk = false;
    bool allPollsOk = true;
    bool retainedForeground = true;
    std::uint64_t acquisitionPollCount = 0;
    while (std::chrono::steady_clock::now() - acquisitionProbeStart < acquisitionProbeDuration) {
        window.PumpMessages();
        if (focusedForegroundExperiment && !window.IsForeground()) {
            retainedForeground = false;
            std::cerr << "The diagnostic window lost foreground ownership; ending the no-effect probe.\n";
            break;
        }
        const Status pollStatus = device->Poll();
        if (pollStatus.IsOk()) {
            anyPollOk = true;
        } else {
            allPollsOk = false;
        }
        ++acquisitionPollCount;
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    const auto acquisitionProbeElapsed = std::chrono::steady_clock::now() - acquisitionProbeStart;
    std::cout << "No-effect acquisition probe: " << std::fixed << std::setprecision(2)
              << std::chrono::duration<double>(acquisitionProbeElapsed).count() << "s, " << acquisitionPollCount
              << " polls, all polls readable=" << (allPollsOk && anyPollOk ? "yes" : "no");
    if (focusedForegroundExperiment) {
        std::cout << ", foreground retained=" << (retainedForeground ? "yes" : "no");
    }
    std::cout << "\n";
    std::cout << "Input still readable under exclusive acquisition: " << (anyPollOk ? "yes" : "no") << "\n";
    std::cout << "  steering=" << device->State().steering << " throttle=" << device->State().throttle
              << " connected=" << (device->IsConnected() ? "true" : "false") << "\n\n";

    std::cout << "Calling the REAL StopForceFeedback() once now. No effect has ever been created on this "
                 "device by this process; expect ZERO motion.\n";
    const Status stopStatus = device->StopForceFeedback();
    std::cout << "StopForceFeedback() returned: " << FormatStatusCode(stopStatus.Code());
    if (!stopStatus.Message().empty()) {
        std::cout << " (" << stopStatus.Message() << ")";
    }
    std::cout << "\n\n";

    std::cout << "Test finished. Before doing anything else with force feedback, confirm with whoever was "
                 "physically observing the wheel that NO motion, resistance, or vibration occurred at any "
                 "point above.\n";
    return anyPollOk && allPollsOk && retainedForeground && stopStatus.IsOk() ? 0 : 1;
}

int DeviceProbeApp::RunFfbHardwareTestAutoCenter() {
    constexpr auto kObservationDuration = std::chrono::seconds{5};
    constexpr auto kPollInterval = std::chrono::milliseconds{20};

    std::cout << "=== RVWheel REAL HARDWARE TEST -- DirectInput native autocenter only ===\n"
                 "This creates and starts NO force-feedback effect. After a 3-second comparison window it\n"
                 "requests DIPROP_AUTOCENTER=OFF for 5 seconds, then restores the exact prior value.\n"
                 "Expected: resistance/return-to-center may become weaker during those 5 seconds and return\n"
                 "afterward. The wheel must NEVER move by itself. Press Ctrl+C immediately if it does.\n\n";

    HiddenWindow window(RequestedFfbWindowMode(options_));
    if (!window.IsValid()) {
        std::cerr << "Failed to create the Win32 window required for DirectInput.\n";
        return 1;
    }
    if (RequestsFocusedForegroundFfb(options_) && !window.ActivateForForegroundDiagnostic()) {
        std::cerr << "Windows did not grant foreground ownership to the diagnostic window. No DirectInput "
                     "device access was attempted; exiting safely.\n";
        return 1;
    }

    auto manager = CreateManager(window, /*requestExclusiveForceFeedbackAccess=*/true,
                                 RequestedFfbCooperativeLevel(options_));
    manager->RefreshIfDue();
    window.PumpMessages();

    IWheelDevice* device = nullptr;
    for (IWheelDevice* candidate : manager->Devices()) {
        if (candidate->Info().capabilities.hasForceFeedback) {
            device = candidate;
            break;
        }
    }
    if (device == nullptr) {
        std::cerr << "No force-feedback-capable device was found. Nothing changed.\n";
        return 1;
    }

    std::cout << "Target device: " << device->Info().name << " ("
              << FormatVendorProductId(device->Info().vendorId, device->Info().productId) << ")\n"
              << "For the next 3 seconds, gently note the normal centering resistance. No property has changed yet.\n";
    for (int second = 3; second > 0 && !stopRequested_.load(); --second) {
        std::cout << second << "...\n";
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    if (stopRequested_.load()) {
        std::cout << "Test cancelled before DIPROP_AUTOCENTER was changed.\n";
        return 1;
    }

    const Status beginStatus = device->BeginForceFeedbackSession();
    std::cout << "BeginForceFeedbackSession(): " << FormatStatusCode(beginStatus.Code());
    if (!beginStatus.Message().empty()) {
        std::cout << " (" << beginStatus.Message() << ")";
    }
    std::cout << "\nObservation window ACTIVE for 5 seconds -- gently compare resistance now.\n";
    if (!beginStatus.IsOk()) {
        std::cerr << "The ownership session could not begin; no observation is valid.\n";
        return 1;
    }

    bool allPollsReadable = true;
    bool foregroundRetained = true;
    std::uint64_t pollCount = 0;
    const auto startedAt = std::chrono::steady_clock::now();
    while (!stopRequested_.load() && std::chrono::steady_clock::now() - startedAt < kObservationDuration) {
        window.PumpMessages();
        if (RequestsFocusedForegroundFfb(options_) && !window.IsForeground()) {
            foregroundRetained = false;
            std::cerr << "The diagnostic window lost foreground ownership; ending the observation immediately.\n";
            break;
        }
        if (!device->Poll().IsOk()) {
            allPollsReadable = false;
            std::cerr << "Input polling failed; ending the observation immediately.\n";
            break;
        }
        ++pollCount;
        std::this_thread::sleep_for(kPollInterval);
    }

    const Status endStatus = device->EndForceFeedbackSession();
    std::cout << "Observation window CLOSED after " << std::fixed << std::setprecision(2)
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count() << "s and "
              << pollCount << " readable polls.\n"
              << "EndForceFeedbackSession(): " << FormatStatusCode(endStatus.Code());
    if (!endStatus.Message().empty()) {
        std::cout << " (" << endStatus.Message() << ")";
    }
    std::cout << "\nNo ApplyForceFeedback call was made and no effect was created by this test.\n"
                 "Confirm whether resistance was lower ONLY during the 5-second window and returned afterward.\n";

    return allPollsReadable && foregroundRetained && endStatus.IsOk() && !stopRequested_.load() ? 0 : 1;
}

int DeviceProbeApp::RunFfbHardwareTestWeakEffect() {
    // Fixed, deliberately conservative constants for this one gated test --
    // not CLI-configurable on purpose, so this mode can never be pointed at
    // a stronger effect than the hardware test procedure's step 6/7 call
    // for. Raised from 0.1 to 0.2 (still far below the safety controller's
    // own 0.6 absolute ceiling) only after three prior real-hardware runs
    // at 0.1 reported no unsafe motion; see
    // docs/FORCE_FEEDBACK_HARDWARE_TEST.md's incident log before raising
    // this further.
    constexpr float kTestGain = 0.2f;
    constexpr float kTestStrength = 0.2f;
    constexpr auto kTestDuration = std::chrono::seconds{5};
    constexpr auto kTickInterval = std::chrono::milliseconds{16};

    const bool isSpring = (options_.ffbTestEffect == FfbTestEffect::Spring);
    std::cout << "=== RVWheel Force Feedback REAL HARDWARE TEST -- weak " << (isSpring ? "spring" : "damper")
              << " ===\n"
              << "This applies ONE real, weak effect (gain=" << kTestGain << ", strength=" << kTestStrength
              << ") for " << kTestDuration.count() << " seconds through the real safety controller, then stops.\n"
              << "If the force feels stronger than expected, oscillates, or anything else feels wrong, press "
                 "Ctrl+C now -- the wheel will stop.\n\n";

    HiddenWindow window(RequestedFfbWindowMode(options_));
    if (!window.IsValid()) {
        std::cerr << "Failed to create the hidden Win32 window required for DirectInput.\n";
        return 1;
    }
    if (RequestsFocusedForegroundFfb(options_) && !window.ActivateForForegroundDiagnostic()) {
        std::cerr << "Windows did not grant foreground ownership to the diagnostic window. No DirectInput device "
                     "access was attempted; exiting safely.\n";
        return 1;
    }

    auto manager = CreateManager(window, /*requestExclusiveForceFeedbackAccess=*/true,
                                 RequestedFfbCooperativeLevel(options_));
    manager->RefreshIfDue();
    window.PumpMessages();

    IWheelDevice* device = nullptr;
    for (IWheelDevice* candidate : manager->Devices()) {
        if (candidate->Info().capabilities.hasForceFeedback) {
            device = candidate;
            break;
        }
    }
    if (device == nullptr) {
        std::cerr << "No force-feedback-capable device was found. Nothing to test; exiting safely.\n";
        return 1;
    }
    std::cout << "Target device: " << device->Info().name << "\n\n";

    rvwheel::ffb::ForceFeedbackConfig config;
    config.enabled = true;
    config.masterGain = kTestGain;
    config.springStrength = isSpring ? kTestStrength : 0.0f;
    config.damperStrength = isSpring ? 0.0f : kTestStrength;
    config.maxTorqueNormalized = kTestStrength;
    config.slewRatePerSecond = 0.5f; // Slow, deliberate ramp for the first-ever real activation.
    config.watchdogTimeout = std::chrono::milliseconds{200};

    std::vector<std::unique_ptr<rvwheel::ffb::IForceFeedbackSource>> sources;
    sources.push_back(std::make_unique<rvwheel::ffb::SpringDamperSource>(config));
    rvwheel::ffb::ForceFeedbackEngine engine(rvwheel::ffb::ForceFeedbackSafetyController(config),
                                              rvwheel::ffb::ForceFeedbackMixer{}, std::move(sources));

    std::cout << "Arming in 3 seconds -- keep hands clear of the wheel until the effect feels correct.\n";
    for (int s = 3; s > 0 && !stopRequested_.load(); --s) {
        std::cout << s << "...\n";
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }

    if (!stopRequested_.load()) {
        const Status beginStatus = device->BeginForceFeedbackSession();
        std::cout << "BeginForceFeedbackSession(): " << FormatStatusCode(beginStatus.Code());
        if (!beginStatus.Message().empty()) {
            std::cout << " (" << beginStatus.Message() << ")";
        }
        std::cout << "\n";
        if (!beginStatus.IsOk()) {
            std::cerr << "Ownership-session preparation failed; no effect was enabled.\n";
            return 1;
        }
        engine.Enable();

        const auto start = std::chrono::steady_clock::now();
        auto lastPrint = start - std::chrono::milliseconds{200};
        bool backendFaulted = false;
        bool inputPollFailed = false;
        bool foregroundLost = false;
        std::string faultReason;
        while (!stopRequested_.load()) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = now - start;
            if (elapsed >= kTestDuration) {
                break;
            }

            window.PumpMessages();
            if (RequestsFocusedForegroundFfb(options_) && !window.IsForeground()) {
                foregroundLost = true;
                std::cerr << "The diagnostic window lost foreground ownership; stopping the effect immediately.\n";
                break;
            }
            // Do not refresh/re-enumerate while this instance owns an
            // active exclusive effect. The DirectInput enumerator acquires
            // each newly constructed candidate; creating a duplicate for
            // this same wheel would steal exclusivity from `device` before
            // DeviceManager recognizes the duplicate and discards it.
            // Poll() below is sufficient to detect a physical disconnect
            // during this short, single-device diagnostic.
            const Status pollStatus = device->Poll();
            if (!pollStatus.IsOk()) {
                inputPollFailed = true;
                std::cerr << "Input/FFB state poll failed: " << FormatStatusCode(pollStatus.Code());
                if (!pollStatus.Message().empty()) {
                    std::cerr << " (" << pollStatus.Message() << ")";
                }
                std::cerr << "\n";
                break;
            }

            const rvwheel::ffb::VehicleTelemetry telemetry{};
            const auto decision = engine.Tick(*device, telemetry, device->State().steering, now, now);

            if (now - lastPrint >= std::chrono::milliseconds{200}) {
                lastPrint = now;
                std::cout << "[t=" << std::fixed << std::setprecision(1)
                          << std::chrono::duration<double>(elapsed).count()
                          << "s] state=" << rvwheel::ffb::ToString(engine.State())
                          << " spring=" << decision.command.spring << " damper=" << decision.command.damper
                          << " gain=" << decision.command.gain << "\n";
            }

            if (engine.State() == rvwheel::ffb::ForceFeedbackState::Faulted) {
                backendFaulted = true;
                faultReason = engine.Diagnostics(now).lastFaultReason;
                std::cerr << "Force-feedback backend faulted; stopping the test immediately: " << faultReason << "\n";
                break;
            }

            std::this_thread::sleep_for(kTickInterval);
        }

        std::cout << "\nStopping...\n";
        // EmergencyStop is intentionally used even from Faulted: it emits
        // one immediate zero/stop edge instead of attempting a long ramp
        // through an exclusive-access failure.
        engine.EmergencyStop();
        (void)engine.TickWithoutTelemetry(*device, std::chrono::steady_clock::now());

        // Belt-and-suspenders explicit stop, regardless of the engine's final state.
        const Status finalStop = device->StopForceFeedback();
        std::cout << "Final StopForceFeedback(): " << FormatStatusCode(finalStop.Code());
        if (!finalStop.Message().empty()) {
            std::cout << " (" << finalStop.Message() << ")";
        }
        std::cout << "\n";
        const Status sessionEnd = device->EndForceFeedbackSession();
        std::cout << "EndForceFeedbackSession(): " << FormatStatusCode(sessionEnd.Code());
        if (!sessionEnd.Message().empty()) {
            std::cout << " (" << sessionEnd.Message() << ")";
        }
        std::cout << "\n";

        if (backendFaulted) {
            std::cout << "Backend fault: " << faultReason << "\n";
        }
        std::cout << "Input polls remained readable: " << (inputPollFailed ? "no" : "yes") << "\n";
        if (RequestsFocusedForegroundFfb(options_)) {
            std::cout << "Foreground retained: " << (foregroundLost ? "no" : "yes") << "\n";
        }

        std::cout << "\nTest finished. Confirm the effect felt correct and stopped completely -- and that the wheel "
                     "is not still resisting or centering -- before increasing gain or trying the other effect, per "
                     "docs/FORCE_FEEDBACK_HARDWARE_TEST.md.\n";
        return !backendFaulted && !inputPollFailed && !foregroundLost && finalStop.IsOk() && sessionEnd.IsOk() ? 0 : 1;
    }

    std::cout << "\nTest cancelled before force feedback was enabled.\n";
    return 1;
}

int DeviceProbeApp::RunTelemetryMonitor() {
    std::cout << "RVWheel vehicle telemetry monitor\n"
                 "Never enumerates or acquires a wheel device, and never calls "
                 "ApplyForceFeedback/StopForceFeedback -- this mode only reads the transport file below.\n\n";

    const std::filesystem::path telemetryPath = ResolveVehicleTelemetryPath();
    if (telemetryPath.empty()) {
        std::cerr << "LOCALAPPDATA is unavailable; cannot resolve the vehicle telemetry path.\n";
        return 1;
    }
    std::cout << "Reading:  " << telemetryPath.string() << "\n"
              << "Rate:     " << options_.rateHz << " Hz\n"
              << "Duration: " << options_.duration.count() << " s\n\n"
              << "Waiting for a fresh RVT1 frame (enter the vehicle in-game to publish one). The first "
                 "sequence found on disk is only a baseline and is never reported as fresh by itself.\n";

    // 500ms: generous relative to the Lua side's documented (not
    // necessarily achieved -- see the measured-rate instrumentation
    // below) 20 Hz publish cap, so ordinary jitter never reads as stale,
    // but still clearly bounded -- this is the only place that decides
    // staleness; the parser/tracker themselves have no opinion on the
    // number.
    constexpr std::chrono::milliseconds kStaleAfter{500};
    VehicleTelemetryFreshnessTracker tracker(kStaleAfter);

    const auto frameInterval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / options_.rateHz));
    const auto deadline = std::chrono::steady_clock::now() + options_.duration;
    auto nextTick = std::chrono::steady_clock::now();

    std::uint64_t polls = 0;
    std::uint64_t freshPolls = 0;
    std::optional<std::uint64_t> lastPrintedSequence;
    bool wasStale = true;
    std::optional<FreshVehicleTelemetrySample> lastFreshSample;
    std::optional<std::chrono::milliseconds> lastUnavailableAge;

    // Instrumentation: classifies each poll by the RAW parsed sequence
    // (independent of the valid/local/baseline rules above), to measure
    // how often the file actually changes -- never assumed from the Lua
    // side's own 20 Hz publish-interval constant.
    std::optional<std::uint64_t> lastRawSequence;
    std::uint64_t newSequenceCount = 0;
    std::uint64_t repeatedPollCount = 0;
    std::uint64_t missingOrInvalidCount = 0;
    std::optional<std::chrono::steady_clock::time_point> lastNewSequenceObservedAt;
    auto minInterval = std::chrono::steady_clock::duration::max();
    auto maxInterval = std::chrono::steady_clock::duration::zero();
    auto sumIntervals = std::chrono::steady_clock::duration::zero();
    std::uint64_t intervalSampleCount = 0;

    while (!stopRequested_.load() && std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const auto parsed = ReadVehicleTelemetryFile(telemetryPath);
        ++polls;

        if (parsed.has_value()) {
            if (!lastRawSequence.has_value() || parsed->sequence != *lastRawSequence) {
                ++newSequenceCount;
                if (lastNewSequenceObservedAt.has_value()) {
                    const auto interval = now - *lastNewSequenceObservedAt;
                    minInterval = (std::min)(minInterval, interval);
                    maxInterval = (std::max)(maxInterval, interval);
                    sumIntervals += interval;
                    ++intervalSampleCount;
                }
                lastNewSequenceObservedAt = now;
                lastRawSequence = parsed->sequence;
            } else {
                ++repeatedPollCount;
            }
        } else {
            ++missingOrInvalidCount;
        }

        const auto current = tracker.Observe(parsed, now);

        if (current.has_value()) {
            ++freshPolls;
            lastFreshSample = current;
            const bool isNewSequence =
                !lastPrintedSequence.has_value() || current->frame.sequence != *lastPrintedSequence;
            if (isNewSequence || wasStale) {
                lastPrintedSequence = current->frame.sequence;
                const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - current->receivedAt);
                std::cout << "[seq=" << current->frame.sequence << " age=" << age.count() << "ms] "
                          << "valid=" << (current->frame.valid ? 1 : 0)
                          << " local=" << (current->frame.localPlayer ? 1 : 0)
                          << " speed=" << current->frame.speedMetersPerSecond << " m/s"
                          << " forward=" << current->frame.forwardMetersPerSecond << " m/s"
                          << " lateral=" << current->frame.lateralMetersPerSecond << " m/s yaw=";
                if (current->frame.yawRateRadiansPerSecond) {
                    std::cout << *current->frame.yawRateRadiansPerSecond << " rad/s\n";
                } else {
                    std::cout << "unavailable\n";
                }
            }
            wasStale = false;
        } else if (!wasStale) {
            wasStale = true;
            if (lastFreshSample.has_value()) {
                const auto age =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFreshSample->receivedAt);
                lastUnavailableAge = age;
                std::cout << "telemetry unavailable (no fresh frame; last usable sample was " << age.count()
                          << "ms old)\n";
            } else {
                std::cout << "telemetry unavailable (no fresh frame; nothing usable observed yet)\n";
            }
        }

        nextTick += frameInterval;
        const auto afterWork = std::chrono::steady_clock::now();
        if (afterWork > nextTick) {
            nextTick = afterWork;
        } else {
            std::this_thread::sleep_until(nextTick);
        }
    }

    std::cout << "\nTelemetry monitor finished: " << polls << " polls, " << freshPolls << " with a fresh frame.\n";
    std::cout << "Instrumentation (measured, not assumed from the Lua side's configured cap):\n"
              << "  new sequences observed: " << newSequenceCount << "\n"
              << "  repeated polls:         " << repeatedPollCount << "\n"
              << "  missing/invalid reads:  " << missingOrInvalidCount << "\n";
    if (intervalSampleCount > 0) {
        using DoubleMs = std::chrono::duration<double, std::milli>;
        const double minMs = std::chrono::duration_cast<DoubleMs>(minInterval).count();
        const double maxMs = std::chrono::duration_cast<DoubleMs>(maxInterval).count();
        const double avgMs = std::chrono::duration_cast<DoubleMs>(sumIntervals).count() /
                              static_cast<double>(intervalSampleCount);
        std::cout << "  new-sequence interval:  min=" << minMs << "ms avg=" << avgMs << "ms max=" << maxMs
                  << "ms (n=" << intervalSampleCount << ")\n";
        if (avgMs > 0.0) {
            std::cout << "  effective rate:         " << (1000.0 / avgMs) << " Hz (measured across the whole run, "
                       "including any idle/stale periods)\n";
        }
    } else {
        std::cout << "  new-sequence interval:  not enough new sequences observed to measure\n";
    }
    if (lastUnavailableAge.has_value()) {
        std::cout << "  age at last 'unavailable' transition: " << lastUnavailableAge->count() << "ms\n";
    }

    return 0;
}

} // namespace rvwheel::tools::probe
