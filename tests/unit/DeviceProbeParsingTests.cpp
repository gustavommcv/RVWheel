#include <catch2/catch_test_macros.hpp>

#include "CliOptions.hpp"
#include "DeviceSelection.hpp"

using rvwheel::tools::probe::CliParser;
using rvwheel::tools::probe::DeviceSelectionOutcome;
using rvwheel::tools::probe::FfbTestEffect;
using rvwheel::tools::probe::FfbTestCooperativeLevel;
using rvwheel::tools::probe::ProbeMode;
using rvwheel::tools::probe::SelectDeviceForMonitoring;

TEST_CASE("CliParser: --help alone parses successfully", "[DeviceProbe][CliParser]") {
    const auto result = CliParser::Parse({L"--help"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::Help);
}

TEST_CASE("CliParser: --list alone parses successfully", "[DeviceProbe][CliParser]") {
    const auto result = CliParser::Parse({L"--list"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::List);
}

TEST_CASE("CliParser: --monitor uses documented defaults", "[DeviceProbe][CliParser]") {
    const auto result = CliParser::Parse({L"--monitor"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::Monitor);
    REQUIRE(result.options.duration == std::chrono::seconds{30});
    REQUIRE(result.options.rateHz == 60);
}

TEST_CASE("CliParser: --monitor accepts explicit duration and rate", "[DeviceProbe][CliParser]") {
    const auto result = CliParser::Parse({L"--monitor", L"--duration", L"10", L"--rate", L"30"});
    REQUIRE(result.success);
    REQUIRE(result.options.duration == std::chrono::seconds{10});
    REQUIRE(result.options.rateHz == 30);
}

TEST_CASE("CliParser: --bridge runs indefinitely by default (no bridgeDuration set) and accepts rate/profile",
          "[DeviceProbe][CliParser][Bridge]") {
    const auto result = CliParser::Parse({L"--bridge", L"--rate", L"120", L"--profile", L"wheel-profile"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::Bridge);
    REQUIRE(result.options.rateHz == 120);
    REQUIRE(result.options.profileSelector == L"wheel-profile");
    REQUIRE_FALSE(result.options.bridgeDuration.has_value());
}

TEST_CASE("CliParser: --bridge --duration bounds the run, in the same [1, 3600]s range as other modes",
          "[DeviceProbe][CliParser][Bridge]") {
    const auto result = CliParser::Parse({L"--bridge", L"--duration", L"5"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::Bridge);
    REQUIRE(result.options.bridgeDuration.has_value());
    REQUIRE(*result.options.bridgeDuration == std::chrono::seconds{5});
}

TEST_CASE("CliParser: --bridge --duration --enable-force-feedback combine (the limited physical test invocation)",
          "[DeviceProbe][CliParser][Bridge][FFB]") {
    const auto result = CliParser::Parse({L"--bridge", L"--enable-force-feedback", L"--duration", L"5"});
    REQUIRE(result.success);
    REQUIRE(result.options.enableForceFeedback);
    REQUIRE(result.options.bridgeDuration.has_value());
    REQUIRE(*result.options.bridgeDuration == std::chrono::seconds{5});
}

TEST_CASE("CliParser: --bridge --duration rejects out-of-range values like every other mode",
          "[DeviceProbe][CliParser][Bridge][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--bridge", L"--duration", L"0"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--bridge", L"--duration", L"3601"}).success);
}

TEST_CASE("CliParser: --ffb-simulate accepts duration/rate/profile like --monitor", "[DeviceProbe][CliParser][FfbSimulate]") {
    const auto result =
        CliParser::Parse({L"--ffb-simulate", L"--duration", L"5", L"--rate", L"60", L"--profile", L"wheel-profile"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::FfbSimulate);
    REQUIRE(result.options.duration == std::chrono::seconds{5});
    REQUIRE(result.options.rateHz == 60);
    REQUIRE(result.options.profileSelector == L"wheel-profile");
}

TEST_CASE("CliParser: --ffb-simulate with no options uses documented defaults", "[DeviceProbe][CliParser][FfbSimulate]") {
    const auto result = CliParser::Parse({L"--ffb-simulate"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::FfbSimulate);
    REQUIRE(result.options.duration == std::chrono::seconds{CliParser::kDefaultDurationSeconds});
    REQUIRE(result.options.rateHz == CliParser::kDefaultRateHz);
}

TEST_CASE("CliParser: --telemetry-monitor accepts duration/rate and uses documented defaults otherwise",
          "[DeviceProbe][CliParser][TelemetryMonitor]") {
    const auto withOptions = CliParser::Parse({L"--telemetry-monitor", L"--duration", L"10", L"--rate", L"30"});
    REQUIRE(withOptions.success);
    REQUIRE(withOptions.options.mode == ProbeMode::TelemetryMonitor);
    REQUIRE(withOptions.options.duration == std::chrono::seconds{10});
    REQUIRE(withOptions.options.rateHz == 30);

    const auto withDefaults = CliParser::Parse({L"--telemetry-monitor"});
    REQUIRE(withDefaults.success);
    REQUIRE(withDefaults.options.duration == std::chrono::seconds{CliParser::kDefaultDurationSeconds});
    REQUIRE(withDefaults.options.rateHz == CliParser::kDefaultRateHz);
}

TEST_CASE("CliParser: --telemetry-monitor rejects options that only apply to hardware-touching modes",
          "[DeviceProbe][CliParser][TelemetryMonitor][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--telemetry-monitor", L"--parent-pid", L"1234"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--telemetry-monitor", L"--profile", L"wheel-profile"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--telemetry-monitor", L"--enable-force-feedback"}).success);
}

TEST_CASE("CliParser: --telemetry-monitor conflicts with other modes", "[DeviceProbe][CliParser][TelemetryMonitor][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--telemetry-monitor", L"--bridge"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--list", L"--telemetry-monitor"}).success);
}

TEST_CASE("CliParser: --ffb-simulate conflicts with other modes", "[DeviceProbe][CliParser][FfbSimulate][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-simulate", L"--bridge"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--list", L"--ffb-simulate"}).success);
}

TEST_CASE("CliParser: --parent-pid does not apply to --ffb-simulate", "[DeviceProbe][CliParser][FfbSimulate][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-simulate", L"--parent-pid", L"123"}).success);
}

TEST_CASE("CliParser: --ffb-hw-test-stop-only parses alone with no extra options", "[DeviceProbe][CliParser][FfbHardwareTest]") {
    const auto result = CliParser::Parse({L"--ffb-hw-test-stop-only"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::FfbHardwareTestStopOnly);
    REQUIRE(result.options.ffbTestCooperativeLevel == FfbTestCooperativeLevel::Background);
}

TEST_CASE("CliParser: real FFB tests accept an explicit cooperative level",
          "[DeviceProbe][CliParser][FfbHardwareTest]") {
    const auto foreground =
        CliParser::Parse({L"--ffb-hw-test-stop-only", L"--ffb-cooperative-level", L"foreground"});
    REQUIRE(foreground.success);
    REQUIRE(foreground.options.ffbTestCooperativeLevel == FfbTestCooperativeLevel::ForegroundUnfocused);

    const auto focused =
        CliParser::Parse({L"--ffb-hw-test-stop-only", L"--ffb-cooperative-level", L"foreground-focused"});
    REQUIRE(focused.success);
    REQUIRE(focused.options.ffbTestCooperativeLevel == FfbTestCooperativeLevel::ForegroundFocused);

    const auto background = CliParser::Parse(
        {L"--ffb-hw-test-weak-effect", L"--effect", L"spring", L"--ffb-cooperative-level", L"background"});
    REQUIRE(background.success);
    REQUIRE(background.options.ffbTestCooperativeLevel == FfbTestCooperativeLevel::Background);
}

TEST_CASE("CliParser: cooperative level is hardware-test-only and rejects unknown values",
          "[DeviceProbe][CliParser][FfbHardwareTest][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--list", L"--ffb-cooperative-level", L"foreground"}).success);
    REQUIRE_FALSE(
        CliParser::Parse({L"--ffb-hw-test-stop-only", L"--ffb-cooperative-level", L"nonsense"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-hw-test-stop-only", L"--ffb-cooperative-level"}).success);
}

TEST_CASE("CliParser: --ffb-hw-test-stop-only rejects duration/rate/profile/parent-pid",
          "[DeviceProbe][CliParser][FfbHardwareTest][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-hw-test-stop-only", L"--duration", L"5"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-hw-test-stop-only", L"--rate", L"60"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-hw-test-stop-only", L"--profile", L"x"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-hw-test-stop-only", L"--parent-pid", L"123"}).success);
}

TEST_CASE("CliParser: --ffb-hw-test-stop-only conflicts with other modes", "[DeviceProbe][CliParser][FfbHardwareTest][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-hw-test-stop-only", L"--ffb-simulate"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--list", L"--ffb-hw-test-stop-only"}).success);
}

TEST_CASE("CliParser: --ffb-hw-test-weak-effect defaults to spring", "[DeviceProbe][CliParser][FfbHardwareTest]") {
    const auto result = CliParser::Parse({L"--ffb-hw-test-weak-effect"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::FfbHardwareTestWeakEffect);
    REQUIRE(result.options.ffbTestEffect == FfbTestEffect::Spring);
}

TEST_CASE("CliParser: --ffb-hw-test-weak-effect accepts --effect spring/damper", "[DeviceProbe][CliParser][FfbHardwareTest]") {
    const auto spring = CliParser::Parse({L"--ffb-hw-test-weak-effect", L"--effect", L"spring"});
    REQUIRE(spring.success);
    REQUIRE(spring.options.ffbTestEffect == FfbTestEffect::Spring);

    const auto damper = CliParser::Parse({L"--ffb-hw-test-weak-effect", L"--effect", L"damper"});
    REQUIRE(damper.success);
    REQUIRE(damper.options.ffbTestEffect == FfbTestEffect::Damper);
}

TEST_CASE("CliParser: --effect rejects an unknown value and only applies to --ffb-hw-test-weak-effect",
          "[DeviceProbe][CliParser][FfbHardwareTest][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-hw-test-weak-effect", L"--effect", L"nonsense"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--list", L"--effect", L"spring"}).success);
}

TEST_CASE("CliParser: --bridge --enable-force-feedback parses and sets the flag", "[DeviceProbe][CliParser][Bridge][FFB]") {
    const auto result = CliParser::Parse({L"--bridge", L"--enable-force-feedback"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::Bridge);
    REQUIRE(result.options.enableForceFeedback);
}

TEST_CASE("CliParser: --bridge without --enable-force-feedback leaves it false", "[DeviceProbe][CliParser][Bridge][FFB]") {
    const auto result = CliParser::Parse({L"--bridge"});
    REQUIRE(result.success);
    REQUIRE_FALSE(result.options.enableForceFeedback);
}

TEST_CASE("CliParser: --enable-force-feedback is rejected outside --bridge", "[DeviceProbe][CliParser][FFB][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--list", L"--enable-force-feedback"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--monitor", L"--enable-force-feedback"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--ffb-simulate", L"--enable-force-feedback"}).success);
}

TEST_CASE("CliParser: --bridge accepts a launcher parent process", "[DeviceProbe][CliParser][Bridge]") {
    const auto result = CliParser::Parse({L"--bridge", L"--parent-pid", L"4242"});
    REQUIRE(result.success);
    REQUIRE(result.options.parentProcessId == 4242);
}

TEST_CASE("CliParser: --parent-pid is bridge-only and rejects zero", "[DeviceProbe][CliParser][Bridge][Invalid]") {
    REQUIRE_FALSE(CliParser::Parse({L"--list", L"--parent-pid", L"4242"}).success);
    REQUIRE_FALSE(CliParser::Parse({L"--bridge", L"--parent-pid", L"0"}).success);
}

TEST_CASE("CliParser: --capture requires and preserves a path argument", "[DeviceProbe][CliParser]") {
    const auto result = CliParser::Parse({L"--capture", L"g923-capture.jsonl"});
    REQUIRE(result.success);
    REQUIRE(result.options.mode == ProbeMode::Capture);
    REQUIRE(result.options.capturePath == std::filesystem::path(L"g923-capture.jsonl"));
}

TEST_CASE("CliParser: --capture preserves a Unicode path exactly", "[DeviceProbe][CliParser][Unicode]") {
    const std::wstring unicodePath = L"captura-éã日本.jsonl";
    const auto result = CliParser::Parse({L"--capture", unicodePath});
    REQUIRE(result.success);
    REQUIRE(result.options.capturePath == std::filesystem::path(unicodePath));
}

TEST_CASE("CliParser: --capture without a path fails", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--capture"});
    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errorMessage.empty());
}

