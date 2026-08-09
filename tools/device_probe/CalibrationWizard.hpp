#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rvwheel/dal/ICalibratableWheelDevice.hpp"
#include "rvwheel/dal/WheelInputLayout.hpp"

namespace rvwheel::tools::probe {

enum class CalibrationStepKind {
    Baseline,
    SteeringCenter,
    SteeringLeft,
    SteeringRight,
    ThrottleReleased,
    ThrottlePressed,
    BrakeReleased,
    BrakePressed,
    ClutchReleased,
    ClutchPressed,
    Summary,
    Done,
};

enum class CalibrationStepOutcome {
    Recorded,   // Snapshot accepted; CurrentStep() has advanced.
    BaselineChanged, // Rest/center changed since the baseline; baseline was
                     // refreshed and the current step must be confirmed again.
    Ambiguous,  // More than one axis moved above threshold; retry the same step.
    NoMovement, // No axis moved above threshold; retry the same step.
    Inconsistent, // A cross-check step (SteeringRight) detected movement on a
                  // DIFFERENT axis than the one already identified; retry.
};

struct CalibrationResult {
    bool success = false;
    rvwheel::dal::WheelInputLayout layout;
    std::vector<std::string> summaryLines; // Human-readable, for the confirmation screen.
    std::string failureReason;              // Set when success is false.
};

// Pure calibration engine: no console I/O, no DAL device access, no
// Win32. Fed raw axis metadata once (from
// ICalibratableWheelDevice::EnumerateRawAxes()) and a RawAxisSnapshot per
// confirmed step; produces step-by-step outcomes and, once every step is
// done, a resolved WheelInputLayout. This is exactly what tests exercise
// with synthetic snapshots -- the console-facing wizard glue in
// DeviceProbeApp only prompts the user and forwards real snapshots here.
class CalibrationWizard {
public:
    explicit CalibrationWizard(std::vector<rvwheel::dal::RawAxisInfo> axes, float relativeThreshold = 0.05f);

    [[nodiscard]] CalibrationStepKind CurrentStep() const noexcept { return step_; }

    // Submits the snapshot captured for the current step. Invalid for
    // ClutchReleased/ClutchPressed if the caller intends to skip instead
    // (use SkipClutch()).
    [[nodiscard]] CalibrationStepOutcome SubmitSnapshot(const rvwheel::dal::RawAxisSnapshot& snapshot);

    // Only valid when CurrentStep() == ClutchReleased; skips both clutch
    // steps and advances straight to Summary.
    void SkipClutch() noexcept;

    // Only valid when CurrentStep() == Summary; advances to Done and
    // returns the resolved layout. Calling this before Summary returns
    // success = false with a failureReason explaining why.
    [[nodiscard]] CalibrationResult Finish();

private:
    struct AxisDelta {
        rvwheel::dal::AxisSource source;
        float relativeDelta;
    };

    [[nodiscard]] const rvwheel::dal::RawAxisInfo* FindAxisInfo(rvwheel::dal::AxisSource source) const noexcept;
    [[nodiscard]] std::vector<AxisDelta> ComputeDeltas(const rvwheel::dal::RawAxisSnapshot& a,
                                                        const rvwheel::dal::RawAxisSnapshot& b) const;
    [[nodiscard]] std::optional<rvwheel::dal::AxisSource> DominantMovedAxis(const std::vector<AxisDelta>& deltas,
                                                                             CalibrationStepOutcome& outcome) const;

    std::vector<rvwheel::dal::RawAxisInfo> axes_;
    float relativeThreshold_;
    CalibrationStepKind step_ = CalibrationStepKind::Baseline;

    rvwheel::dal::RawAxisSnapshot baseline_;
    rvwheel::dal::RawAxisSnapshot steeringCenterSnapshot_;
    rvwheel::dal::RawAxisSnapshot steeringLeftSnapshot_;
    rvwheel::dal::RawAxisSnapshot throttleReleasedSnapshot_;
    rvwheel::dal::RawAxisSnapshot brakeReleasedSnapshot_;
    rvwheel::dal::RawAxisSnapshot clutchReleasedSnapshot_;

    std::optional<rvwheel::dal::AxisSource> steeringAxis_;
    std::optional<rvwheel::dal::AxisSource> throttleAxis_;
    std::optional<rvwheel::dal::AxisSource> brakeAxis_;
    std::optional<rvwheel::dal::AxisSource> clutchAxis_;

    rvwheel::dal::AxisDirection throttleDirection_ = rvwheel::dal::AxisDirection::Normal;
    rvwheel::dal::AxisDirection brakeDirection_ = rvwheel::dal::AxisDirection::Normal;
    rvwheel::dal::AxisDirection clutchDirection_ = rvwheel::dal::AxisDirection::Normal;

    bool clutchSkipped_ = false;
};

} // namespace rvwheel::tools::probe
