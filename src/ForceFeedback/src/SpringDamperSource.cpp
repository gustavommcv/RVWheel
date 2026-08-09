#include "rvwheel/ffb/SpringDamperSource.hpp"

#include <algorithm>

namespace rvwheel::ffb {

rvwheel::dal::ForceFeedbackCommand SpringDamperSource::Compute(const VehicleTelemetry& /*telemetry*/,
                                                                 float /*wheelSteering*/) const noexcept {
    rvwheel::dal::ForceFeedbackCommand out{};
    out.constantForce = 0.0f;
    out.spring = std::clamp(config_.springStrength, 0.0f, 1.0f);
    out.damper = std::clamp(config_.damperStrength, 0.0f, 1.0f);
    // The safety controller/mixer own master gain; this source always
    // reports full-scale so it never silently double-applies a gain cut.
    out.gain = 1.0f;
    return out;
}

} // namespace rvwheel::ffb
