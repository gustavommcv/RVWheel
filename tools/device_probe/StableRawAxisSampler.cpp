#include "StableRawAxisSampler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace rvwheel::tools::probe {

namespace {

[[nodiscard]] const rvwheel::dal::RawAxisSample* FindSample(const rvwheel::dal::RawAxisSnapshot& snapshot,
                                                             rvwheel::dal::AxisSource source) noexcept {
    for (std::uint8_t i = 0; i < snapshot.count; ++i) {
        if (snapshot.samples[i].source == source) {
            return &snapshot.samples[i];
        }
    }
    return nullptr;
}

[[nodiscard]] std::int32_t Median(std::vector<std::int32_t>& values) {
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if ((values.size() % 2U) != 0U) {
        return values[middle];
    }
    const std::int64_t sum = static_cast<std::int64_t>(values[middle - 1]) + static_cast<std::int64_t>(values[middle]);
    return static_cast<std::int32_t>(sum / 2);
}

} // namespace

void CalibrationCaptureGate::Arm(TimePoint now) noexcept {
    if (!cancelled_ && !armedAt_) {
        armedAt_ = now;
    }
}

CalibrationCaptureState CalibrationCaptureGate::StateAt(TimePoint now) const noexcept {
    if (cancelled_) {
        return CalibrationCaptureState::Cancelled;
    }
    if (!armedAt_) {
        return CalibrationCaptureState::WaitingForConfirmation;
    }
    if (timeout_.count() >= 0 && now - *armedAt_ >= timeout_) {
        return CalibrationCaptureState::TimedOut;
    }
    return CalibrationCaptureState::Capturing;
}

StableRawAxisSampler::StableRawAxisSampler(std::vector<rvwheel::dal::RawAxisInfo> axes,
                                           StableRawAxisSamplerConfig config)
    : axes_(std::move(axes)), config_(config) {}

bool StableRawAxisSampler::IsConfigurationValid() const noexcept {
    if (axes_.empty() || axes_.size() > rvwheel::dal::kMaxRawAxes || config_.minimumAcquisition.count() < 0 ||
        config_.stableWindow.count() < 0 || config_.minimumSamples == 0 ||
        config_.maximumSamples < config_.minimumSamples || !std::isfinite(config_.relativeTolerance) ||
        config_.relativeTolerance < 0.0f || config_.relativeTolerance > 1.0f) {
        return false;
    }
    for (const auto& axis : axes_) {
        if (axis.rawMax <= axis.rawMin) {
            return false;
        }
    }
    return true;
}

bool StableRawAxisSampler::ContainsEveryAxisExactlyOnce(const rvwheel::dal::RawAxisSnapshot& snapshot) const noexcept {
    if (snapshot.count > rvwheel::dal::kMaxRawAxes) {
        return false;
    }
    for (const auto& axis : axes_) {
        std::size_t matches = 0;
        for (std::uint8_t i = 0; i < snapshot.count; ++i) {
            if (snapshot.samples[i].source == axis.source) {
                ++matches;
            }
        }
        if (matches != 1) {
            return false;
        }
    }
    return snapshot.count == axes_.size();
}

StableRawAxisStatus StableRawAxisSampler::AddSample(TimePoint timestamp,
                                                     const rvwheel::dal::RawAxisSnapshot& snapshot) {
    if (!IsConfigurationValid()) {
        samples_.clear();
        acquisitionStart_.reset();
        lastInputError_ = StableRawAxisStatus::InvalidConfiguration;
        hasInputError_ = true;
        return lastInputError_;
    }
    if (!samples_.empty() && timestamp < samples_.back().timestamp) {
        samples_.clear();
        acquisitionStart_.reset();
        lastInputError_ = StableRawAxisStatus::NonMonotonicTimestamp;
        hasInputError_ = true;
        return lastInputError_;
    }
    if (!ContainsEveryAxisExactlyOnce(snapshot)) {
        samples_.clear();
        acquisitionStart_.reset();
        lastInputError_ = StableRawAxisStatus::MissingAxis;
        hasInputError_ = true;
        return lastInputError_;
    }

    hasInputError_ = false;
    if (!acquisitionStart_) {
        acquisitionStart_ = timestamp;
    }
    samples_.push_back(TimedSnapshot{timestamp, snapshot});
    PruneOldSamples();
    return StableRawAxisStatus::Accepted;
}

