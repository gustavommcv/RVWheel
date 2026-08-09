#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

#include "CalibrationWizard.hpp"
#include "StableRawAxisSampler.hpp"

using namespace std::chrono_literals;
using rvwheel::dal::AxisSource;
using rvwheel::dal::RawAxisInfo;
using rvwheel::dal::RawAxisSample;
using rvwheel::dal::RawAxisSnapshot;
using rvwheel::tools::probe::CalibrationCaptureGate;
using rvwheel::tools::probe::CalibrationCaptureState;
using rvwheel::tools::probe::StableRawAxisSampler;
using rvwheel::tools::probe::StableRawAxisSamplerConfig;
using rvwheel::tools::probe::StableRawAxisStatus;

namespace {

using TimePoint = StableRawAxisSampler::TimePoint;

TimePoint At(std::int64_t milliseconds) {
    return TimePoint{} + std::chrono::milliseconds{milliseconds};
}

RawAxisInfo Axis(AxisSource source, std::int32_t rawMin = 0, std::int32_t rawMax = 65535) {
    RawAxisInfo result{};
    result.source = source;
    result.rawMin = rawMin;
    result.rawMax = rawMax;
    return result;
}

RawAxisSnapshot Snapshot(std::initializer_list<std::pair<AxisSource, std::int32_t>> values) {
    RawAxisSnapshot result{};
    for (const auto& [source, value] : values) {
        result.samples[result.count] = RawAxisSample{source, value};
        ++result.count;
    }
    return result;
}

std::int32_t ValueOf(const RawAxisSnapshot& snapshot, AxisSource source) {
    for (std::uint8_t i = 0; i < snapshot.count; ++i) {
        if (snapshot.samples[i].source == source) {
            return snapshot.samples[i].rawValue;
        }
    }
    throw std::logic_error("Expected axis was absent from the aggregated snapshot");
}

StableRawAxisSamplerConfig TestConfig(std::chrono::milliseconds stableWindow = 500ms,
                                      std::size_t minimumSamples = 5) {
    StableRawAxisSamplerConfig config;
    config.stableWindow = stableWindow;
    config.minimumSamples = minimumSamples;
    config.relativeTolerance = 0.005f;
    config.maximumSamples = 128;
    return config;
}

} // namespace

TEST_CASE("StableRawAxisSampler requires enough samples and a full time window", "[DeviceProbe][StableSampler]") {
    auto config = TestConfig(500ms, 5);
    StableRawAxisSampler sampler({Axis(AxisSource::X)}, config);

    for (std::int64_t time = 0; time <= 300; time += 100) {
        REQUIRE(sampler.AddSample(At(time), Snapshot({{AxisSource::X, 1000}})) == StableRawAxisStatus::Accepted);
    }
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::InsufficientSamples);

    REQUIRE(sampler.AddSample(At(400), Snapshot({{AxisSource::X, 1000}})) == StableRawAxisStatus::Accepted);
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::InsufficientWindow);

    REQUIRE(sampler.AddSample(At(500), Snapshot({{AxisSource::X, 1000}})) == StableRawAxisStatus::Accepted);
    REQUIRE(sampler.Evaluate().IsStable());
}

TEST_CASE("StableRawAxisSampler enforces minimum acquisition independently from its retained window",
          "[DeviceProbe][StableSampler]") {
    auto config = TestConfig(200ms, 3);
    config.minimumAcquisition = 1000ms;
    StableRawAxisSampler sampler({Axis(AxisSource::X)}, config);

    for (std::int64_t time = 0; time <= 900; time += 100) {
        (void)sampler.AddSample(At(time), Snapshot({{AxisSource::X, 1234}}));
    }
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::InsufficientAcquisition);

    (void)sampler.AddSample(At(1000), Snapshot({{AxisSource::X, 1234}}));
    REQUIRE(sampler.Evaluate().IsStable());
}

