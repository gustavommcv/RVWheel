#pragma once

#include "rvwheel/dal/ReadinessState.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::tools::probe {

// Pure decision, no I/O, no device/manager access: whether --bridge's
// periodic DeviceManager::RefreshIfDue() call may run this tick.
//
// The only input is whether THIS RUN ever requested exclusive
// force-feedback access (i.e. --enable-force-feedback was passed) --
// deliberately NOT any per-tick BridgeForceFeedbackSession state (Armed,
// Active, Faulted, Stopped). A second exclusive Acquire() of the same
// physical device while any earlier acquisition (faulted or not) still
// holds it is the confirmed root cause of the DIERR_NOTEXCLUSIVEACQUIRED
// failure documented in docs/FORCE_FEEDBACK_HARDWARE_TEST.md's incident
// log. Keeping the decision independent of session state means a session
// ending, faulting, or never arming at all can never flip this back to
// true mid-run and trigger a second acquisition -- reconnecting the wheel
// requires restarting the whole bridge process, on purpose.
[[nodiscard]] constexpr bool ShouldRefreshDuringBridge(bool exclusiveForceFeedbackAccessRequestedThisRun) noexcept {
    return !exclusiveForceFeedbackAccessRequestedThisRun;
}

// Pure decision, no I/O: whether it is safe to call
// BridgeForceFeedbackSession::Enable() this tick. All three device-state
// checks are required, even though `state.valid` already implies the
// other two per WheelState's own contract (last Poll() succeeded AND
// connected AND readiness == Ready) -- kept explicit here so the safety
// condition reads directly from the requirement, not from a single flag's
// documented-but-not-enforced meaning.
[[nodiscard]] constexpr bool IsReadyToEnableForceFeedback(bool pollSucceeded,
                                                            const rvwheel::dal::WheelState& state) noexcept {
    return pollSucceeded && state.connected && state.valid && state.readiness == rvwheel::dal::ReadinessState::Ready;
}

} // namespace rvwheel::tools::probe
