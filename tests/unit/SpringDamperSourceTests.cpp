#include <catch2/catch_test_macros.hpp>

#include "rvwheel/ffb/SpringDamperSource.hpp"

using rvwheel::ffb::ForceFeedbackConfig;
using rvwheel::ffb::SpringDamperSource;
using rvwheel::ffb::VehicleTelemetry;

TEST_CASE("SpringDamperSource: with a default config, produces zero strength effects", "[FFB][Source]") {
    const SpringDamperSource source;
    const auto command = source.Compute(VehicleTelemetry{}, 0.0f);
    REQUIRE(command.spring == 0.0f);
    REQUIRE(command.damper == 0.0f);
    REQUIRE(command.constantForce == 0.0f);
}

TEST_CASE("SpringDamperSource: reports the configured spring/damper strength regardless of telemetry", "[FFB][Source]") {
    ForceFeedbackConfig config;
    config.springStrength = 0.6f;
    config.damperStrength = 0.4f;
    SpringDamperSource source(config);

    const auto withNoTelemetry = source.Compute(VehicleTelemetry{}, 0.0f);
    REQUIRE(withNoTelemetry.spring == 0.6f);
    REQUIRE(withNoTelemetry.damper == 0.4f);

    VehicleTelemetry telemetry;
    telemetry.speedMetersPerSecond = 30.0f;
    const auto withTelemetry = source.Compute(telemetry, 0.5f);
    REQUIRE(withTelemetry.spring == 0.6f);
    REQUIRE(withTelemetry.damper == 0.4f);
}

TEST_CASE("SpringDamperSource: never produces a constant-force component", "[FFB][Source]") {
    ForceFeedbackConfig config;
    config.springStrength = 1.0f;
    config.damperStrength = 1.0f;
    const SpringDamperSource source(config);
    const auto command = source.Compute(VehicleTelemetry{}, 1.0f);
    REQUIRE(command.constantForce == 0.0f);
}

TEST_CASE("SpringDamperSource: out-of-range config values are clamped, not passed through raw", "[FFB][Source]") {
    ForceFeedbackConfig config;
    config.springStrength = 5.0f;
    config.damperStrength = -3.0f;
    const SpringDamperSource source(config);
    const auto command = source.Compute(VehicleTelemetry{}, 0.0f);
    REQUIRE(command.spring == 1.0f);
    REQUIRE(command.damper == 0.0f);
}

TEST_CASE("SpringDamperSource: Configure() replaces the tuning at runtime", "[FFB][Source]") {
    SpringDamperSource source;
    REQUIRE(source.Compute(VehicleTelemetry{}, 0.0f).spring == 0.0f);

    ForceFeedbackConfig config;
    config.springStrength = 0.9f;
    source.Configure(config);
    REQUIRE(source.Compute(VehicleTelemetry{}, 0.0f).spring == 0.9f);
}
