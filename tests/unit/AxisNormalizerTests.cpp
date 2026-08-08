#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

#include "rvwheel/dal/AxisNormalizer.hpp"

using rvwheel::dal::AxisCalibration;
using rvwheel::dal::AxisNormalizer;
using rvwheel::dal::PedalCalibration;
using rvwheel::dal::StatusCode;

TEST_CASE("Steering: min/center/max on a symmetric signed range", "[AxisNormalizer][Steering]") {
    const AxisCalibration cal{-32768, 0, 32767};

    const auto atMin = AxisNormalizer::NormalizeSteering(-32768, cal);
    REQUIRE(atMin.status.IsOk());
    REQUIRE(atMin.value == Catch::Approx(-1.0f));

    const auto atCenter = AxisNormalizer::NormalizeSteering(0, cal);
    REQUIRE(atCenter.status.IsOk());
    REQUIRE(atCenter.value == Catch::Approx(0.0f));

    const auto atMax = AxisNormalizer::NormalizeSteering(32767, cal);
    REQUIRE(atMax.status.IsOk());
    REQUIRE(atMax.value == Catch::Approx(1.0f));
}

TEST_CASE("Steering: common DirectInput 0..65535 range with a coherent center", "[AxisNormalizer][Steering]") {
    const AxisCalibration cal{0, 32767, 65535};

    REQUIRE(AxisNormalizer::NormalizeSteering(0, cal).value == Catch::Approx(-1.0f));
    REQUIRE(AxisNormalizer::NormalizeSteering(32767, cal).value == Catch::Approx(0.0f));
    REQUIRE(AxisNormalizer::NormalizeSteering(65535, cal).value == Catch::Approx(1.0f));
}

TEST_CASE("Steering: signed -32768..32767 range intermediate values", "[AxisNormalizer][Steering]") {
    const AxisCalibration cal{-32768, 0, 32767};

    REQUIRE(AxisNormalizer::NormalizeSteering(-16384, cal).value == Catch::Approx(-0.5f).margin(0.001));
    REQUIRE(AxisNormalizer::NormalizeSteering(16383, cal).value == Catch::Approx(0.5f).margin(0.002));
}

TEST_CASE("Steering: asymmetric range (center far from the midpoint)", "[AxisNormalizer][Steering]") {
    const AxisCalibration cal{0, 1000, 65535};

    REQUIRE(AxisNormalizer::NormalizeSteering(1000, cal).value == Catch::Approx(0.0f));
    REQUIRE(AxisNormalizer::NormalizeSteering(0, cal).value == Catch::Approx(-1.0f));
    REQUIRE(AxisNormalizer::NormalizeSteering(65535, cal).value == Catch::Approx(1.0f));
    // Halfway between min (0) and center (1000): should be about -0.5,
    // using the min-side scale, not the (very different) max-side scale.
    REQUIRE(AxisNormalizer::NormalizeSteering(500, cal).value == Catch::Approx(-0.5f).margin(0.001));
    // Halfway between center (1000) and max (65535): should be about 0.5.
    REQUIRE(AxisNormalizer::NormalizeSteering(33267, cal).value == Catch::Approx(0.5f).margin(0.002));
}

TEST_CASE("Steering: clamps values beyond the calibrated range", "[AxisNormalizer][Steering][Clamp]") {
    const AxisCalibration cal{0, 32767, 65535};

    REQUIRE(AxisNormalizer::NormalizeSteering(-1000, cal).value == Catch::Approx(-1.0f));
    REQUIRE(AxisNormalizer::NormalizeSteering(70000, cal).value == Catch::Approx(1.0f));
}

TEST_CASE("Steering: extreme int32 inputs stay finite and clamped", "[AxisNormalizer][Steering][Clamp]") {
    const AxisCalibration cal{0, 32767, 65535};

    const auto lo = AxisNormalizer::NormalizeSteering(std::numeric_limits<std::int32_t>::min(), cal);
    REQUIRE(std::isfinite(lo.value));
    REQUIRE(lo.value == Catch::Approx(-1.0f));

    const auto hi = AxisNormalizer::NormalizeSteering(std::numeric_limits<std::int32_t>::max(), cal);
    REQUIRE(std::isfinite(hi.value));
    REQUIRE(hi.value == Catch::Approx(1.0f));
}

