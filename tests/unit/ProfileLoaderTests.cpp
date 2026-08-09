#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "rvwheel/dal/AxisNormalizer.hpp"
#include "rvwheel/profiles/ProfileLoader.hpp"

using rvwheel::dal::AxisCalibration;
using rvwheel::dal::AxisDirection;
using rvwheel::dal::AxisNormalizer;
using rvwheel::dal::AxisSource;
using rvwheel::dal::DeviceBackend;
using rvwheel::dal::PedalCalibration;
using rvwheel::profiles::DeviceProfile;
using rvwheel::profiles::ProfileLoader;

namespace {

// Mirrors configs/default_profiles/logitech-g923-ps-pc-directinput.json
// exactly (see docs/hardware/G923_DIRECTINPUT_CAPTURE.md for the captured
// evidence this profile is built from).
// Custom delimiter ("json"), not the bare R"(...)": the displayName value
// below contains a literal ")" (the closing paren of "DirectInput)"), and
// that character immediately followed by the JSON's closing quote forms
// exactly the bare raw-string terminator )" -- which would truncate the
// literal right there. R"json(...)json" cannot collide with that.
constexpr const char* kG923Json = R"json({
  "schemaVersion": 1,
  "profileId": "logitech-g923-ps-pc-directinput",
  "displayName": "Logitech G923 (PlayStation/PC, DirectInput)",
  "match": {
    "backend": "DirectInput",
    "vendorId": "0x046D",
    "productId": "0xC266"
  },
  "axes": {
    "steering": { "source": "X", "direction": "normal", "center": "rangeMidpoint" },
    "throttle": { "source": "Y", "direction": "inverted" },
    "brake": { "source": "Rz", "direction": "inverted" },
    "clutch": { "source": "Slider0", "direction": "inverted" }
  },
  "readiness": {
    "minimumWarmupMilliseconds": 2200,
    "stableSampleMilliseconds": 250,
    "maximumWaitMilliseconds": 5000,
    "requireAxisActivation": true,
    "activationThreshold": 0.05
  },
  "sanityChecks": { "expectedButtonCount": 25, "expectedPovCount": 1 }
})json";

} // namespace

TEST_CASE("ProfileLoader: parses the verified G923 profile completely", "[ProfileLoader][G923]") {
    const auto result = ProfileLoader::ParseFromString(kG923Json);
    REQUIRE(result.IsOk());
    const DeviceProfile& profile = *result.profile;

    REQUIRE(profile.schemaVersion == 1);
    REQUIRE(profile.profileId == "logitech-g923-ps-pc-directinput");
    REQUIRE(profile.match.backend == DeviceBackend::DirectInput);
    REQUIRE(profile.match.vendorId == std::uint16_t{0x046D});
    REQUIRE(profile.match.productId == std::uint16_t{0xC266});

    REQUIRE(profile.layout.steering.has_value());
    REQUIRE(profile.layout.steering->source == AxisSource::X);
    REQUIRE(profile.layout.steering->direction == AxisDirection::Normal); // Steering remains normal per the capture.

    REQUIRE(profile.layout.throttle.has_value());
    REQUIRE(profile.layout.throttle->source == AxisSource::Y);
    REQUIRE(profile.layout.throttle->direction == AxisDirection::Inverted);

    REQUIRE(profile.layout.brake.has_value());
    REQUIRE(profile.layout.brake->source == AxisSource::RotationZ);
    REQUIRE(profile.layout.brake->direction == AxisDirection::Inverted);

    REQUIRE(profile.layout.clutch.has_value());
    REQUIRE(profile.layout.clutch->source == AxisSource::Slider0);
    REQUIRE(profile.layout.clutch->direction == AxisDirection::Inverted);

    REQUIRE(profile.readiness.minimumWarmup == std::chrono::milliseconds{2200});
    REQUIRE(profile.readiness.stableSample == std::chrono::milliseconds{250});
    REQUIRE(profile.readiness.maximumWait == std::chrono::milliseconds{5000});
    REQUIRE(profile.readiness.requireAxisActivation);
    REQUIRE(profile.readiness.activationThreshold == Catch::Approx(0.05f));

    REQUIRE(profile.expectedButtonCount == std::uint16_t{25});
    REQUIRE(profile.expectedPovCount == std::uint8_t{1});
}

TEST_CASE("ProfileLoader: Serialize then ParseFromString round-trips the G923 profile", "[ProfileLoader][G923]") {
    const auto parsed = ProfileLoader::ParseFromString(kG923Json);
    REQUIRE(parsed.IsOk());

    const std::string serialized = ProfileLoader::Serialize(*parsed.profile);
    const auto reparsed = ProfileLoader::ParseFromString(serialized);
    REQUIRE(reparsed.IsOk());

    REQUIRE(reparsed.profile->profileId == parsed.profile->profileId);
    REQUIRE(reparsed.profile->match.vendorId == parsed.profile->match.vendorId);
    REQUIRE(reparsed.profile->match.productId == parsed.profile->match.productId);
    REQUIRE(reparsed.profile->layout.throttle->source == parsed.profile->layout.throttle->source);
    REQUIRE(reparsed.profile->layout.throttle->direction == parsed.profile->layout.throttle->direction);
    REQUIRE(reparsed.profile->readiness.minimumWarmup == parsed.profile->readiness.minimumWarmup);
    REQUIRE(reparsed.profile->readiness.requireAxisActivation);
    REQUIRE(reparsed.profile->readiness.activationThreshold == Catch::Approx(0.05f));
}

