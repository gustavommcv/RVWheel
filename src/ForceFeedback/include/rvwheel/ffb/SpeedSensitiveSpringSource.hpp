#pragma once

#include "rvwheel/ffb/ForceFeedbackTypes.hpp"
#include "rvwheel/ffb/IForceFeedbackSource.hpp"

namespace rvwheel::ffb {

// Pure curve: given an already-known-valid (finite, non-negative) speed and
// the profile's speed-sensitivity tuning, returns the fraction of
// springStrength that should apply, in [minimumScale, 1]. A smoothstep
// (never a linear ramp or a step) between speed=0 (minimumScale) and
// speed=fullStrengthSpeedMetersPerSecond (1.0); speed at or beyond the
// full-strength threshold is clamped to 1.0, never extrapolated past it.
// Defensive against a degenerate fullStrengthSpeedMetersPerSecond <= 0 or
// an out-of-[0,1] minimumScale (both should be rejected by profile
// validation already, but this function never assumes that and never
// divides by zero or returns outside [0, 1]).
[[nodiscard]] float ComputeSpeedSensitiveSpringScale(float speedMetersPerSecond, float minimumScale,
                                                       float fullStrengthSpeedMetersPerSecond) noexcept;

// The first genuinely telemetry-reactive force feedback source: the same
// centering spring SpringDamperSource always applies, but scaled by
// vehicle speed via ComputeSpeedSensitiveSpringScale (see
// SpeedSensitiveSpringConfig's own doc comment for the curve shape).
// Deliberately does not implement self-aligning torque, yaw-rate-derived
// effects, collisions, RPM, or terrain -- only a speed-scaled spring.
//
// Contract, mirroring SpringDamperSource where it applies:
//   - no valid speed (absent, NaN, Inf, or negative) -> the whole command
//     is zero (spring, damper, and constantForce all 0.0f), never a guess;
//   - a valid speed always yields spring in [0, springStrength] and damper
//     exactly equal to the configured damperStrength (unscaled -- this
//     source never invents a new damper curve of its own);
//   - constantForce is always 0.0f;
//   - gain is always 1.0f so the safety controller's masterGain is the only
//     place gain is ever applied, exactly like SpringDamperSource.
class SpeedSensitiveSpringSource final : public IForceFeedbackSource {
public:
    explicit SpeedSensitiveSpringSource(ForceFeedbackConfig config = {}) noexcept : config_(config) {}

    void Configure(const ForceFeedbackConfig& config) noexcept { config_ = config; }

    [[nodiscard]] rvwheel::dal::ForceFeedbackCommand Compute(const VehicleTelemetry& telemetry,
                                                               float wheelSteering) const noexcept override;

    [[nodiscard]] const char* Name() const noexcept override { return "SpeedSensitiveSpring"; }

private:
    ForceFeedbackConfig config_;
};

} // namespace rvwheel::ffb
