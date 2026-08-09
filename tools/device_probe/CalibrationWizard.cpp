#include "CalibrationWizard.hpp"

#include <cmath>
#include <utility>

namespace rvwheel::tools::probe {

namespace {

using rvwheel::dal::AxisBinding;
using rvwheel::dal::AxisCenterPolicy;
using rvwheel::dal::AxisDirection;
using rvwheel::dal::AxisSource;
using rvwheel::dal::RawAxisInfo;
using rvwheel::dal::RawAxisSnapshot;

[[nodiscard]] std::optional<std::int32_t> FindRawValue(const RawAxisSnapshot& snapshot, AxisSource source) noexcept {
    for (std::uint8_t i = 0; i < snapshot.count; ++i) {
        if (snapshot.samples[i].source == source) {
            return snapshot.samples[i].rawValue;
        }
    }
    return std::nullopt;
}

// "Released"/"left" plays the same role as "rawAtMin" in AxisNormalizer's
// convention: whichever end of the raw range the observed sample sits
// closer to determines whether this axis reads normally or inverted.
[[nodiscard]] AxisDirection InferDirectionFromEndpoint(std::int32_t observedRaw, const RawAxisInfo& info) noexcept {
    const double distToMin = std::abs(static_cast<double>(observedRaw) - static_cast<double>(info.rawMin));
    const double distToMax = std::abs(static_cast<double>(observedRaw) - static_cast<double>(info.rawMax));
    return (distToMin <= distToMax) ? AxisDirection::Normal : AxisDirection::Inverted;
}

} // namespace

CalibrationWizard::CalibrationWizard(std::vector<rvwheel::dal::RawAxisInfo> axes, float relativeThreshold)
    : axes_(std::move(axes)), relativeThreshold_(relativeThreshold) {}

const rvwheel::dal::RawAxisInfo* CalibrationWizard::FindAxisInfo(rvwheel::dal::AxisSource source) const noexcept {
    for (const auto& info : axes_) {
        if (info.source == source) {
            return &info;
        }
    }
    return nullptr;
}

std::vector<CalibrationWizard::AxisDelta> CalibrationWizard::ComputeDeltas(const rvwheel::dal::RawAxisSnapshot& a,
                                                                            const rvwheel::dal::RawAxisSnapshot& b) const {
    std::vector<AxisDelta> deltas;
    for (std::uint8_t i = 0; i < a.count; ++i) {
        const AxisSource source = a.samples[i].source;
        const auto bValue = FindRawValue(b, source);
        const RawAxisInfo* info = FindAxisInfo(source);
        if (!bValue || info == nullptr) {
            continue;
        }
        const double range = std::abs(static_cast<double>(info->rawMax) - static_cast<double>(info->rawMin));
        if (range <= 0.0) {
            continue; // Degenerate axis range; cannot compute a meaningful relative delta.
        }
        const double rawDelta = std::abs(static_cast<double>(a.samples[i].rawValue) - static_cast<double>(*bValue));
        deltas.push_back(AxisDelta{source, static_cast<float>(rawDelta / range)});
    }
    return deltas;
}

std::optional<rvwheel::dal::AxisSource> CalibrationWizard::DominantMovedAxis(const std::vector<AxisDelta>& deltas,
                                                                              CalibrationStepOutcome& outcome) const {
    std::vector<AxisSource> moved;
    for (const auto& delta : deltas) {
        if (delta.relativeDelta >= relativeThreshold_) {
            moved.push_back(delta.source);
        }
    }
    if (moved.empty()) {
        outcome = CalibrationStepOutcome::NoMovement;
        return std::nullopt;
    }
    if (moved.size() > 1) {
        outcome = CalibrationStepOutcome::Ambiguous;
        return std::nullopt;
    }
    outcome = CalibrationStepOutcome::Recorded;
    return moved.front();
}

CalibrationStepOutcome CalibrationWizard::SubmitSnapshot(const rvwheel::dal::RawAxisSnapshot& snapshot) {
    switch (step_) {
        case CalibrationStepKind::Baseline:
            baseline_ = snapshot;
            step_ = CalibrationStepKind::SteeringCenter;
            return CalibrationStepOutcome::Recorded;

        case CalibrationStepKind::SteeringCenter:
            for (const auto& delta : ComputeDeltas(snapshot, baseline_)) {
                if (delta.relativeDelta >= relativeThreshold_) {
                    // Some drivers publish placeholder values until their
                    // first physical input. Never let that late activation
                    // silently become the steering reference: rebase the
                    // all-controls-released snapshot and require a second
                    // stable confirmation.
                    baseline_ = snapshot;
                    return CalibrationStepOutcome::BaselineChanged;
                }
            }
            steeringCenterSnapshot_ = snapshot;
            step_ = CalibrationStepKind::SteeringLeft;
            return CalibrationStepOutcome::Recorded;

        case CalibrationStepKind::SteeringLeft: {
            CalibrationStepOutcome outcome{};
            const auto axis = DominantMovedAxis(ComputeDeltas(snapshot, steeringCenterSnapshot_), outcome);
            if (!axis) {
                return outcome;
            }
            steeringAxis_ = axis;
            steeringLeftSnapshot_ = snapshot;
            step_ = CalibrationStepKind::SteeringRight;
            return CalibrationStepOutcome::Recorded;
        }

        case CalibrationStepKind::SteeringRight: {
            CalibrationStepOutcome outcome{};
            const auto axis = DominantMovedAxis(ComputeDeltas(snapshot, steeringCenterSnapshot_), outcome);
            if (!axis) {
                return outcome;
            }
            if (*axis != *steeringAxis_) {
                return CalibrationStepOutcome::Inconsistent;
            }
            step_ = CalibrationStepKind::ThrottleReleased;
            return CalibrationStepOutcome::Recorded;
        }

        case CalibrationStepKind::ThrottleReleased:
            throttleReleasedSnapshot_ = snapshot;
            step_ = CalibrationStepKind::ThrottlePressed;
            return CalibrationStepOutcome::Recorded;

        case CalibrationStepKind::ThrottlePressed: {
            CalibrationStepOutcome outcome{};
            const auto axis = DominantMovedAxis(ComputeDeltas(snapshot, throttleReleasedSnapshot_), outcome);
            if (!axis) {
                return outcome;
            }
            throttleAxis_ = axis;
            const RawAxisInfo* info = FindAxisInfo(*axis);
            const auto releasedRaw = FindRawValue(throttleReleasedSnapshot_, *axis);
            if (info != nullptr && releasedRaw) {
                throttleDirection_ = InferDirectionFromEndpoint(*releasedRaw, *info);
            }
            step_ = CalibrationStepKind::BrakeReleased;
            return CalibrationStepOutcome::Recorded;
        }

        case CalibrationStepKind::BrakeReleased:
            brakeReleasedSnapshot_ = snapshot;
            step_ = CalibrationStepKind::BrakePressed;
            return CalibrationStepOutcome::Recorded;

        case CalibrationStepKind::BrakePressed: {
            CalibrationStepOutcome outcome{};
            const auto axis = DominantMovedAxis(ComputeDeltas(snapshot, brakeReleasedSnapshot_), outcome);
            if (!axis) {
                return outcome;
            }
            brakeAxis_ = axis;
            const RawAxisInfo* info = FindAxisInfo(*axis);
            const auto releasedRaw = FindRawValue(brakeReleasedSnapshot_, *axis);
            if (info != nullptr && releasedRaw) {
                brakeDirection_ = InferDirectionFromEndpoint(*releasedRaw, *info);
            }
            step_ = CalibrationStepKind::ClutchReleased;
            return CalibrationStepOutcome::Recorded;
        }

        case CalibrationStepKind::ClutchReleased:
            clutchReleasedSnapshot_ = snapshot;
            step_ = CalibrationStepKind::ClutchPressed;
            return CalibrationStepOutcome::Recorded;

        case CalibrationStepKind::ClutchPressed: {
            CalibrationStepOutcome outcome{};
            const auto axis = DominantMovedAxis(ComputeDeltas(snapshot, clutchReleasedSnapshot_), outcome);
            if (!axis) {
                return outcome;
            }
            clutchAxis_ = axis;
            const RawAxisInfo* info = FindAxisInfo(*axis);
            const auto releasedRaw = FindRawValue(clutchReleasedSnapshot_, *axis);
            if (info != nullptr && releasedRaw) {
                clutchDirection_ = InferDirectionFromEndpoint(*releasedRaw, *info);
            }
            step_ = CalibrationStepKind::Summary;
            return CalibrationStepOutcome::Recorded;
        }

        case CalibrationStepKind::Summary:
        case CalibrationStepKind::Done:
        default:
            return CalibrationStepOutcome::NoMovement;
    }
}

void CalibrationWizard::SkipClutch() noexcept {
    if (step_ == CalibrationStepKind::ClutchReleased) {
        clutchSkipped_ = true;
        step_ = CalibrationStepKind::Summary;
    }
}

CalibrationResult CalibrationWizard::Finish() {
    CalibrationResult result;
    if (step_ != CalibrationStepKind::Summary) {
        result.success = false;
        result.failureReason = "Finish() called before calibration reached the Summary step.";
        return result;
    }

    rvwheel::dal::WheelInputLayout layout;

    if (!steeringAxis_) {
        result.failureReason = "Steering axis was never identified.";
        return result;
    }
    {
        const RawAxisInfo* info = FindAxisInfo(*steeringAxis_);
        const auto leftRaw = FindRawValue(steeringLeftSnapshot_, *steeringAxis_);
        AxisDirection direction = AxisDirection::Normal;
        if (info != nullptr && leftRaw) {
            direction = InferDirectionFromEndpoint(*leftRaw, *info);
        }
        layout.steering = AxisBinding{*steeringAxis_, direction, AxisCenterPolicy::RangeMidpoint};
        result.summaryLines.push_back("steering -> " + std::string(rvwheel::dal::ToString(*steeringAxis_)) + " (" +
                                       (direction == AxisDirection::Normal ? "normal" : "inverted") + ")");
    }

    if (!throttleAxis_) {
        result.failureReason = "Throttle axis was never identified.";
        return result;
    }
    layout.throttle = AxisBinding{*throttleAxis_, throttleDirection_, std::nullopt};
    result.summaryLines.push_back("throttle -> " + std::string(rvwheel::dal::ToString(*throttleAxis_)) + " (" +
                                   (throttleDirection_ == AxisDirection::Normal ? "normal" : "inverted") + ")");

    if (!brakeAxis_) {
        result.failureReason = "Brake axis was never identified.";
        return result;
    }
    layout.brake = AxisBinding{*brakeAxis_, brakeDirection_, std::nullopt};
    result.summaryLines.push_back("brake -> " + std::string(rvwheel::dal::ToString(*brakeAxis_)) + " (" +
                                   (brakeDirection_ == AxisDirection::Normal ? "normal" : "inverted") + ")");

    if (clutchSkipped_) {
        result.summaryLines.push_back("clutch -> skipped");
    } else if (clutchAxis_) {
        layout.clutch = AxisBinding{*clutchAxis_, clutchDirection_, std::nullopt};
        result.summaryLines.push_back("clutch -> " + std::string(rvwheel::dal::ToString(*clutchAxis_)) + " (" +
                                       (clutchDirection_ == AxisDirection::Normal ? "normal" : "inverted") + ")");
    }

    result.success = true;
    result.layout = layout;
    step_ = CalibrationStepKind::Done;
    return result;
}

} // namespace rvwheel::tools::probe