TEST_CASE("ProfileLoader: G923 pedal calibration matches the documented direction semantics", "[ProfileLoader][G923][AxisNormalizer]") {
    // Applies the ACTUAL swap logic DirectInputDevice::ApplyLayout uses
    // (AxisNormalizer::ResolvePedalCalibration), not a reimplementation,
    // against the profile's own "inverted" direction and a representative
    // DirectInput-style runtime-queried range.
    const auto parsed = ProfileLoader::ParseFromString(kG923Json);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.profile->layout.throttle->direction == AxisDirection::Inverted);

    const PedalCalibration cal = AxisNormalizer::ResolvePedalCalibration(0, 65535, parsed.profile->layout.throttle->direction);

    // "G923 released raw max -> output 0".
    REQUIRE(AxisNormalizer::NormalizePedal(65535, cal).value == Catch::Approx(0.0f));
    // "G923 pressed raw min -> output 1".
    REQUIRE(AxisNormalizer::NormalizePedal(0, cal).value == Catch::Approx(1.0f));
}

TEST_CASE("ProfileLoader: G923 steering calibration remains normal", "[ProfileLoader][G923][AxisNormalizer]") {
    const auto parsed = ProfileLoader::ParseFromString(kG923Json);
    REQUIRE(parsed.IsOk());
    REQUIRE(parsed.profile->layout.steering->direction == AxisDirection::Normal);

    const AxisCalibration cal = AxisNormalizer::ResolveSteeringCalibration(0, 65535, parsed.profile->layout.steering->direction);
    REQUIRE(AxisNormalizer::NormalizeSteering(0, cal).value == Catch::Approx(-1.0f));
    REQUIRE(AxisNormalizer::NormalizeSteering(65535, cal).value == Catch::Approx(1.0f));
}

TEST_CASE("ProfileLoader: malformed JSON fails with a document-level error", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString("{ not valid json");
    REQUIRE_FALSE(result.IsOk());
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("ProfileLoader: missing schemaVersion is rejected", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({"profileId":"x","match":{"backend":"DirectInput"}})");
    REQUIRE_FALSE(result.IsOk());
    const bool foundField =
        std::any_of(result.errors.begin(), result.errors.end(), [](const auto& e) { return e.path == "schemaVersion"; });
    REQUIRE(foundField);
}

TEST_CASE("ProfileLoader: unsupported schemaVersion is rejected", "[ProfileLoader][Invalid]") {
    const auto result =
        ProfileLoader::ParseFromString(R"({"schemaVersion":99,"profileId":"x","match":{"backend":"DirectInput"}})");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: empty profileId is rejected", "[ProfileLoader][Invalid]") {
    const auto result =
        ProfileLoader::ParseFromString(R"({"schemaVersion":1,"profileId":"","match":{"backend":"DirectInput"}})");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: unknown backend is rejected", "[ProfileLoader][Invalid]") {
    const auto result =
        ProfileLoader::ParseFromString(R"({"schemaVersion":1,"profileId":"x","match":{"backend":"Nonsense"}})");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: vendorId without productId (or vice versa) is rejected", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(
        R"({"schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput","vendorId":"0x046D"}})");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: a duplicate axis source across roles is rejected", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "axes": {
            "steering": {"source":"X","direction":"normal"},
            "throttle": {"source":"X","direction":"normal"}
        }
    })");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: an unknown axis source token is rejected", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "axes": { "steering": {"source":"NotAnAxis","direction":"normal"} }
    })");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: a boolean direction is rejected (must be an enumerated string)", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "axes": { "steering": {"source":"X","direction": true} }
    })");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: an unrecognized direction string is rejected", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "axes": { "steering": {"source":"X","direction":"backwards"} }
    })");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: a negative readiness time is rejected", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "readiness": {"minimumWarmupMilliseconds":-1,"stableSampleMilliseconds":0,"maximumWaitMilliseconds":0}
    })");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: a readiness time above the documented bound is rejected", "[ProfileLoader][Invalid]") {
    const auto result = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "readiness": {"minimumWarmupMilliseconds":0,"stableSampleMilliseconds":0,"maximumWaitMilliseconds":999999}
    })");
    REQUIRE_FALSE(result.IsOk());
}

TEST_CASE("ProfileLoader: activation readiness fields are strictly typed and bounded", "[ProfileLoader][Invalid]") {
    const auto wrongType = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "readiness": {
          "minimumWarmupMilliseconds":0,"stableSampleMilliseconds":0,"maximumWaitMilliseconds":1000,
          "requireAxisActivation":"yes","activationThreshold":0.05
        }
    })");
    REQUIRE_FALSE(wrongType.IsOk());

    const auto invalidThreshold = ProfileLoader::ParseFromString(R"({
        "schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"},
        "readiness": {
          "minimumWarmupMilliseconds":0,"stableSampleMilliseconds":0,"maximumWaitMilliseconds":1000,
          "requireAxisActivation":true,"activationThreshold":0.0
        }
    })");
    REQUIRE_FALSE(invalidThreshold.IsOk());
}

TEST_CASE("ProfileLoader: omitting readiness entirely uses a conservative default, not zero", "[ProfileLoader]") {
    const auto result =
        ProfileLoader::ParseFromString(R"({"schemaVersion":1,"profileId":"x","match":{"backend":"DirectInput"}})");
    REQUIRE(result.IsOk());
    REQUIRE(result.profile->readiness.minimumWarmup > std::chrono::milliseconds{0});
}
