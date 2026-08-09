#include <catch2/catch_test_macros.hpp>

#include "CalibrationWizard.hpp"

using rvwheel::dal::AxisDirection;
using rvwheel::dal::AxisSource;
using rvwheel::dal::RawAxisInfo;
using rvwheel::dal::RawAxisSnapshot;

namespace {

RawAxisInfo MakeAxisInfo(AxisSource source, std::int32_t rawMin = 0, std::int32_t rawMax = 65535) {
    RawAxisInfo info{};
    info.source = source;
    info.rawMin = rawMin;
    info.rawMax = rawMax;
    return info;
}

RawAxisSnapshot MakeSnapshot(std::initializer_list<std::pair<AxisSource, std::int32_t>> values) {
    RawAxisSnapshot snapshot{};
    for (const auto& [source, value] : values) {
        snapshot.samples[snapshot.count] = rvwheel::dal::RawAxisSample{source, value};
        ++snapshot.count;
    }
    return snapshot;
}

std::vector<RawAxisInfo> G923LikeAxes() {
    return {
        MakeAxisInfo(AxisSource::X),
        MakeAxisInfo(AxisSource::Y),
        MakeAxisInfo(AxisSource::RotationZ),
        MakeAxisInfo(AxisSource::Slider0),
    };
}

} // namespace

TEST_CASE("CalibrationWizard: a full G923-like session reproduces the verified profile bindings",
          "[DeviceProbe][CalibrationWizard][G923]") {
    using namespace rvwheel::tools::probe;

    CalibrationWizard wizard(G923LikeAxes());

    // Baseline: everything at rest. Per the G923's actual wiring, released
    // pedals sit near the raw maximum (this is exactly why the profile
    // marks them "inverted").
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringCenter);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringLeft);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 0}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringRight);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 65535}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::ThrottleReleased);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::ThrottlePressed);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 65535}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::BrakeReleased);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 65535}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::BrakePressed);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 0}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::ClutchReleased);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 0}, {AxisSource::Slider0, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::ClutchPressed);

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 0}, {AxisSource::Slider0, 0}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::Summary);

    const CalibrationResult result = wizard.Finish();
    REQUIRE(result.success);

    REQUIRE(result.layout.steering.has_value());
    REQUIRE(result.layout.steering->source == AxisSource::X);
    REQUIRE(result.layout.steering->direction == AxisDirection::Normal);

    REQUIRE(result.layout.throttle.has_value());
    REQUIRE(result.layout.throttle->source == AxisSource::Y);
    REQUIRE(result.layout.throttle->direction == AxisDirection::Inverted);

    REQUIRE(result.layout.brake.has_value());
    REQUIRE(result.layout.brake->source == AxisSource::RotationZ);
    REQUIRE(result.layout.brake->direction == AxisDirection::Inverted);

    REQUIRE(result.layout.clutch.has_value());
    REQUIRE(result.layout.clutch->source == AxisSource::Slider0);
    REQUIRE(result.layout.clutch->direction == AxisDirection::Inverted);
}

TEST_CASE("CalibrationWizard: ignores small noise on other axes while choosing the one that actually moved",
          "[DeviceProbe][CalibrationWizard]") {
    using namespace rvwheel::tools::probe;
    CalibrationWizard wizard(G923LikeAxes());

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}})) ==
            CalibrationStepOutcome::Recorded); // Baseline
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}})) ==
            CalibrationStepOutcome::Recorded); // SteeringCenter

    // X moves fully left; Y jitters by a tiny amount well below the
    // default 5% threshold (65535 * 0.001 ~= 65 raw units of noise).
    const auto outcome =
        wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 0}, {AxisSource::Y, 65535 - 50}}));
    REQUIRE(outcome == CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringRight);
}

