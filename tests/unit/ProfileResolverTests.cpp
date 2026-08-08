#include <catch2/catch_test_macros.hpp>

#include "rvwheel/profiles/ProfileResolver.hpp"

using rvwheel::dal::AxisBinding;
using rvwheel::dal::AxisDirection;
using rvwheel::dal::AxisSource;
using rvwheel::dal::DeviceBackend;
using rvwheel::dal::DeviceInfo;
using rvwheel::profiles::DeviceProfile;
using rvwheel::profiles::ProfileOrigin;
using rvwheel::profiles::ProfileResolver;
using rvwheel::profiles::ProfileWithOrigin;

namespace {

DeviceInfo MakeDevice(DeviceBackend backend, std::optional<std::uint16_t> vid, std::optional<std::uint16_t> pid) {
    DeviceInfo info{};
    info.backend = backend;
    info.vendorId = vid;
    info.productId = pid;
    return info;
}

DeviceProfile MakeExactProfile(std::string id, std::uint16_t vid, std::uint16_t pid, AxisSource steeringSource = AxisSource::X) {
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = std::move(id);
    profile.match.backend = DeviceBackend::DirectInput;
    profile.match.vendorId = vid;
    profile.match.productId = pid;
    profile.layout.steering = AxisBinding{steeringSource, AxisDirection::Normal, std::nullopt};
    return profile;
}

DeviceProfile MakeGenericProfile(std::string id, AxisSource steeringSource = AxisSource::X) {
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = std::move(id);
    profile.match.backend = DeviceBackend::DirectInput;
    profile.layout.steering = AxisBinding{steeringSource, AxisDirection::Normal, std::nullopt};
    return profile;
}

} // namespace

TEST_CASE("ProfileResolver: exact backend+VID+PID match resolves to BuiltInProfile", "[ProfileResolver]") {
    const std::vector<ProfileWithOrigin> profiles{{MakeExactProfile("g923", 0x046D, 0xC266), false}};
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, 0x046D, 0xC266);

    const auto resolution = ProfileResolver::Resolve(profiles, device, {AxisSource::X, AxisSource::Y});
    REQUIRE(resolution.origin == ProfileOrigin::BuiltInProfile);
    REQUIRE(resolution.profile.has_value());
    REQUIRE(resolution.profile->profileId == "g923");
    REQUIRE(resolution.layout.steering.has_value());
}

TEST_CASE("ProfileResolver: a user-origin exact match resolves to UserProfile", "[ProfileResolver]") {
    const std::vector<ProfileWithOrigin> profiles{{MakeExactProfile("g923", 0x046D, 0xC266), true}};
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, 0x046D, 0xC266);

    const auto resolution = ProfileResolver::Resolve(profiles, device, {AxisSource::X});
    REQUIRE(resolution.origin == ProfileOrigin::UserProfile);
}

TEST_CASE("ProfileResolver: two profiles tied at the same exact VID/PID are AmbiguousMatch, never a random pick",
          "[ProfileResolver]") {
    const std::vector<ProfileWithOrigin> profiles{
        {MakeExactProfile("profile-a", 0x046D, 0xC266), false},
        {MakeExactProfile("profile-b", 0x046D, 0xC266), true},
    };
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, 0x046D, 0xC266);

    const auto resolution = ProfileResolver::Resolve(profiles, device, {AxisSource::X});
    REQUIRE(resolution.origin == ProfileOrigin::AmbiguousMatch);
    REQUIRE_FALSE(resolution.profile.has_value());
    REQUIRE(resolution.layout.IsEmpty()); // Never applies either candidate's layout.
}

