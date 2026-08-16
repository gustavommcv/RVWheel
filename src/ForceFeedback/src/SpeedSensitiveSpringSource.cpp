#include "rvwheel/ffb/SpeedSensitiveSpringSource.hpp"

#include <algorithm>
#include <cmath>

namespace rvwheel::ffb {

float ComputeSpeedSensitiveSpringScale(float speedMetersPerSecond, float minimumScale,
                                        float fullStrengthSpeedMetersPerSecond) noexcept {
    const float clampedMinimumScale = std::clamp(minimumScale, 0.0f, 1.0f);
    if (fullStrengthSpeedMetersPerSecond <= 0.0f) {
        // Degenerate config (should already be rejected by profile
        // validation): treat as "already at full strength" rather than
        // dividing by zero or returning something outside [minimumScale, 1].
        return 1.0f;
    }

    const float t = std::clamp(speedMetersPerSecond / fullStrengthSpeedMetersPerSecond, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t); // Classic smoothstep: 0 at t=0, 1 at t=1, S-shaped between.
    return clampedMinimumScale + (1.0f - clampedMinimumScale) * smooth;
}

rvwheel::dal::ForceFeedbackCommand SpeedSensitiveSpringSource::Compute(const VehicleTelemetry& telemetry,
                                                                         float /*wheelSteering*/) const noexcept {
    rvwheel::dal::ForceFeedbackCommand zero{0.0f, 0.0f, 0.0f, 1.0f};

    const bool hasValidSpeed = telemetry.speedMetersPerSecond.has_value() &&
                                std::isfinite(*telemetry.speedMetersPerSecond) &&
                                *telemetry.speedMetersPerSecond >= 0.0f;
    if (!hasValidSpeed) {
        return zero;
    }

    const float springStrength = std::clamp(config_.springStrength, 0.0f, 1.0f);
    const float scale = ComputeSpeedSensitiveSpringScale(*telemetry.speedMetersPerSecond,
                                                          config_.speedSensitiveSpring.minimumScale,
                                                          config_.speedSensitiveSpring.fullStrengthSpeedMetersPerSecond);

    rvwheel::dal::ForceFeedbackCommand out{};
    out.constantForce = 0.0f;
    // Belt-and-suspenders clamp on top of scale's own [0,1] range: this
    // source must never report more than the profile's own springStrength,
    // regardless of any future change to the curve above.
    out.spring = std::clamp(scale * springStrength, 0.0f, springStrength);
    out.damper = std::clamp(config_.damperStrength, 0.0f, 1.0f); // Passthrough only -- no new damper curve here.
    out.gain = 1.0f;
    return out;
}

} // namespace rvwheel::ffb
