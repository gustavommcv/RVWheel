#pragma once

#include <cstdint>

namespace rvwheel::dal {

// Lifecycle of a device's readiness to publish gameplay-valid input,
// tracked per-device from the moment a WheelInputLayout is applied.
// Separate from Status/StatusCode: readiness is an ongoing state, not a
// one-shot operation result, and "still warming up" is expected, routine
// behavior, not an error.
enum class ReadinessState : std::uint8_t {
    // No WheelInputLayout has been applied to this device yet: the DAL has
    // nothing to say about steering/throttle/brake/clutch at all.
    Unconfigured,
    // A verified profile says this driver may expose a stable placeholder
    // until a physical control changes. Input remains invalid until that
    // first meaningful axis change is observed.
    AwaitingActivation,
    // A layout is applied but the device hasn't run for
    // minimumWarmupMilliseconds yet; input is being read but must not be
    // treated as gameplay-valid (see the G923 startup transient).
    WarmingUp,
    // Past the minimum warmup; waiting for the relevant axes to hold
    // steady for stableSampleMilliseconds before trusting them.
    Stabilizing,
    // Past both the minimum warmup and the stability window: input is
    // gameplay-valid.
    Ready,
    // maximumWaitMilliseconds elapsed without reaching Ready. Terminal
    // until the device disconnects/reacquires or a new layout is applied.
    TimedOut,
};

} // namespace rvwheel::dal
