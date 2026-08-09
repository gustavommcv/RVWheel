#pragma once

#include "rvwheel/ffb/ForceFeedbackTypes.hpp"
#include "rvwheel/ffb/IForceFeedbackSource.hpp"

namespace rvwheel::ffb {

// MVP force feedback source: a profile-configured centering spring and
// damper, requiring no vehicle telemetry at all. This is the "stable
// baseline" priority #1 effect from the Force Feedback design brief --
// always computable, with zero open questions about game/telemetry
// availability. Self-aligning torque and other telemetry-derived sources
// are added alongside this one, not in place of it.
class SpringDamperSource final : public IForceFeedbackSource {
public:
    explicit SpringDamperSource(ForceFeedbackConfig config = {}) noexcept : config_(config) {}

    void Configure(const ForceFeedbackConfig& config) noexcept { config_ = config; }

    [[nodiscard]] rvwheel::dal::ForceFeedbackCommand Compute(const VehicleTelemetry& telemetry,
                                                               float wheelSteering) const noexcept override;

    [[nodiscard]] const char* Name() const noexcept override { return "SpringDamper"; }

private:
    ForceFeedbackConfig config_;
};

} // namespace rvwheel::ffb
