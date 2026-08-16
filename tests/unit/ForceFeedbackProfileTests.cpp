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

TEST_CASE("ProfileLoader: a forceFeedback block with no speedSensitiveSpring key stays inert "
          "(backward compatibility with profiles written before this field existed)",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"enabled": true, "springStrength": 0.2}
    })");
    REQUIRE(result.profile.has_value());
    REQUIRE(result.profile->forceFeedback.has_value());
    REQUIRE_FALSE(result.profile->forceFeedback->speedSensitiveSpring.enabled);
    REQUIRE(result.profile->forceFeedback->speedSensitiveSpring.minimumScale == 0.25f);
    REQUIRE(result.profile->forceFeedback->speedSensitiveSpring.fullStrengthSpeedMetersPerSecond == 5.0f);
}

TEST_CASE("ProfileLoader: an explicit speedSensitiveSpring block is parsed with the exact values given",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {
            "enabled": true,
            "speedSensitiveSpring": {"enabled": true, "minimumScale": 0.4, "fullStrengthSpeedMetersPerSecond": 8.0}
        }
    })");
    REQUIRE(result.profile.has_value());
    const auto& sss = result.profile->forceFeedback->speedSensitiveSpring;
    REQUIRE(sss.enabled);
    REQUIRE(sss.minimumScale == 0.4f);
    REQUIRE(sss.fullStrengthSpeedMetersPerSecond == 8.0f);
}

TEST_CASE("ProfileLoader: speedSensitiveSpring.enabled=false leaves the block inert regardless of its other fields",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {
            "enabled": true,
            "speedSensitiveSpring": {"enabled": false, "minimumScale": 0.9, "fullStrengthSpeedMetersPerSecond": 1.0}
        }
    })");
    REQUIRE(result.profile.has_value());
    REQUIRE_FALSE(result.profile->forceFeedback->speedSensitiveSpring.enabled);
}

TEST_CASE("ProfileLoader: speedSensitiveSpring must be a JSON object",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": true}
    })");
    REQUIRE_FALSE(result.profile.has_value());
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ProfileLoader: speedSensitiveSpring.minimumScale outside [0,1] is rejected, not clamped",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring][Invalid]") {
    const auto tooHigh = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": {"minimumScale": 1.5}}
    })");
    REQUIRE_FALSE(tooHigh.profile.has_value());

    const auto negative = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": {"minimumScale": -0.1}}
    })");
    REQUIRE_FALSE(negative.profile.has_value());
}

TEST_CASE("ProfileLoader: speedSensitiveSpring.fullStrengthSpeedMetersPerSecond must be positive and "
          "within the conservative sanity ceiling",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring][Invalid]") {
    const auto zero = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": {"fullStrengthSpeedMetersPerSecond": 0.0}}
    })");
    REQUIRE_FALSE(zero.profile.has_value());

    const auto negative = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": {"fullStrengthSpeedMetersPerSecond": -5.0}}
    })");
    REQUIRE_FALSE(negative.profile.has_value());

    const auto tooHigh = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": {"fullStrengthSpeedMetersPerSecond": 500.0}}
    })");
    REQUIRE_FALSE(tooHigh.profile.has_value());

    const auto valid = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": {"fullStrengthSpeedMetersPerSecond": 5.0}}
    })");
    REQUIRE(valid.profile.has_value());
}

TEST_CASE("ProfileLoader: speedSensitiveSpring.enabled must be a boolean",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"speedSensitiveSpring": {"enabled": 1}}
    })");
    REQUIRE_FALSE(result.profile.has_value());
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ProfileLoader: Serialize then ParseFromString round-trips an enabled speedSensitiveSpring block",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring]") {
    const auto parsed = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {
            "enabled": true, "springStrength": 0.2,
            "speedSensitiveSpring": {"enabled": true, "minimumScale": 0.25, "fullStrengthSpeedMetersPerSecond": 5.0}
        }
    })");
    REQUIRE(parsed.profile.has_value());

    const std::string serialized = ProfileLoader::Serialize(*parsed.profile);
    const auto roundTripped = ProfileLoader::ParseFromString(serialized);
    REQUIRE(roundTripped.profile.has_value());
    const auto& sss = roundTripped.profile->forceFeedback->speedSensitiveSpring;
    REQUIRE(sss.enabled);
    REQUIRE(sss.minimumScale == 0.25f);
    REQUIRE(sss.fullStrengthSpeedMetersPerSecond == 5.0f);
}

TEST_CASE("ProfileLoader: Serialize then ParseFromString round-trips a disabled (default) speedSensitiveSpring "
          "block exactly, proving absence and enabled=false are indistinguishable in behavior",
          "[ProfileLoader][ForceFeedback][SpeedSensitiveSpring]") {
    const auto parsed = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "forceFeedback": {"enabled": true, "springStrength": 0.2}
    })");
    REQUIRE(parsed.profile.has_value());

    const std::string serialized = ProfileLoader::Serialize(*parsed.profile);
    const auto roundTripped = ProfileLoader::ParseFromString(serialized);
    REQUIRE(roundTripped.profile.has_value());
    REQUIRE_FALSE(roundTripped.profile->forceFeedback->speedSensitiveSpring.enabled);
}
