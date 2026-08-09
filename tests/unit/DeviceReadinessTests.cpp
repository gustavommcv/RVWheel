#include <catch2/catch_test_macros.hpp>

#include "rvwheel/dal/DeviceReadinessTracker.hpp"

using rvwheel::dal::DeviceReadinessPolicy;
using rvwheel::dal::DeviceReadinessTracker;
using rvwheel::dal::ReadinessAxisSample;
using rvwheel::dal::ReadinessState;

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point T(long long ms) { return Clock::time_point{} + std::chrono::milliseconds{ms}; }
} // namespace

TEST_CASE("DeviceReadinessTracker: starts Unconfigured before any Reset", "[DeviceReadinessTracker]") {
    const DeviceReadinessPolicy policy{};
    const DeviceReadinessTracker tracker(policy);
    REQUIRE(tracker.CurrentState() == ReadinessState::Unconfigured);
}

TEST_CASE("DeviceReadinessTracker: activation-gated profiles never accept a stable startup placeholder",
          "[DeviceReadinessTracker][Activation]") {
    DeviceReadinessPolicy policy{};
    policy.requireAxisActivation = true;
    policy.activationThreshold = 0.05f;
    policy.minimumWarmup = std::chrono::milliseconds{100};
    policy.stableSample = std::chrono::milliseconds{100};
    policy.maximumWait = std::chrono::milliseconds{500};

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);
    REQUIRE(tracker.CurrentState() == ReadinessState::AwaitingActivation);

    ReadinessAxisSample placeholder{};
    placeholder.throttle = 0.5f;
    placeholder.brake = 0.5f;
    placeholder.clutch = 0.5f;
    placeholder.hasClutch = true;

    REQUIRE(tracker.Update(T(0), placeholder) == ReadinessState::AwaitingActivation);
    REQUIRE(tracker.Update(T(10000), placeholder) == ReadinessState::AwaitingActivation);

    ReadinessAxisSample activated = placeholder;
    activated.steering = 0.25f;
    activated.throttle = 0.0f;
    activated.brake = 0.0f;
    activated.clutch = 0.0f;
    REQUIRE(tracker.Update(T(10001), activated) == ReadinessState::WarmingUp);
    REQUIRE(tracker.Update(T(10102), activated) == ReadinessState::Stabilizing);
    REQUIRE(tracker.Update(T(10203), activated) == ReadinessState::Ready);
}

TEST_CASE("DeviceReadinessTracker: sub-threshold noise does not satisfy activation", "[DeviceReadinessTracker][Activation]") {
    DeviceReadinessPolicy policy{};
    policy.requireAxisActivation = true;
    policy.activationThreshold = 0.05f;

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);

    ReadinessAxisSample initial{};
    REQUIRE(tracker.Update(T(0), initial) == ReadinessState::AwaitingActivation);
    ReadinessAxisSample noise = initial;
    noise.steering = 0.049f;
    REQUIRE(tracker.Update(T(100), noise) == ReadinessState::AwaitingActivation);
    noise.steering = 0.05f;
    REQUIRE(tracker.Update(T(101), noise) == ReadinessState::WarmingUp);
}

TEST_CASE("DeviceReadinessTracker: stays WarmingUp until minimumWarmup elapses", "[DeviceReadinessTracker]") {
    DeviceReadinessPolicy policy{};
    policy.minimumWarmup = std::chrono::milliseconds{2200};
    policy.stableSample = std::chrono::milliseconds{250};
    policy.maximumWait = std::chrono::milliseconds{5000};

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);
    REQUIRE(tracker.CurrentState() == ReadinessState::WarmingUp);

    ReadinessAxisSample sample{};
    sample.throttle = 0.5f;
    sample.brake = 0.5f;
    sample.clutch = 0.5f;
    sample.hasClutch = true;

    // Still within minimumWarmup: must not become Ready even though the
    // sample itself is perfectly steady.
    REQUIRE(tracker.Update(T(500), sample) == ReadinessState::WarmingUp);
    REQUIRE(tracker.Update(T(2000), sample) == ReadinessState::WarmingUp);
}

TEST_CASE("DeviceReadinessTracker: requires stability for stableSample after minimumWarmup", "[DeviceReadinessTracker]") {
    DeviceReadinessPolicy policy{};
    policy.minimumWarmup = std::chrono::milliseconds{2000};
    policy.stableSample = std::chrono::milliseconds{300};
    policy.maximumWait = std::chrono::milliseconds{10000};

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);

    ReadinessAxisSample steady{};
    steady.throttle = 0.0f;
    steady.brake = 0.0f;

    REQUIRE(tracker.Update(T(1999), steady) == ReadinessState::WarmingUp);
    // Crosses minimumWarmup: enters Stabilizing, starting a fresh window.
    REQUIRE(tracker.Update(T(2001), steady) == ReadinessState::Stabilizing);
    // Not yet stableSample (300ms) into the stabilizing window.
    REQUIRE(tracker.Update(T(2200), steady) == ReadinessState::Stabilizing);
    // 300ms after the window started at 2001 -> Ready.
    REQUIRE(tracker.Update(T(2301), steady) == ReadinessState::Ready);
}

