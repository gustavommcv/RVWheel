#include <catch2/catch_test_macros.hpp>

#include "BridgeForceFeedbackSession.hpp"
#include "VehicleTelemetryTransport.hpp"
#include "support/FakeWheelDevice.hpp"

using rvwheel::dal::DeviceId;
using rvwheel::dal::DeviceInfo;
using rvwheel::ffb::ForceFeedbackConfig;
using rvwheel::ffb::ForceFeedbackState;
using rvwheel::ffb::VehicleTelemetry;
using rvwheel::testing::FakeWheelDevice;
using rvwheel::tools::probe::ToVehicleTelemetry;
using rvwheel::tools::probe::VehicleTelemetryFrame;
using rvwheel::tools::probe::VehicleTelemetryFreshnessTracker;

// Integration-level tests for the wiring between the already-validated RVT1
// transport (tools/device_probe/VehicleTelemetryTransport) and
// BridgeForceFeedbackSession's adaptive path. Curve-shape correctness
// (smoothstep, minimumScale, NaN/Inf/negative handling, never exceeding
// springStrength) is exhaustively covered at the pure-source level in
// SpeedSensitiveSpringSourceTests.cpp; this file is about whether the
// pieces are actually wired together correctly -- timestamps, staleness,
// baseline suppression, and the static path's independence from RVT1.
// Uses only FakeWheelDevice -- never touches a real device or DirectInput.

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point T(long long ms) { return Clock::time_point{} + std::chrono::milliseconds{ms}; }

DeviceInfo MakeFakeInfo() {
    DeviceInfo info;
    info.id = DeviceId::FromValue(1);
    info.name = "Fake Bridge Adaptive FFB Test Device";
    info.capabilities.hasForceFeedback = true;
    return info;
}

// The exact values from the temporary physical test profile prepared for
// the (not-yet-authorized) hardware run: the same limits already
// physically validated for the static spring, plus speedSensitiveSpring
// opted in.
ForceFeedbackConfig AdaptiveValidatedConfig() {
    ForceFeedbackConfig config;
    config.enabled = true;
    config.masterGain = 0.2f;
    config.springStrength = 0.2f;
    config.damperStrength = 0.0f;
    config.maxTorqueNormalized = 0.2f;
    config.slewRatePerSecond = 1000.0f; // Fast so tests aren't dominated by ramping, matching the static suite's pattern.
    config.watchdogTimeout = std::chrono::milliseconds{200};
    config.speedSensitiveSpring.enabled = true;
    config.speedSensitiveSpring.minimumScale = 0.25f;
    config.speedSensitiveSpring.fullStrengthSpeedMetersPerSecond = 5.0f;
    return config;
}

VehicleTelemetryFrame MakeFrame(std::uint64_t sequence, float speed, bool valid = true, bool localPlayer = true) {
    VehicleTelemetryFrame frame;
    frame.sequence = sequence;
    frame.valid = valid;
    frame.localPlayer = localPlayer;
    frame.speedMetersPerSecond = speed;
    frame.forwardMetersPerSecond = speed;
    frame.lateralMetersPerSecond = 0.0f;
    return frame;
}
} // namespace

TEST_CASE("BridgeForceFeedbackSession (adaptive): with no telemetry at all, never applies force",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    session.TickWithTelemetry(T(0), std::nullopt, T(0));
    session.TickWithTelemetry(T(20), std::nullopt, T(0));
    session.TickWithTelemetry(T(40), std::nullopt, T(0));

    REQUIRE(device.forceFeedbackCallCount == 0);
    REQUIRE(session.State() != ForceFeedbackState::Active);
}

TEST_CASE("BridgeForceFeedbackSession (adaptive): a fresh, valid, local sample activates a speed-scaled spring",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    VehicleTelemetry lowSpeed = ToVehicleTelemetry(
        rvwheel::tools::probe::FreshVehicleTelemetrySample{MakeFrame(2, 0.0f), T(0)});
    session.TickWithTelemetry(T(0), lowSpeed, T(0));
    session.TickWithTelemetry(T(20), lowSpeed, T(0));

    REQUIRE(device.forceFeedbackCallCount >= 1);
    const float lowSpeedSpring = device.appliedCommands.back().spring;
    REQUIRE(lowSpeedSpring > 0.0f);        // minimumScale > 0, so some spring even at rest.
    REQUIRE(lowSpeedSpring < 0.2f);        // But less than full springStrength.

    // A new RVT1 sequence "just arrived" at T(20); both further ticks stay
    // well within the 200ms watchdog window relative to that receivedAt.
    VehicleTelemetry fullSpeed = ToVehicleTelemetry(
        rvwheel::tools::probe::FreshVehicleTelemetrySample{MakeFrame(3, 5.0f), T(20)});
    session.TickWithTelemetry(T(40), fullSpeed, T(20));
    session.TickWithTelemetry(T(60), fullSpeed, T(20)); // Slew rate is fast; settles within a couple of ticks.

    REQUIRE(device.appliedCommands.back().spring > lowSpeedSpring);
    REQUIRE(device.appliedCommands.back().spring <= 0.2f + 1e-4f);
}

TEST_CASE("BridgeForceFeedbackSession (adaptive): the safety controller receives the sample's own receivedAt, "
          "never `now`",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    VehicleTelemetry telemetry = ToVehicleTelemetry(
        rvwheel::tools::probe::FreshVehicleTelemetrySample{MakeFrame(2, 2.0f), T(0)});
    // The sample was captured at T(0), but this tick happens at T(50) --
    // exactly the gap a real bridge loop sees when the same RVT1 sequence
    // is still the latest one several polls later.
    session.TickWithTelemetry(T(50), telemetry, T(0));

    const auto diagnostics = session.Diagnostics(T(50));
    REQUIRE(diagnostics.telemetryAge == std::chrono::milliseconds{50});
}