TEST_CASE("Steering: degenerate calibrations report InvalidArgument without NaN/Inf", "[AxisNormalizer][Steering][Degenerate]") {
    SECTION("min == center") {
        const AxisCalibration cal{100, 100, 65535};
        const auto result = AxisNormalizer::NormalizeSteering(50, cal);
        REQUIRE(result.status.Code() == StatusCode::InvalidArgument);
        REQUIRE(std::isfinite(result.value));
        REQUIRE(result.value == Catch::Approx(0.0f));
    }
    SECTION("center == max") {
        const AxisCalibration cal{0, 65535, 65535};
        const auto result = AxisNormalizer::NormalizeSteering(70000, cal);
        REQUIRE(result.status.Code() == StatusCode::InvalidArgument);
        REQUIRE(std::isfinite(result.value));
        REQUIRE(result.value == Catch::Approx(0.0f));
    }
    SECTION("min == center == max") {
        const AxisCalibration cal{100, 100, 100};
        const auto result = AxisNormalizer::NormalizeSteering(999, cal);
        REQUIRE(result.status.Code() == StatusCode::InvalidArgument);
        REQUIRE(std::isfinite(result.value));
        REQUIRE(result.value == Catch::Approx(0.0f));
    }
}

TEST_CASE("Pedal: released and fully pressed on a non-inverted range", "[AxisNormalizer][Pedal]") {
    const PedalCalibration cal{0, 65535};

    REQUIRE(AxisNormalizer::NormalizePedal(0, cal).value == Catch::Approx(0.0f));
    REQUIRE(AxisNormalizer::NormalizePedal(65535, cal).value == Catch::Approx(1.0f));
    REQUIRE(AxisNormalizer::NormalizePedal(32767, cal).value == Catch::Approx(0.5f).margin(0.001));
}

TEST_CASE("Pedal: inverted wiring (rawAtPressed < rawAtReleased)", "[AxisNormalizer][Pedal][Inverted]") {
    const PedalCalibration cal{65535, 0};

    REQUIRE(AxisNormalizer::NormalizePedal(65535, cal).value == Catch::Approx(0.0f));
    REQUIRE(AxisNormalizer::NormalizePedal(0, cal).value == Catch::Approx(1.0f));
    REQUIRE(AxisNormalizer::NormalizePedal(32767, cal).value == Catch::Approx(0.5f).margin(0.001));
}

TEST_CASE("Pedal: clamps beyond the calibrated range on both sides", "[AxisNormalizer][Pedal][Clamp]") {
    const PedalCalibration cal{1000, 60000};

    REQUIRE(AxisNormalizer::NormalizePedal(0, cal).value == Catch::Approx(0.0f));
    REQUIRE(AxisNormalizer::NormalizePedal(65535, cal).value == Catch::Approx(1.0f));

    const PedalCalibration invertedCal{60000, 1000};
    REQUIRE(AxisNormalizer::NormalizePedal(65535, invertedCal).value == Catch::Approx(0.0f));
    REQUIRE(AxisNormalizer::NormalizePedal(0, invertedCal).value == Catch::Approx(1.0f));
}

TEST_CASE("Pedal: degenerate calibration reports InvalidArgument without NaN/Inf", "[AxisNormalizer][Pedal][Degenerate]") {
    const PedalCalibration cal{32767, 32767};
    const auto result = AxisNormalizer::NormalizePedal(32767, cal);
    REQUIRE(result.status.Code() == StatusCode::InvalidArgument);
    REQUIRE(std::isfinite(result.value));
    REQUIRE(result.value == Catch::Approx(0.0f));
}

TEST_CASE("Pedal: extreme int32 inputs stay finite and clamped", "[AxisNormalizer][Pedal][Clamp]") {
    const PedalCalibration cal{0, 65535};

    const auto lo = AxisNormalizer::NormalizePedal(std::numeric_limits<std::int32_t>::min(), cal);
    REQUIRE(std::isfinite(lo.value));
    REQUIRE(lo.value == Catch::Approx(0.0f));

    const auto hi = AxisNormalizer::NormalizePedal(std::numeric_limits<std::int32_t>::max(), cal);
    REQUIRE(std::isfinite(hi.value));
    REQUIRE(hi.value == Catch::Approx(1.0f));
}
