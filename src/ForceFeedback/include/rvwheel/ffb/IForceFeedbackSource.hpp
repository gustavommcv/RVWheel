#pragma once

#include "rvwheel/dal/WheelTypes.hpp"
#include "rvwheel/ffb/ForceFeedbackTypes.hpp"

namespace rvwheel::ffb {

// One independent contributor to the final mixed force feedback command
// (e.g. a profile-configured centering spring, or a future
// telemetry-derived self-aligning-torque source). A source never talks to
// a device, never knows about DirectInput, and never applies its own
// safety limits -- ForceFeedbackMixer combines contributions and
// ForceFeedbackSafetyController is the only place limits are enforced.
class IForceFeedbackSource {
public:
    virtual ~IForceFeedbackSource() = default;

    // Returns this source's contribution for the current tick. `telemetry`
    // may have any or all fields absent -- a source that needs data it does
    // not have must return an all-zero command rather than guessing.
    // `wheelSteering` is the wheel's current normalized steering position
    // ([-1, 1]); condition effects (spring/damper) are direction-neutral
    // from software's point of view (the device computes the
    // position-dependent restoring force itself once started), so most
    // sources can ignore it -- it exists for sources that compute a signed
    // constant-force torque themselves.
    [[nodiscard]] virtual rvwheel::dal::ForceFeedbackCommand Compute(const VehicleTelemetry& telemetry,
                                                                       float wheelSteering) const noexcept = 0;

    [[nodiscard]] virtual const char* Name() const noexcept = 0;

protected:
    IForceFeedbackSource() = default;
    IForceFeedbackSource(const IForceFeedbackSource&) = default;
    IForceFeedbackSource& operator=(const IForceFeedbackSource&) = default;
};

} // namespace rvwheel::ffb
