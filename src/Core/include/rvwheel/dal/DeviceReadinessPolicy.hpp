#pragma once

#include <chrono>

namespace rvwheel::dal {

// Policy for how long to wait, and how to judge stability, before trusting
// a device's input after connect/acquire. See DeviceReadinessTracker for
// the state machine that applies this policy, and
// ReadinessState/WheelState::readiness for how it is surfaced.
struct DeviceReadinessPolicy {
    std::chrono::milliseconds minimumWarmup{0};
    std::chrono::milliseconds stableSample{0};
    std::chrono::milliseconds maximumWait{0};

    // Maximum allowed change in any relevant normalized axis (same domain
    // as WheelState fields: steering in [-1,1], pedals in [0,1]) across
    // the stableSample window for it to still count as "stable". Not a
    // per-model hardcoded value: a profile can override it, but this
    // default is generous enough for ordinary hand-tremor-level noise
    // without being so loose it would accept a still-moving startup
    // transient like the G923's.
    float stabilityTolerance = 0.01f;

    // Deliberately conservative: used only when no profile (built-in,
    // user, or generated) supplies its own readiness policy for a device,
    // i.e. exactly the "safe generic heuristic" path in the plug-and-play
    // fallback flow. Longer minimum warmup and wider stability window than
    // the G923's own verified ~2.05s transient, because an unknown
    // device's startup behavior is, by definition, unverified.
    [[nodiscard]] static DeviceReadinessPolicy ConservativeDefault() noexcept {
        DeviceReadinessPolicy policy;
        policy.minimumWarmup = std::chrono::milliseconds{3000};
        policy.stableSample = std::chrono::milliseconds{500};
        policy.maximumWait = std::chrono::milliseconds{8000};
        policy.stabilityTolerance = 0.02f;
        return policy;
    }
};

} // namespace rvwheel::dal