TEST_CASE("CliParser: no arguments fails with a usable error", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({});
    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errorMessage.empty());
}

TEST_CASE("CliParser: conflicting mode flags fail", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--list", L"--monitor"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: --duration below the minimum fails", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--monitor", L"--duration", L"0"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: --duration above the maximum fails", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--monitor", L"--duration", L"3601"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: --duration at the exact boundaries succeeds", "[DeviceProbe][CliParser]") {
    REQUIRE(CliParser::Parse({L"--monitor", L"--duration", L"1"}).success);
    REQUIRE(CliParser::Parse({L"--monitor", L"--duration", L"3600"}).success);
}

TEST_CASE("CliParser: --rate below the minimum fails", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--monitor", L"--rate", L"0"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: --rate above the maximum fails", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--monitor", L"--rate", L"251"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: --rate at the exact boundaries succeeds", "[DeviceProbe][CliParser]") {
    REQUIRE(CliParser::Parse({L"--monitor", L"--rate", L"1"}).success);
    REQUIRE(CliParser::Parse({L"--monitor", L"--rate", L"250"}).success);
}

TEST_CASE("CliParser: a negative-looking duration fails rather than being misread", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--monitor", L"--duration", L"-5"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: a non-numeric duration fails", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--monitor", L"--duration", L"abc"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: an unrecognized flag fails", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--bogus"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("CliParser: --duration/--rate are rejected outside polling modes", "[DeviceProbe][CliParser][Invalid]") {
    const auto result = CliParser::Parse({L"--list", L"--duration", L"10"});
    REQUIRE_FALSE(result.success);
}

TEST_CASE("DeviceSelection: zero devices", "[DeviceProbe][DeviceSelection]") {
    const auto result = SelectDeviceForMonitoring(0);
    REQUIRE(result.outcome == DeviceSelectionOutcome::NoDevices);
}

TEST_CASE("DeviceSelection: exactly one device", "[DeviceProbe][DeviceSelection]") {
    const auto result = SelectDeviceForMonitoring(1);
    REQUIRE(result.outcome == DeviceSelectionOutcome::SingleDevice);
    REQUIRE(result.selectedIndex == 0);
}

TEST_CASE("DeviceSelection: multiple devices selects the first and flags it", "[DeviceProbe][DeviceSelection]") {
    const auto result = SelectDeviceForMonitoring(5);
    REQUIRE(result.outcome == DeviceSelectionOutcome::MultipleDevicesUsingFirst);
    REQUIRE(result.selectedIndex == 0);
}