TEST_CASE("BridgeForceFeedbackSession (adaptive): telemetry that stops advancing eventually triggers the "
          "watchdog and StopForceFeedback, exactly like any other stale telemetry",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    VehicleTelemetry telemetry = ToVehicleTelemetry(
        rvwheel::tools::probe::FreshVehicleTelemetrySample{MakeFrame(2, 3.0f), T(0)});
    session.TickWithTelemetry(T(0), telemetry, T(0));
    session.TickWithTelemetry(T(20), telemetry, T(0));
    REQUIRE(session.State() == ForceFeedbackState::Active);

    // The same sample (same receivedAt = T(0)) keeps being fed, as a real
    // bridge loop would while the RVT1 sequence number hasn't changed --
    // but enough wall-clock time has now passed to exceed watchdogTimeout
    // (200ms).
    session.TickWithTelemetry(T(300), telemetry, T(0));

    REQUIRE(session.State() == ForceFeedbackState::Armed); // Watchdog-stopped, not Faulted.
    REQUIRE(device.appliedCommands.back().spring == 0.0f);
    REQUIRE(device.stopForceFeedbackCallCount >= 1);
}

TEST_CASE("BridgeForceFeedbackSession (adaptive): a baseline-only observation from the freshness tracker "
          "never activates force",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    // Exactly what RunBridge does each tick: hand the raw parsed frame to
    // the tracker first, and only build/feed a VehicleTelemetry if the
    // tracker actually returns a usable sample.
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    const auto baseline = tracker.Observe(MakeFrame(34859, 12.0f), T(0)); // A high-speed leftover frame, on purpose.
    REQUIRE_FALSE(baseline.has_value());

    // Exactly what RunBridge does when the tracker returns nothing usable.
    session.TickWithTelemetry(T(0), std::nullopt, T(0));
    session.TickWithTelemetry(T(20), std::nullopt, T(0));

    REQUIRE(device.forceFeedbackCallCount == 0);
}

TEST_CASE("BridgeForceFeedbackSession (adaptive): an invalid or non-local sequence from the tracker "
          "never activates force",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    static_cast<void>(tracker.Observe(MakeFrame(1, 0.0f), T(0))); // Baseline.
    const auto invalid = tracker.Observe(MakeFrame(2, 10.0f, /*valid=*/false, /*localPlayer=*/true), T(20));
    REQUIRE_FALSE(invalid.has_value());

    const auto nonLocal = tracker.Observe(MakeFrame(3, 10.0f, /*valid=*/true, /*localPlayer=*/false), T(40));
    REQUIRE_FALSE(nonLocal.has_value());

    session.TickWithTelemetry(T(20), std::nullopt, T(0));
    session.TickWithTelemetry(T(40), std::nullopt, T(0));

    REQUIRE(device.forceFeedbackCallCount == 0);
}

TEST_CASE("BridgeForceFeedbackSession (adaptive): a backend failure faults the session with no automatic re-arm, "
          "exactly like the static path",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    VehicleTelemetry telemetry = ToVehicleTelemetry(
        rvwheel::tools::probe::FreshVehicleTelemetrySample{MakeFrame(2, 3.0f), T(0)});
    session.TickWithTelemetry(T(0), telemetry, T(0));
    device.nextApplyForceFeedbackFailure = rvwheel::dal::Status::BackendError("simulated failure");
    session.TickWithTelemetry(T(20), telemetry, T(0));

    REQUIRE(session.IsFaulted());

    for (long long ms = 40; ms <= 200; ms += 20) {
        session.TickWithTelemetry(T(ms), telemetry, T(0));
        REQUIRE(session.IsFaulted());
    }
}

TEST_CASE("BridgeForceFeedbackSession: a session built with speedSensitiveSpring.enabled=false never depends "
          "on RVT1 -- Tick(now) alone reproduces the exact static spring, with no telemetry involved",
          "[Bridge][FFB][Adaptive][Static]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackConfig config = AdaptiveValidatedConfig();
    config.speedSensitiveSpring.enabled = false; // Explicitly present but disabled, not merely absent.
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, config);

    REQUIRE(session.Enable().IsOk());
    session.Tick(T(0));
    session.Tick(T(20));

    REQUIRE(device.forceFeedbackCallCount >= 1);
    REQUIRE(device.appliedCommands.back().spring == 0.2f); // Full springStrength, never scaled by speed.
}

TEST_CASE("BridgeForceFeedbackSession (adaptive): extreme speed values still respect the same "
          "already-validated absolute limits as the static path",
          "[Bridge][FFB][Adaptive]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, AdaptiveValidatedConfig());
    REQUIRE(session.Enable().IsOk());

    VehicleTelemetry extremeSpeed = ToVehicleTelemetry(
        rvwheel::tools::probe::FreshVehicleTelemetrySample{MakeFrame(2, 200.0f), T(0)});
    session.TickWithTelemetry(T(0), extremeSpeed, T(0));
    session.TickWithTelemetry(T(1000), extremeSpeed, T(0) + std::chrono::milliseconds{980});

    REQUIRE(device.appliedCommands.back().spring <= 0.2f + 1e-4f);
    REQUIRE(device.appliedCommands.back().damper == 0.0f);
    REQUIRE(device.appliedCommands.back().constantForce == 0.0f);
}