TEST_CASE("DeviceReadinessTracker: movement above tolerance restarts the stability window", "[DeviceReadinessTracker]") {
    DeviceReadinessPolicy policy{};
    policy.minimumWarmup = std::chrono::milliseconds{100};
    policy.stableSample = std::chrono::milliseconds{300};
    policy.maximumWait = std::chrono::milliseconds{10000};

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);

    ReadinessAxisSample a{};
    a.throttle = 0.0f;
    ReadinessAxisSample b{};
    b.throttle = 0.5f; // Well above the default 0.01 tolerance.

    REQUIRE(tracker.Update(T(150), a) == ReadinessState::Stabilizing); // Enters Stabilizing, window starts at 150.
    REQUIRE(tracker.Update(T(300), b) == ReadinessState::Stabilizing); // Moved: window restarts at 300.
    REQUIRE(tracker.Update(T(450), b) == ReadinessState::Stabilizing); // Only 150ms since the restart; not stable yet.
    REQUIRE(tracker.Update(T(601), b) == ReadinessState::Ready);       // 301ms since 300 -> Ready.
}

TEST_CASE("DeviceReadinessTracker: times out if never stable within maximumWait", "[DeviceReadinessTracker]") {
    DeviceReadinessPolicy policy{};
    policy.minimumWarmup = std::chrono::milliseconds{100};
    policy.stableSample = std::chrono::milliseconds{300};
    policy.maximumWait = std::chrono::milliseconds{1000};

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);

    ReadinessAxisSample jittery{};
    for (long long ms = 150; ms < 1000; ms += 50) {
        jittery.throttle = (ms % 100 == 0) ? 0.0f : 0.5f; // Keeps moving; never stabilizes.
        (void)tracker.Update(T(ms), jittery);
    }
    REQUIRE(tracker.Update(T(1001), jittery) == ReadinessState::TimedOut);
}

TEST_CASE("DeviceReadinessTracker: Reset restarts the state machine (disconnect/reconnect)", "[DeviceReadinessTracker]") {
    DeviceReadinessPolicy policy{};
    policy.minimumWarmup = std::chrono::milliseconds{100};
    policy.stableSample = std::chrono::milliseconds{100};
    policy.maximumWait = std::chrono::milliseconds{5000};

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);

    ReadinessAxisSample steady{};
    (void)tracker.Update(T(150), steady);
    REQUIRE(tracker.Update(T(300), steady) == ReadinessState::Ready);

    // Simulate a disconnect/reacquire: Reset() restarts from WarmingUp,
    // even though it is called at a "later" wall-clock time than before.
    tracker.Reset(T(10000), true);
    REQUIRE(tracker.CurrentState() == ReadinessState::WarmingUp);
    REQUIRE(tracker.Update(T(10050), steady) == ReadinessState::WarmingUp);
}

TEST_CASE("DeviceReadinessTracker: Reset with layoutApplied=false returns to Unconfigured", "[DeviceReadinessTracker]") {
    const DeviceReadinessPolicy policy{};
    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);
    REQUIRE(tracker.CurrentState() == ReadinessState::WarmingUp);

    tracker.Reset(T(1), false);
    REQUIRE(tracker.CurrentState() == ReadinessState::Unconfigured);
}

// Compact synthetic fixture based on the verified G923 capture (see
// docs/hardware/G923_DIRECTINPUT_CAPTURE.md): for ~2.05s after acquire, all
// three pedal channels read 0.499992 simultaneously, then settle. This
// checks that a profile-supplied minimumWarmup of 2200ms (comfortably past
// the observed ~2050ms transient) keeps the tracker in WarmingUp through
// the whole transient -- readiness must never be satisfied by value
// stability alone before minimumWarmup elapses -- and only reaches Ready
// once the settled value has actually held steady for stableSample.
TEST_CASE("DeviceReadinessTracker: G923-like startup transient never reaches Ready during the transient",
          "[DeviceReadinessTracker][G923]") {
    DeviceReadinessPolicy policy{};
    policy.minimumWarmup = std::chrono::milliseconds{2200};
    policy.stableSample = std::chrono::milliseconds{250};
    policy.maximumWait = std::chrono::milliseconds{5000};

    DeviceReadinessTracker tracker(policy);
    tracker.Reset(T(0), true);

    ReadinessAxisSample transient{};
    transient.throttle = 0.499992f;
    transient.brake = 0.499992f;
    transient.clutch = 0.499992f;
    transient.hasClutch = true;

    for (long long ms = 0; ms <= 2033; ms += 17) {
        REQUIRE(tracker.Update(T(ms), transient) == ReadinessState::WarmingUp);
    }

    ReadinessAxisSample settledReleased{};
    settledReleased.throttle = 1.0f;
    settledReleased.brake = 1.0f;
    settledReleased.clutch = 1.0f;
    settledReleased.hasClutch = true;

    // The real capture settles at elapsed=2051ms. minimumWarmup (2200ms)
    // has not elapsed yet, so this must still be WarmingUp, not Ready.
    REQUIRE(tracker.Update(T(2051), settledReleased) == ReadinessState::WarmingUp);

    // Past minimumWarmup: enters Stabilizing on the settled value.
    REQUIRE(tracker.Update(T(2201), settledReleased) == ReadinessState::Stabilizing);
    // Holds steady for stableSample (250ms) -> Ready.
    REQUIRE(tracker.Update(T(2452), settledReleased) == ReadinessState::Ready);
}
