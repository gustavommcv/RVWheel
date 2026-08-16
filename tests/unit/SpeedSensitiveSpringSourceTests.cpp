#include <cmath>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "rvwheel/ffb/SpeedSensitiveSpringSource.hpp"

using Catch::Approx;
using rvwheel::ffb::ComputeSpeedSensitiveSpringScale;
using rvwheel::ffb::ForceFeedbackConfig;
using rvwheel::ffb::SpeedSensitiveSpringSource;
using rvwheel::ffb::VehicleTelemetry;

namespace {
ForceFeedbackConfig ValidatedConfig() {
    // The exact values physically validated for the static spring, reused
    // here so the speed-sensitive source is exercised against the same
    // real limits it will actually run with -- never a looser demonstration
    // config.
    ForceFeedbackConfig config;
    config.enabled = true;
    config.masterGain = 0.2f;
    config.springStrength = 0.2f;
    config.damperStrength = 0.0f;
    config.maxTorqueNormalized = 0.2f;
    config.slewRatePerSecond = 0.5f;
    config.watchdogTimeout = std::chrono::milliseconds{200};
    config.speedSensitiveSpring.enabled = true;
    config.speedSensitiveSpring.minimumScale = 0.25f;
    config.speedSensitiveSpring.fullStrengthSpeedMetersPerSecond = 5.0f;
    return config;
}

VehicleTelemetry WithSpeed(float speed) {
    VehicleTelemetry telemetry;
    telemetry.speedMetersPerSecond = speed;
    return telemetry;
}
} // namespace

TEST_CASE("SpeedSensitiveSpringSource: no valid speed produces an all-zero command", "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());
    const auto command = source.Compute(VehicleTelemetry{}, 0.0f); // speedMetersPerSecond absent.
    REQUIRE(command.spring == 0.0f);
    REQUIRE(command.damper == 0.0f);
    REQUIRE(command.constantForce == 0.0f);
    REQUIRE(command.gain == 1.0f);
}

TEST_CASE("SpeedSensitiveSpringSource: NaN, Inf, and negative speed all produce a safe zero command",
          "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());

    const auto nan = source.Compute(WithSpeed(std::numeric_limits<float>::quiet_NaN()), 0.0f);
    REQUIRE(nan.spring == 0.0f);
    REQUIRE(nan.damper == 0.0f);
    REQUIRE(nan.constantForce == 0.0f);

    const auto inf = source.Compute(WithSpeed(std::numeric_limits<float>::infinity()), 0.0f);
    REQUIRE(inf.spring == 0.0f);

    const auto negative = source.Compute(WithSpeed(-0.001f), 0.0f);
    REQUIRE(negative.spring == 0.0f);
    REQUIRE(negative.damper == 0.0f);
}

TEST_CASE("SpeedSensitiveSpringSource: speed=0 produces exactly springStrength * minimumScale",
          "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());
    const auto command = source.Compute(WithSpeed(0.0f), 0.0f);
    REQUIRE(command.spring == Approx(0.2f * 0.25f));
}

TEST_CASE("SpeedSensitiveSpringSource: speed at or beyond fullStrengthSpeedMetersPerSecond produces at most "
          "springStrength, never more",
          "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());

    const auto atFullStrength = source.Compute(WithSpeed(5.0f), 0.0f);
    REQUIRE(atFullStrength.spring == Approx(0.2f));

    const auto wellAboveFullStrength = source.Compute(WithSpeed(10.0f), 0.0f);
    REQUIRE(wellAboveFullStrength.spring == Approx(0.2f));
    REQUIRE(wellAboveFullStrength.spring <= 0.2f);

    const auto extremeSpeed = source.Compute(WithSpeed(200.0f), 0.0f);
    REQUIRE(extremeSpeed.spring <= 0.2f);
}

TEST_CASE("SpeedSensitiveSpringSource: intermediate speeds follow a smoothstep curve, not a linear ramp or a step",
          "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());

    // Hand-computed from smoothstep(t) = t^2*(3-2t), t = speed/5, scaled
    // into [minimumScale, 1] then multiplied by springStrength=0.2 -- see
    // docs/game-integration/RVT1_TELEMETRY_VALIDATION.md-style validation
    // table for the same values presented to the operator.
    REQUIRE(source.Compute(WithSpeed(1.0f), 0.0f).spring == Approx(0.0656f).margin(0.0005f));
    REQUIRE(source.Compute(WithSpeed(2.5f), 0.0f).spring == Approx(0.125f).margin(0.0005f));

    // Strictly increasing across the ramp (never a step): sampling closely
    // spaced speeds must never produce a flat or decreasing sequence.
    float previous = source.Compute(WithSpeed(0.0f), 0.0f).spring;
    for (float speed = 0.5f; speed <= 5.0f; speed += 0.5f) {
        const float current = source.Compute(WithSpeed(speed), 0.0f).spring;
        REQUIRE(current > previous);
        previous = current;
    }
}