TEST_CASE("StableRawAxisSampler uses a robust median and ignores one isolated spike", "[DeviceProbe][StableSampler]") {
    auto config = TestConfig(1000ms, 11);
    StableRawAxisSampler sampler({Axis(AxisSource::X)}, config);

    for (std::int64_t i = 0; i <= 10; ++i) {
        const std::int32_t value = i == 5 ? 60000 : 1000;
        (void)sampler.AddSample(At(i * 100), Snapshot({{AxisSource::X, value}}));
    }

    const auto result = sampler.Evaluate();
    REQUIRE(result.IsStable());
    REQUIRE(ValueOf(result.snapshot, AxisSource::X) == 1000);
}

TEST_CASE("StableRawAxisSampler rejects sustained jitter outside the relative tolerance", "[DeviceProbe][StableSampler]") {
    auto config = TestConfig(500ms, 6);
    config.relativeTolerance = 0.01f;
    StableRawAxisSampler sampler({Axis(AxisSource::X)}, config);

    for (std::int64_t i = 0; i <= 5; ++i) {
        const std::int32_t value = (i % 2) == 0 ? 10000 : 12000;
        (void)sampler.AddSample(At(i * 100), Snapshot({{AxisSource::X, value}}));
    }
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::Unstable);
}

TEST_CASE("StableRawAxisSampler resets on a missing axis or backwards timestamp", "[DeviceProbe][StableSampler]") {
    auto config = TestConfig(100ms, 2);
    StableRawAxisSampler sampler({Axis(AxisSource::X), Axis(AxisSource::Y)}, config);

    (void)sampler.AddSample(At(0), Snapshot({{AxisSource::X, 1}, {AxisSource::Y, 2}}));
    REQUIRE(sampler.AddSample(At(50), Snapshot({{AxisSource::X, 1}})) == StableRawAxisStatus::MissingAxis);
    REQUIRE(sampler.SampleCount() == 0);
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::MissingAxis);

    (void)sampler.AddSample(At(100), Snapshot({{AxisSource::X, 1}, {AxisSource::Y, 2}}));
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::InsufficientSamples);
    REQUIRE(sampler.AddSample(At(99), Snapshot({{AxisSource::X, 1}, {AxisSource::Y, 2}})) ==
            StableRawAxisStatus::NonMonotonicTimestamp);
    REQUIRE(sampler.SampleCount() == 0);
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::NonMonotonicTimestamp);
}

TEST_CASE("StableRawAxisSampler rejects degenerate ranges and excludes failed polls", "[DeviceProbe][StableSampler]") {
    StableRawAxisSampler invalid({Axis(AxisSource::X, 7, 7)}, TestConfig(100ms, 2));
    REQUIRE(invalid.Evaluate().status == StableRawAxisStatus::DegenerateRange);

    StableRawAxisSampler sampler({Axis(AxisSource::X)}, TestConfig(100ms, 2));
    (void)sampler.AddSample(At(0), Snapshot({{AxisSource::X, 42}}));
    sampler.NotifyPollFailure();
    REQUIRE(sampler.SampleCount() == 0);
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::PollFailure);
    (void)sampler.AddSample(At(100), Snapshot({{AxisSource::X, 42}}));
    REQUIRE(sampler.Evaluate().status == StableRawAxisStatus::InsufficientSamples);
}

TEST_CASE("CalibrationCaptureGate times out only after confirmation and cancellation wins",
          "[DeviceProbe][StableSampler][CaptureGate]") {
    CalibrationCaptureGate gate(10s);
    REQUIRE(gate.StateAt(At(60000)) == CalibrationCaptureState::WaitingForConfirmation);

    gate.Arm(At(100));
    REQUIRE(gate.StateAt(At(100)) == CalibrationCaptureState::Capturing);
    REQUIRE(gate.StateAt(At(10099)) == CalibrationCaptureState::Capturing);
    REQUIRE(gate.StateAt(At(10100)) == CalibrationCaptureState::TimedOut);

    gate.Cancel();
    REQUIRE(gate.StateAt(At(10100)) == CalibrationCaptureState::Cancelled);
}