void StableRawAxisSampler::NotifyPollFailure() {
    samples_.clear();
    acquisitionStart_.reset();
    lastInputError_ = StableRawAxisStatus::PollFailure;
    hasInputError_ = true;
}

void StableRawAxisSampler::Reset() noexcept {
    samples_.clear();
    acquisitionStart_.reset();
    hasInputError_ = false;
    lastInputError_ = StableRawAxisStatus::InsufficientSamples;
}

void StableRawAxisSampler::PruneOldSamples() {
    const TimePoint newest = samples_.back().timestamp;
    while (samples_.size() > 1 && newest - samples_[1].timestamp >= config_.stableWindow) {
        samples_.pop_front();
    }
    while (samples_.size() > config_.maximumSamples) {
        samples_.pop_front();
    }
}

StableRawAxisResult StableRawAxisSampler::Result(StableRawAxisStatus status, std::string diagnostic) const {
    StableRawAxisResult result;
    result.status = status;
    result.diagnostic = std::move(diagnostic);
    return result;
}

StableRawAxisResult StableRawAxisSampler::Evaluate() const {
    if (!IsConfigurationValid()) {
        for (const auto& axis : axes_) {
            if (axis.rawMax <= axis.rawMin) {
                return Result(StableRawAxisStatus::DegenerateRange, "An axis has a degenerate or inverted raw range.");
            }
        }
        return Result(StableRawAxisStatus::InvalidConfiguration, "The stable-sampler configuration is invalid.");
    }
    if (hasInputError_) {
        switch (lastInputError_) {
            case StableRawAxisStatus::MissingAxis:
                return Result(lastInputError_, "A raw snapshot omitted or duplicated a discovered axis; the window was reset.");
            case StableRawAxisStatus::NonMonotonicTimestamp:
                return Result(lastInputError_, "A sample timestamp moved backwards; the window was reset.");
            case StableRawAxisStatus::PollFailure:
                return Result(lastInputError_, "The last hardware poll failed; the window was reset.");
            default:
                return Result(lastInputError_, "The sample window was reset after invalid input.");
        }
    }
    if (samples_.size() < config_.minimumSamples) {
        return Result(StableRawAxisStatus::InsufficientSamples, "Not enough valid samples have been collected yet.");
    }

    const auto acquisitionDuration = samples_.back().timestamp - *acquisitionStart_;
    if (acquisitionDuration < config_.minimumAcquisition) {
        return Result(StableRawAxisStatus::InsufficientAcquisition, "The minimum acquisition time has not elapsed.");
    }
    const auto observedDuration = samples_.back().timestamp - samples_.front().timestamp;
    if (observedDuration < config_.stableWindow) {
        return Result(StableRawAxisStatus::InsufficientWindow, "The stability window is not full yet.");
    }

    StableRawAxisResult result;
    result.status = StableRawAxisStatus::Stable;
    for (const auto& axis : axes_) {
        std::vector<std::int32_t> values;
        values.reserve(samples_.size());
        for (const auto& timed : samples_) {
            const auto* sample = FindSample(timed.snapshot, axis.source);
            if (sample == nullptr) {
                return Result(StableRawAxisStatus::MissingAxis, "A retained raw snapshot is missing a discovered axis.");
            }
            values.push_back(sample->rawValue);
        }

        const std::int32_t median = Median(values);
        const double rawRange = static_cast<double>(axis.rawMax) - static_cast<double>(axis.rawMin);
        const double allowedDelta = rawRange * static_cast<double>(config_.relativeTolerance);
        std::size_t outlierCount = 0;
        for (const std::int32_t value : values) {
            if (std::abs(static_cast<double>(value) - static_cast<double>(median)) > allowedDelta) {
                ++outlierCount;
            }
        }

        // A single transient read must not bias the median or prevent a
        // good window from settling. For normal 60 Hz windows this permits
        // at most 10% outliers; tiny synthetic windows permit none.
        const std::size_t allowedOutliers = values.size() >= 5 ? std::max<std::size_t>(1, values.size() / 10) : 0;
        if (outlierCount > allowedOutliers) {
            return Result(StableRawAxisStatus::Unstable, "At least one axis is still moving or too noisy.");
        }

        result.snapshot.samples[result.snapshot.count] = rvwheel::dal::RawAxisSample{axis.source, median};
        ++result.snapshot.count;
    }
    return result;
}

} // namespace rvwheel::tools::probe
