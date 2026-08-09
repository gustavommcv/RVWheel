#include <catch2/catch_test_macros.hpp>

#include "rvwheel/dal/ForceFeedbackCooperativeLevel.hpp"
#include "rvwheel/devices/DirectInputCooperativeLevel.hpp"

using rvwheel::dal::ForceFeedbackCooperativeLevel;
using rvwheel::devices::SelectDirectInputCooperativeFlags;

TEST_CASE("DirectInput cooperative level preserves shared background input by default",
          "[DirectInput][CooperativeLevel]") {
    REQUIRE(SelectDirectInputCooperativeFlags(true, false, ForceFeedbackCooperativeLevel::Foreground) ==
            (DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
    REQUIRE(SelectDirectInputCooperativeFlags(false, true, ForceFeedbackCooperativeLevel::Foreground) ==
            (DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
}

TEST_CASE("DirectInput cooperative level selects exclusive background only for an explicit FFB owner",
          "[DirectInput][CooperativeLevel]") {
    REQUIRE(SelectDirectInputCooperativeFlags(true, true, ForceFeedbackCooperativeLevel::Background) ==
            (DISCL_EXCLUSIVE | DISCL_BACKGROUND));
}

TEST_CASE("DirectInput cooperative level selects exclusive foreground only for an explicit FFB owner",
          "[DirectInput][CooperativeLevel]") {
    REQUIRE(SelectDirectInputCooperativeFlags(true, true, ForceFeedbackCooperativeLevel::Foreground) ==
            (DISCL_EXCLUSIVE | DISCL_FOREGROUND));
}