TEST_CASE("G923 startup settling is excluded before steering-axis inference",
          "[DeviceProbe][StableSampler][CalibrationWizard][Regression]") {
    using rvwheel::tools::probe::CalibrationStepKind;
    using rvwheel::tools::probe::CalibrationStepOutcome;
    using rvwheel::tools::probe::CalibrationWizard;

    const std::vector<RawAxisInfo> axes = {
        Axis(AxisSource::X), Axis(AxisSource::Y), Axis(AxisSource::RotationZ), Axis(AxisSource::Slider0)};
    auto warmupConfig = TestConfig(500ms, 5);
    warmupConfig.minimumAcquisition = 2500ms;
    StableRawAxisSampler warmup(axes, warmupConfig);

    for (std::int64_t time = 0; time <= 2000; time += 100) {
        (void)warmup.AddSample(At(time), Snapshot({{AxisSource::X, 32767},
                                                  {AxisSource::Y, 32767},
                                                  {AxisSource::RotationZ, 32767},
                                                  {AxisSource::Slider0, 32767}}));
    }
    for (std::int64_t time = 2100; time <= 2600; time += 100) {
        (void)warmup.AddSample(At(time), Snapshot({{AxisSource::X, 32767},
                                                  {AxisSource::Y, 65535},
                                                  {AxisSource::RotationZ, 65535},
                                                  {AxisSource::Slider0, 65535}}));
    }

    const auto settled = warmup.Evaluate();
    REQUIRE(settled.IsStable());
    REQUIRE(ValueOf(settled.snapshot, AxisSource::Y) == 65535);
    REQUIRE(ValueOf(settled.snapshot, AxisSource::RotationZ) == 65535);
    REQUIRE(ValueOf(settled.snapshot, AxisSource::Slider0) == 65535);

    CalibrationWizard wizard(axes);
    REQUIRE(wizard.SubmitSnapshot(settled.snapshot) == CalibrationStepOutcome::Recorded); // Baseline
    REQUIRE(wizard.SubmitSnapshot(settled.snapshot) == CalibrationStepOutcome::Recorded); // Center

    StableRawAxisSampler leftCapture(axes, TestConfig(500ms, 5));
    (void)leftCapture.AddSample(At(0), Snapshot({{AxisSource::X, 32767},
                                                {AxisSource::Y, 65535},
                                                {AxisSource::RotationZ, 65535},
                                                {AxisSource::Slider0, 65535}}));
    (void)leftCapture.AddSample(At(100), Snapshot({{AxisSource::X, 10000},
                                                  {AxisSource::Y, 65535},
                                                  {AxisSource::RotationZ, 65535},
                                                  {AxisSource::Slider0, 65535}}));
    for (std::int64_t time = 200; time <= 700; time += 100) {
        (void)leftCapture.AddSample(At(time), Snapshot({{AxisSource::X, 0},
                                                       {AxisSource::Y, 65535},
                                                       {AxisSource::RotationZ, 65535},
                                                       {AxisSource::Slider0, 65535}}));
    }

    const auto left = leftCapture.Evaluate();
    REQUIRE(left.IsStable());
    REQUIRE(wizard.SubmitSnapshot(left.snapshot) == CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.CurrentStep() == CalibrationStepKind::SteeringRight);
}

TEST_CASE("Stable captures still report genuinely simultaneous two-axis movement as ambiguous",
          "[DeviceProbe][StableSampler][CalibrationWizard]") {
    using rvwheel::tools::probe::CalibrationStepOutcome;
    using rvwheel::tools::probe::CalibrationWizard;

    const std::vector<RawAxisInfo> axes = {Axis(AxisSource::X), Axis(AxisSource::Y)};
    CalibrationWizard wizard(axes);
    const auto center = Snapshot({{AxisSource::X, 32767}, {AxisSource::Y, 32767}});
    REQUIRE(wizard.SubmitSnapshot(center) == CalibrationStepOutcome::Recorded);
    REQUIRE(wizard.SubmitSnapshot(center) == CalibrationStepOutcome::Recorded);

    StableRawAxisSampler capture(axes, TestConfig(500ms, 5));
    for (std::int64_t time = 0; time <= 500; time += 100) {
        (void)capture.AddSample(At(time), Snapshot({{AxisSource::X, 0}, {AxisSource::Y, 65535}}));
    }
    const auto moved = capture.Evaluate();
    REQUIRE(moved.IsStable());
    REQUIRE(wizard.SubmitSnapshot(moved.snapshot) == CalibrationStepOutcome::Ambiguous);
}
