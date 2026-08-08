#include <catch2/catch_test_macros.hpp>

#include "rvwheel/dal/AxisSource.hpp"

using rvwheel::dal::AxisSource;
using rvwheel::dal::AxisSourceFromString;
using rvwheel::dal::kAllAxisSources;
using rvwheel::dal::ToString;

TEST_CASE("AxisSource: every enumerable value round-trips through ToString/FromString", "[AxisSource]") {
    for (const AxisSource source : kAllAxisSources) {
        const auto text = ToString(source);
        const auto parsed = AxisSourceFromString(text);
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == source);
    }
}

TEST_CASE("AxisSource: known tokens map to the documented DirectInput-style names", "[AxisSource]") {
    REQUIRE(ToString(AxisSource::X) == "X");
    REQUIRE(ToString(AxisSource::Y) == "Y");
    REQUIRE(ToString(AxisSource::Z) == "Z");
    REQUIRE(ToString(AxisSource::RotationX) == "Rx");
    REQUIRE(ToString(AxisSource::RotationY) == "Ry");
    REQUIRE(ToString(AxisSource::RotationZ) == "Rz");
    REQUIRE(ToString(AxisSource::Slider0) == "Slider0");
    REQUIRE(ToString(AxisSource::Slider1) == "Slider1");
}

TEST_CASE("AxisSource: an unrecognized token fails to parse rather than defaulting silently", "[AxisSource]") {
    REQUIRE_FALSE(AxisSourceFromString("bogus").has_value());
    REQUIRE_FALSE(AxisSourceFromString("x").has_value()); // Case-sensitive: lowercase must not alias "X".
    REQUIRE_FALSE(AxisSourceFromString("").has_value());
}

TEST_CASE("AxisSource: \"Unknown\" itself is not accepted as a parseable token", "[AxisSource]") {
    // A profile should never need to spell out "Unknown"; treating it as
    // parseable would let a typo silently become "no source" instead of
    // being rejected.
    REQUIRE_FALSE(AxisSourceFromString("Unknown").has_value());
}