TEST_CASE("ProfileResolver: an exact match referencing a source the device lacks is InvalidExactMatch, never silently downgraded",
          "[ProfileResolver]") {
    const std::vector<ProfileWithOrigin> profiles{{MakeExactProfile("g923", 0x046D, 0xC266, AxisSource::Slider1), false}};
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, 0x046D, 0xC266);

    // Device only has X and Y; the profile references Slider1.
    const auto resolution = ProfileResolver::Resolve(profiles, device, {AxisSource::X, AxisSource::Y});
    REQUIRE(resolution.origin == ProfileOrigin::InvalidExactMatch);
    REQUIRE(resolution.layout.IsEmpty());
}

TEST_CASE("ProfileResolver: a device with no VID/PID can still match a generic backend profile", "[ProfileResolver]") {
    const std::vector<ProfileWithOrigin> profiles{{MakeGenericProfile("generic-directinput"), false}};
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, std::nullopt, std::nullopt);

    const auto resolution = ProfileResolver::Resolve(profiles, device, {AxisSource::X});
    REQUIRE(resolution.origin == ProfileOrigin::BuiltInProfile);
    REQUIRE(resolution.profile->profileId == "generic-directinput");
}

TEST_CASE("ProfileResolver: an exact-match profile never applies to a device with no VID/PID", "[ProfileResolver]") {
    const std::vector<ProfileWithOrigin> profiles{{MakeExactProfile("g923", 0x046D, 0xC266), false}};
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, std::nullopt, std::nullopt);

    const auto resolution = ProfileResolver::Resolve(profiles, device, {AxisSource::X});
    // No candidate at all (the exact-match profile requires VID/PID this
    // device does not report), so this falls through to the generic
    // fallback heuristic.
    REQUIRE(resolution.origin == ProfileOrigin::ProvisionalFallback);
}

TEST_CASE("ProfileResolver: a generic profile referencing a missing source falls through to the fallback heuristic, "
          "not a hard failure",
          "[ProfileResolver]") {
    const std::vector<ProfileWithOrigin> profiles{{MakeGenericProfile("generic-directinput", AxisSource::Slider1), false}};
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, std::nullopt, std::nullopt);

    const auto resolution = ProfileResolver::Resolve(profiles, device, {AxisSource::X});
    REQUIRE(resolution.origin == ProfileOrigin::ProvisionalFallback);
    REQUIRE(resolution.layout.steering.has_value());
    REQUIRE(resolution.layout.steering->source == AxisSource::X);
}

TEST_CASE("ProfileResolver: no profiles at all but a recognizable axis produces ProvisionalFallback", "[ProfileResolver]") {
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, 0x1234, 0x5678);
    const auto resolution = ProfileResolver::Resolve({}, device, {AxisSource::X, AxisSource::Y, AxisSource::RotationZ, AxisSource::Slider0});
    REQUIRE(resolution.origin == ProfileOrigin::ProvisionalFallback);
    REQUIRE(resolution.layout.steering.has_value());
    REQUIRE(resolution.layout.throttle.has_value());
    REQUIRE(resolution.layout.brake.has_value());
    REQUIRE(resolution.layout.clutch.has_value());
}

TEST_CASE("ProfileResolver: no profiles and no recognizable axis is Unconfigured, never a silent guess", "[ProfileResolver]") {
    const DeviceInfo device = MakeDevice(DeviceBackend::DirectInput, 0x1234, 0x5678);
    const auto resolution = ProfileResolver::Resolve({}, device, {AxisSource::RotationY}); // Not one the fallback recognizes.
    REQUIRE(resolution.origin == ProfileOrigin::Unconfigured);
    REQUIRE(resolution.layout.IsEmpty());
}

TEST_CASE("ProfileResolver::GenericFallbackLayout only binds roles for axes actually present", "[ProfileResolver]") {
    const auto layout = ProfileResolver::GenericFallbackLayout({AxisSource::X, AxisSource::RotationZ});
    REQUIRE(layout.steering.has_value());
    REQUIRE_FALSE(layout.throttle.has_value()); // Y not present.
    REQUIRE(layout.brake.has_value());
    REQUIRE_FALSE(layout.clutch.has_value()); // Slider0 not present.
}
