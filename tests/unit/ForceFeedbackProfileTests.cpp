#include <catch2/catch_test_macros.hpp>

#include "rvwheel/profiles/ProfileLoader.hpp"

using rvwheel::profiles::ProfileLoader;

TEST_CASE("ProfileLoader: a profile with no forceFeedback block leaves it absent, never a default-enabled config",
          "[ProfileLoader][ForceFeedback]") {
    const auto result =
        ProfileLoader::ParseFromString(R"({"schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"}})");
    REQUIRE(result.profile.has_value());
    REQUIRE_FALSE(result.profile->forceFeedback.has_value());
}

TEST_CASE("ProfileLoader: an explicit forceFeedback block is parsed with the exact values given",
          "[ProfileLoader][ForceFeedback]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {
            "enabled": true,
            "masterGain": 0.4,
            "invertDirection": true,
            "springStrength": 0.6,
            "damperStrength": 0.3,
            "selfAligningTorqueStrength": 0.1,
            "maxTorqueNormalized": 0.5,
            "deadband": 0.02,
            "slewRatePerSecond": 3.0,
            "watchdogTimeoutMilliseconds": 250
        }
    })");

    REQUIRE(result.profile.has_value());
    REQUIRE(result.profile->forceFeedback.has_value());
    const auto& ffb = *result.profile->forceFeedback;
    REQUIRE(ffb.enabled);
    REQUIRE(ffb.masterGain == 0.4f);
    REQUIRE(ffb.invertDirection);
    REQUIRE(ffb.springStrength == 0.6f);
    REQUIRE(ffb.damperStrength == 0.3f);
    REQUIRE(ffb.selfAligningTorqueStrength == 0.1f);
    REQUIRE(ffb.maxTorqueNormalized == 0.5f);
    REQUIRE(ffb.deadband == 0.02f);
    REQUIRE(ffb.slewRatePerSecond == 3.0f);
    REQUIRE(ffb.watchdogTimeout == std::chrono::milliseconds{250});
}

TEST_CASE("ProfileLoader: an empty forceFeedback block still yields ForceFeedbackConfig's own safe defaults",
          "[ProfileLoader][ForceFeedback]") {
    const auto result = ProfileLoader::ParseFromString(
        R"({"schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},"forceFeedback":{}})");
    REQUIRE(result.profile.has_value());
    REQUIRE(result.profile->forceFeedback.has_value());
    REQUIRE_FALSE(result.profile->forceFeedback->enabled);
    REQUIRE(result.profile->forceFeedback->masterGain == 0.0f);
}

TEST_CASE("ProfileLoader: forceFeedback must be a JSON object", "[ProfileLoader][ForceFeedback][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(
        R"({"schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},"forceFeedback":"on"})");
    REQUIRE_FALSE(result.profile.has_value());
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ProfileLoader: an out-of-range forceFeedback gain is rejected, not silently clamped",
          "[ProfileLoader][ForceFeedback][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"masterGain": 5.0}
    })");
    REQUIRE_FALSE(result.profile.has_value());
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ProfileLoader: enabled must be a boolean, not e.g. a string", "[ProfileLoader][ForceFeedback][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"enabled": "true"}
    })");
    REQUIRE_FALSE(result.profile.has_value());
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ProfileLoader: a zero or negative watchdogTimeoutMilliseconds is rejected",
          "[ProfileLoader][ForceFeedback][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"watchdogTimeoutMilliseconds": 0}
    })");
    REQUIRE_FALSE(result.profile.has_value());
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ProfileLoader: Serialize then ParseFromString round-trips an enabled forceFeedback block",
          "[ProfileLoader][ForceFeedback]") {
    const auto parsed = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"enabled": true, "masterGain": 0.5, "springStrength": 0.25}
    })");
    REQUIRE(parsed.profile.has_value());

    const std::string serialized = ProfileLoader::Serialize(*parsed.profile);
    const auto roundTripped = ProfileLoader::ParseFromString(serialized);
    REQUIRE(roundTripped.profile.has_value());
    REQUIRE(roundTripped.profile->forceFeedback.has_value());
    REQUIRE(roundTripped.profile->forceFeedback->enabled);
    REQUIRE(roundTripped.profile->forceFeedback->masterGain == 0.5f);
    REQUIRE(roundTripped.profile->forceFeedback->springStrength == 0.25f);
}

TEST_CASE("ProfileLoader: Serialize omits the forceFeedback key entirely when the profile never set one",
          "[ProfileLoader][ForceFeedback]") {
    const auto parsed =
        ProfileLoader::ParseFromString(R"({"schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"}})");
    REQUIRE(parsed.profile.has_value());

    const std::string serialized = ProfileLoader::Serialize(*parsed.profile);
    REQUIRE(serialized.find("forceFeedback") == std::string::npos);
}