TEST_CASE("SpeedSensitiveSpringSource: never exceeds the configured springStrength at any speed",
          "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());
    for (float speed = 0.0f; speed <= 20.0f; speed += 0.25f) {
        REQUIRE(source.Compute(WithSpeed(speed), 0.0f).spring <= 0.2f + 1e-6f);
    }
}

TEST_CASE("SpeedSensitiveSpringSource: constantForce is always zero, regardless of speed",
          "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());
    REQUIRE(source.Compute(WithSpeed(0.0f), 1.0f).constantForce == 0.0f);
    REQUIRE(source.Compute(WithSpeed(5.0f), -1.0f).constantForce == 0.0f);
    REQUIRE(source.Compute(VehicleTelemetry{}, 1.0f).constantForce == 0.0f);
}

TEST_CASE("SpeedSensitiveSpringSource: damper passes through the configured damperStrength unscaled, "
          "never a newly invented curve",
          "[FFB][Source][Adaptive]") {
    ForceFeedbackConfig config = ValidatedConfig();
    config.damperStrength = 0.4f;
    const SpeedSensitiveSpringSource source(config);

    REQUIRE(source.Compute(WithSpeed(0.0f), 0.0f).damper == 0.4f);
    REQUIRE(source.Compute(WithSpeed(2.5f), 0.0f).damper == 0.4f);
    REQUIRE(source.Compute(WithSpeed(10.0f), 0.0f).damper == 0.4f);
}

TEST_CASE("SpeedSensitiveSpringSource: gain is always 1.0 so the safety controller applies masterGain",
          "[FFB][Source][Adaptive]") {
    const SpeedSensitiveSpringSource source(ValidatedConfig());
    REQUIRE(source.Compute(WithSpeed(0.0f), 0.0f).gain == 1.0f);
    REQUIRE(source.Compute(WithSpeed(5.0f), 0.0f).gain == 1.0f);
    REQUIRE(source.Compute(VehicleTelemetry{}, 0.0f).gain == 1.0f);
}

TEST_CASE("SpeedSensitiveSpringSource: Configure() replaces the tuning at runtime", "[FFB][Source][Adaptive]") {
    SpeedSensitiveSpringSource source;
    REQUIRE(source.Compute(WithSpeed(5.0f), 0.0f).spring == 0.0f); // Default springStrength is 0.

    source.Configure(ValidatedConfig());
    REQUIRE(source.Compute(WithSpeed(5.0f), 0.0f).spring == Approx(0.2f));
}

TEST_CASE("ComputeSpeedSensitiveSpringScale: curve table at 0, 1, 2.5, 5, and 10 m/s "
          "(minimumScale=0.25, fullStrengthSpeedMetersPerSecond=5.0)",
          "[FFB][Adaptive][CurveTable]") {
    CHECK(ComputeSpeedSensitiveSpringScale(0.0f, 0.25f, 5.0f) == Approx(0.25f).margin(0.0005f));
    CHECK(ComputeSpeedSensitiveSpringScale(1.0f, 0.25f, 5.0f) == Approx(0.328f).margin(0.0005f));
    CHECK(ComputeSpeedSensitiveSpringScale(2.5f, 0.25f, 5.0f) == Approx(0.625f).margin(0.0005f));
    CHECK(ComputeSpeedSensitiveSpringScale(5.0f, 0.25f, 5.0f) == Approx(1.0f).margin(0.0005f));
    CHECK(ComputeSpeedSensitiveSpringScale(10.0f, 0.25f, 5.0f) == Approx(1.0f).margin(0.0005f));
}

TEST_CASE("ComputeSpeedSensitiveSpringScale: a degenerate fullStrengthSpeedMetersPerSecond never divides by zero",
          "[FFB][Adaptive][Invalid]") {
    REQUIRE(ComputeSpeedSensitiveSpringScale(3.0f, 0.25f, 0.0f) == 1.0f);
    REQUIRE(ComputeSpeedSensitiveSpringScale(3.0f, 0.25f, -1.0f) == 1.0f);
}
