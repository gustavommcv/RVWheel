#include <catch2/catch_test_macros.hpp>

#include "rvwheel/ffb/ForceFeedbackMixer.hpp"

using rvwheel::dal::ForceFeedbackCommand;
using rvwheel::ffb::ForceFeedbackMixer;

TEST_CASE("ForceFeedbackMixer: mixing zero contributions yields an all-zero command", "[FFB][Mixer]") {
    const ForceFeedbackMixer mixer;
    const auto mixed = mixer.Mix({});
    REQUIRE(mixed.constantForce == 0.0f);
    REQUIRE(mixed.spring == 0.0f);
    REQUIRE(mixed.damper == 0.0f);
}

TEST_CASE("ForceFeedbackMixer: constant force contributions are summed", "[FFB][Mixer]") {
    const ForceFeedbackMixer mixer;
    const auto mixed = mixer.Mix({
        ForceFeedbackCommand{0.2f, 0.0f, 0.0f, 1.0f},
        ForceFeedbackCommand{0.3f, 0.0f, 0.0f, 1.0f},
    });
    REQUIRE(mixed.constantForce > 0.49f);
    REQUIRE(mixed.constantForce < 0.51f);
}

TEST_CASE("ForceFeedbackMixer: summed constant force is clamped to [-1, 1]", "[FFB][Mixer]") {
    const ForceFeedbackMixer mixer;
    const auto mixed = mixer.Mix({
        ForceFeedbackCommand{0.9f, 0.0f, 0.0f, 1.0f},
        ForceFeedbackCommand{0.9f, 0.0f, 0.0f, 1.0f},
    });
    REQUIRE(mixed.constantForce <= 1.0f);
}

TEST_CASE("ForceFeedbackMixer: spring and damper take the strongest single request, not the sum", "[FFB][Mixer]") {
    const ForceFeedbackMixer mixer;
    const auto mixed = mixer.Mix({
        ForceFeedbackCommand{0.0f, 0.3f, 0.4f, 1.0f},
        ForceFeedbackCommand{0.0f, 0.7f, 0.2f, 1.0f},
    });
    REQUIRE(mixed.spring == 0.7f);
    REQUIRE(mixed.damper == 0.4f);
}

TEST_CASE("ForceFeedbackMixer: gain takes the most conservative (minimum) request", "[FFB][Mixer]") {
    const ForceFeedbackMixer mixer;
    const auto mixed = mixer.Mix({
        ForceFeedbackCommand{0.0f, 0.0f, 0.0f, 1.0f},
        ForceFeedbackCommand{0.0f, 0.0f, 0.0f, 0.3f},
    });
    REQUIRE(mixed.gain == 0.3f);
}

TEST_CASE("ForceFeedbackMixer: a single source's contribution passes through clamped but otherwise unchanged",
          "[FFB][Mixer]") {
    const ForceFeedbackMixer mixer;
    const auto mixed = mixer.Mix({ForceFeedbackCommand{-0.4f, 0.5f, 0.6f, 0.8f}});
    REQUIRE(mixed.constantForce == -0.4f);
    REQUIRE(mixed.spring == 0.5f);
    REQUIRE(mixed.damper == 0.6f);
    REQUIRE(mixed.gain == 0.8f);
}
