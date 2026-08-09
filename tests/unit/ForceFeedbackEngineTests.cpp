#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "rvwheel/ffb/ForceFeedbackEngine.hpp"
#include "rvwheel/ffb/SpringDamperSource.hpp"
#include "support/FakeWheelDevice.hpp"

using rvwheel::dal::DeviceId;
using rvwheel::dal::DeviceInfo;
using rvwheel::dal::Status;
using rvwheel::ffb::ForceFeedbackConfig;
using rvwheel::ffb::ForceFeedbackEngine;
using rvwheel::ffb::ForceFeedbackMixer;
using rvwheel::ffb::ForceFeedbackSafetyController;
using rvwheel::ffb::ForceFeedbackState;
using rvwheel::ffb::IForceFeedbackSource;
using rvwheel::ffb::SpringDamperSource;
using rvwheel::ffb::VehicleTelemetry;
using rvwheel::testing::FakeWheelDevice;

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point T(long long ms) { return Clock::time_point{} + std::chrono::milliseconds{ms}; }

DeviceInfo MakeFakeInfo() {
    DeviceInfo info;
    info.id = DeviceId::FromValue(1);
    info.name = "Fake FFB Test Device";
    info.capabilities.hasForceFeedback = true;
    return info;
}

ForceFeedbackConfig EnabledConfig() {
    ForceFeedbackConfig config;
    config.enabled = true;
    config.masterGain = 1.0f;
    config.springStrength = 0.5f;
    config.slewRatePerSecond = 1000.0f;
    config.watchdogTimeout = std::chrono::milliseconds{200};
    return config;
}

ForceFeedbackEngine MakeEngine(const ForceFeedbackConfig& config) {
    std::vector<std::unique_ptr<IForceFeedbackSource>> sources;
    sources.push_back(std::make_unique<SpringDamperSource>(config));
    return ForceFeedbackEngine(ForceFeedbackSafetyController(config), ForceFeedbackMixer{}, std::move(sources));
}
} // namespace

TEST_CASE("ForceFeedbackEngine: never calls ApplyForceFeedback while disabled", "[FFB][Engine]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackEngine engine = MakeEngine(EnabledConfig());
    // Deliberately not calling Enable().

    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(0), T(0));
    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(20), T(20));

    REQUIRE(device.forceFeedbackCallCount == 0);
}

TEST_CASE("ForceFeedbackEngine: once enabled, applies the mixed spring command to the device", "[FFB][Engine]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackEngine engine = MakeEngine(EnabledConfig());
    engine.Enable();

    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(0), T(0));
    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(20), T(20));

    REQUIRE(device.forceFeedbackCallCount >= 1);
    REQUIRE(device.appliedCommands.back().spring > 0.0f);
}

TEST_CASE("ForceFeedbackEngine: a backend failure from the device is reflected as Faulted and stops the device",
          "[FFB][Engine]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackEngine engine = MakeEngine(EnabledConfig());
    engine.Enable();

    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(0), T(0));
    device.nextApplyForceFeedbackFailure = Status::BackendError("simulated CreateEffect failure");
    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(20), T(20));

    REQUIRE(engine.State() == ForceFeedbackState::Faulted);

    // The very next tick, regardless of what telemetry says, must stop the device.
    const bool stopped = engine.TickWithoutTelemetry(device, T(21)).stopDevice;
    REQUIRE(stopped);
    REQUIRE(device.stopForceFeedbackCallCount >= 1);
}

TEST_CASE("ForceFeedbackEngine: a disconnected device is recoverable, not a permanent fault", "[FFB][Engine]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackEngine engine = MakeEngine(EnabledConfig());
    engine.Enable();

    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(0), T(0));
    device.nextApplyForceFeedbackFailure = Status::NotConnected("simulated disconnect");
    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(20), T(20));

    REQUIRE(engine.State() != ForceFeedbackState::Faulted);
}

TEST_CASE("ForceFeedbackEngine: EmergencyStop always results in StopForceFeedback on the device", "[FFB][Engine]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackEngine engine = MakeEngine(EnabledConfig());
    engine.Enable();
    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(0), T(0));
    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(20), T(20));

    engine.EmergencyStop();
    engine.TickWithoutTelemetry(device, T(21));

    REQUIRE(device.stopForceFeedbackCallCount >= 1);
    REQUIRE(engine.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("ForceFeedbackEngine: watchdog timeout with no telemetry updates eventually stops the device",
          "[FFB][Engine]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackConfig config = EnabledConfig();
    config.watchdogTimeout = std::chrono::milliseconds{50};
    ForceFeedbackEngine engine = MakeEngine(config);
    engine.Enable();

    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(0), T(0));
    engine.Tick(device, VehicleTelemetry{}, 0.0f, T(10), T(10));

    // Nothing but idle ticks from here on -- simulates a stalled telemetry source.
    const auto decision = engine.TickWithoutTelemetry(device, T(1000));

    REQUIRE(decision.stopDevice);
    REQUIRE(device.stopForceFeedbackCallCount >= 1);
}
