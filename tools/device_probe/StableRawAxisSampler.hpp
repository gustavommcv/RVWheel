#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "rvwheel/dal/ICalibratableWheelDevice.hpp"

namespace rvwheel::tools::probe {

struct StableRawAxisSamplerConfig {
    std::chrono::milliseconds minimumAcquisition{0};
    std::chrono::milliseconds stableWindow{500};
    std::size_t minimumSamples = 15;
    float relativeTolerance = 0.005f;
    std::size_t maximumSamples = 256;
};

enum class StableRawAxisStatus {
    Accepted,
    Stable,
    InsufficientAcquisition,
    InsufficientSamples,
    InsufficientWindow,
    Unstable,
    MissingAxis,
    DegenerateRange,
    NonMonotonicTimestamp,
    PollFailure,
    InvalidConfiguration,
};

struct StableRawAxisResult {
    StableRawAxisStatus status = StableRawAxisStatus::InsufficientSamples;
    rvwheel::dal::RawAxisSnapshot snapshot{};
    std::string diagnostic;

    [[nodiscard]] bool IsStable() const noexcept { return status == StableRawAxisStatus::Stable; }
};

enum class CalibrationCaptureState {
    WaitingForConfirmation,
    Capturing,
    TimedOut,
    Cancelled,
};

// Small deterministic state holder shared by the real polling loop and
// tests. Human preparation time is unlimited; the timeout starts only
// after Enter arms a capture.
class CalibrationCaptureGate {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    explicit CalibrationCaptureGate(std::chrono::milliseconds timeout) noexcept : timeout_(timeout) {}

    void Arm(TimePoint now) noexcept;
    void Cancel() noexcept { cancelled_ = true; }
    [[nodiscard]] CalibrationCaptureState StateAt(TimePoint now) const noexcept;

private:
    std::chrono::milliseconds timeout_;
    std::optional<TimePoint> armedAt_;
    bool cancelled_ = false;
};

// Hardware-independent time-series reducer used by guided calibration.
// It retains a bounded recent window, rejects incomplete sequences, and
// returns one median value per discovered axis only when the window is
// stable. Timestamps are supplied by the caller, so tests need no sleeps.
class StableRawAxisSampler {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    StableRawAxisSampler(std::vector<rvwheel::dal::RawAxisInfo> axes, StableRawAxisSamplerConfig config = {});

    [[nodiscard]] StableRawAxisStatus AddSample(TimePoint timestamp,
                                                 const rvwheel::dal::RawAxisSnapshot& snapshot);
    void NotifyPollFailure();
    void Reset() noexcept;

    [[nodiscard]] StableRawAxisResult Evaluate() const;
    [[nodiscard]] std::size_t SampleCount() const noexcept { return samples_.size(); }

private:
    struct TimedSnapshot {
        TimePoint timestamp;
        rvwheel::dal::RawAxisSnapshot snapshot;
    };

    [[nodiscard]] StableRawAxisResult Result(StableRawAxisStatus status, std::string diagnostic) const;
    [[nodiscard]] bool IsConfigurationValid() const noexcept;
    [[nodiscard]] bool ContainsEveryAxisExactlyOnce(const rvwheel::dal::RawAxisSnapshot& snapshot) const noexcept;
    void PruneOldSamples();

    std::vector<rvwheel::dal::RawAxisInfo> axes_;
    StableRawAxisSamplerConfig config_;
    std::deque<TimedSnapshot> samples_;
    std::optional<TimePoint> acquisitionStart_;
    StableRawAxisStatus lastInputError_ = StableRawAxisStatus::InsufficientSamples;
    bool hasInputError_ = false;
};

} // namespace rvwheel::tools::probe