TEST_CASE("CalibrationWizard: refreshes a late-changing startup baseline before accepting steering center",
          "[DeviceProbe][CalibrationWizard][Startup]") {
    using namespace rvwheel::tools::probe;
    CalibrationWizard wizard(G923LikeAxes());

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767},
                                                {AxisSource::Y, 32767},
                                                {AxisSource::RotationZ, 32767},
                                                {AxisSource::Slider0, 32767}})) == CalibrationStepOutcome::Recorded);

    const auto settled = MakeSnapshot({{AxisSource::X, 32767},
                                       {AxisSource::Y, 65535},
                                       {AxisSource::RotationZ, 65535},
                                       {AxisSource::Slider0, 65535}});
    REQUIRE(wizard.SubmitSnapshot(settled) == CalibrationStepOutcome::BaselineChanged);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringCenter);

    REQUIRE(wizard.SubmitSnapshot(settled) == CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringLeft);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 0},
                                                {AxisSource::Y, 65535},
                                                {AxisSource::RotationZ, 65535},
                                                {AxisSource::Slider0, 65535}})) == CalibrationStepOutcome::Recorded);
}

TEST_CASE("CalibrationWizard: rejects a step where more than one axis moves above threshold", "[DeviceProbe][CalibrationWizard]") {
    using namespace rvwheel::tools::probe;
    CalibrationWizard wizard(G923LikeAxes());

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}})) ==
            CalibrationStepOutcome::Recorded);

    // Both X and Y move by a large amount: genuinely ambiguous.
    const auto outcome = wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 0}, {AxisSource::Y, 0}}));
    REQUIRE(outcome == CalibrationStepOutcome::Ambiguous);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringLeft); // Did not advance.

    // Retrying with only X moving succeeds.
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 0}, {AxisSource::Y, 65535}})) == CalibrationStepOutcome::Recorded);
}

TEST_CASE("CalibrationWizard: rejects a step where nothing moves", "[DeviceProbe][CalibrationWizard]") {
    using namespace rvwheel::tools::probe;
    CalibrationWizard wizard(G923LikeAxes());

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}})) == CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}})) == CalibrationStepOutcome::Recorded);

    const auto outcome = wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}}));
    REQUIRE(outcome == CalibrationStepOutcome::NoMovement);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringLeft);
}

TEST_CASE("CalibrationWizard: detects a different axis moving between Left and Right as Inconsistent", "[DeviceProbe][CalibrationWizard]") {
    using namespace rvwheel::tools::probe;
    CalibrationWizard wizard(G923LikeAxes());

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 0}, {AxisSource::Y, 65535}})) == CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringRight);

    // A different axis (Y) moves this time instead of X.
    const auto outcome = wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}}));
    REQUIRE(outcome == CalibrationStepOutcome::Inconsistent);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringRight); // Did not advance.
}

TEST_CASE("CalibrationWizard: SkipClutch bypasses both clutch steps and the layout omits clutch", "[DeviceProbe][CalibrationWizard]") {
    using namespace rvwheel::tools::probe;
    CalibrationWizard wizard(G923LikeAxes());

    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 0}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 65535}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 65535}, {AxisSource::RotationZ, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 65535}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(MakeSnapshot({{AxisSource::X, 32767}, {AxisSource::Y, 0}, {AxisSource::RotationZ, 0}})) ==
            CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::ClutchReleased);

    wizard.SkipClutch();
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::Summary);

    const CalibrationResult result = wizard.Finish();
    REQUIRE(result.success);
    REQUIRE_FALSE(result.layout.clutch.has_value());
    REQUIRE(result.layout.throttle.has_value());
    REQUIRE(result.layout.brake.has_value());
}

TEST_CASE("CalibrationWizard: Finish before Summary fails with a clear reason", "[DeviceProbe][CalibrationWizard]") {
    using namespace rvwheel::tools::probe;
    CalibrationWizard wizard(G923LikeAxes());

    const CalibrationResult result = wizard.Finish();
    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.failureReason.empty());
}
