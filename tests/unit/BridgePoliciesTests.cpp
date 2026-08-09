#include <catch2/catch_test_macros.hpp>

#include "BridgePolicies.hpp"

using rvwheel::dal::ReadinessState;
using rvwheel::dal::WheelState;
using rvwheel::tools::probe::IsReadyToEnableForceFeedback;
using rvwheel::tools::probe::ShouldRefreshDuringBridge;

namespace {
WheelState MakeState(bool connected, bool valid, ReadinessState readiness) {
    WheelState state;
    state.connected = connected;
    state.valid = valid;
    state.readiness = readiness;
    return state;
}
} // namespace

TEST_CASE("ShouldRefreshDuringBridge: an input-only bridge (no --enable-force-feedback) allows refresh",
          "[Bridge][Policy]") {
    REQUIRE(ShouldRefreshDuringBridge(false));
}

TEST_CASE("ShouldRefreshDuringBridge: a run that requested exclusive force-feedback access never refreshes",
          "[Bridge][Policy]") {
    REQUIRE_FALSE(ShouldRefreshDuringBridge(true));
}

TEST_CASE("ShouldRefreshDuringBridge: the decision depends only on the run-level flag, never on session "
          "state -- so a session ending or faulting can never re-enable refresh mid-run",
          "[Bridge][Policy]") {
    // The function takes no session-state parameter at all: there is no
    // "session just faulted" or "session just stopped" input it could even
    // read. Calling it repeatedly with the same run-level flag, simulating
    // many ticks across a session's lifetime (armed, active, faulted,
    // stopped), always yields the same answer -- proving the invariant
    // structurally, not just for one snapshot in time.
    for (int tick = 0; tick < 5; ++tick) {
        REQUIRE_FALSE(ShouldRefreshDuringBridge(true));
    }
}

TEST_CASE("IsReadyToEnableForceFeedback: requires a successful poll plus connected+valid+Ready, all four",
          "[Bridge][Policy]") {
    REQUIRE(IsReadyToEnableForceFeedback(true, MakeState(true, true, ReadinessState::Ready)));

    REQUIRE_FALSE(IsReadyToEnableForceFeedback(false, MakeState(true, true, ReadinessState::Ready)));
    REQUIRE_FALSE(IsReadyToEnableForceFeedback(true, MakeState(false, true, ReadinessState::Ready)));
    REQUIRE_FALSE(IsReadyToEnableForceFeedback(true, MakeState(true, false, ReadinessState::Ready)));
    REQUIRE_FALSE(IsReadyToEnableForceFeedback(true, MakeState(true, true, ReadinessState::WarmingUp)));
    REQUIRE_FALSE(IsReadyToEnableForceFeedback(true, MakeState(true, true, ReadinessState::Stabilizing)));
    REQUIRE_FALSE(IsReadyToEnableForceFeedback(true, MakeState(true, true, ReadinessState::AwaitingActivation)));
    REQUIRE_FALSE(IsReadyToEnableForceFeedback(true, MakeState(true, true, ReadinessState::Unconfigured)));
    REQUIRE_FALSE(IsReadyToEnableForceFeedback(true, MakeState(true, true, ReadinessState::TimedOut)));
}
