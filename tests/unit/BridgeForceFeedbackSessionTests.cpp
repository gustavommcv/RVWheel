#include <catch2/catch_test_macros.hpp>

#include "BridgeForceFeedbackSession.hpp"
#include "support/FakeWheelDevice.hpp"

using rvwheel::dal::DeviceId;
using rvwheel::dal::DeviceInfo;
using rvwheel::dal::Status;
using rvwheel::dal::StatusCode;
using rvwheel::ffb::ForceFeedbackConfig;
using rvwheel::ffb::ForceFeedbackState;
using rvwheel::testing::FakeWheelDevice;

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point T(long long ms) { return Clock::time_point{} + std::chrono::milliseconds{ms}; }

DeviceInfo MakeFakeInfo() {
    DeviceInfo info;
    info.id = DeviceId::FromValue(1);
    info.name = "Fake Bridge FFB Test Device";
    info.capabilities.hasForceFeedback = true;
    return info;
}

ForceFeedbackConfig ValidatedConfig() {
    // The exact values physically validated in
    // docs/FORCE_FEEDBACK_HARDWARE_TEST.md's background-mode runs.
    ForceFeedbackConfig config;
    config.enabled = true;
    config.masterGain = 0.2f;
    config.springStrength = 0.2f;
    config.damperStrength = 0.0f;
    config.maxTorqueNormalized = 0.2f;
    config.slewRatePerSecond = 1000.0f; // Fast so tests aren't dominated by ramping.
    config.watchdogTimeout = std::chrono::milliseconds{200};
    return config;
}
} // namespace

TEST_CASE("BridgeForceFeedbackSession: never applies force before Enable() is called", "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    session.Tick(T(0));
    session.Tick(T(20));

    REQUIRE(device.forceFeedbackCallCount == 0);
}

TEST_CASE("BridgeForceFeedbackSession: with enabled=false in config, Enable() never arms it", "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    ForceFeedbackConfig disabled = ValidatedConfig();
    disabled.enabled = false;
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, disabled);

    REQUIRE(session.Enable().IsOk());
    session.Tick(T(0));
    session.Tick(T(20));

    REQUIRE(device.forceFeedbackCallCount == 0);
    REQUIRE(device.beginForceFeedbackSessionCallCount == 0);
    REQUIRE(session.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("BridgeForceFeedbackSession: once enabled, applies the profile's own spring configuration",
          "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    REQUIRE(session.Enable().IsOk());
    REQUIRE(session.Enable().IsOk()); // Idempotent ownership preparation.
    session.Tick(T(0));
    session.Tick(T(20));

    REQUIRE(device.forceFeedbackCallCount >= 1);
    REQUIRE(device.beginForceFeedbackSessionCallCount == 1);
    REQUIRE(device.appliedCommands.back().spring > 0.0f);
    REQUIRE(device.appliedCommands.back().damper == 0.0f);
}

TEST_CASE("BridgeForceFeedbackSession: Stop() is idempotent, confirms the stop, and is safe even if never enabled",
          "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    const auto first = session.Stop();
    // EmergencyStop() unconditionally schedules a stop decision on the very
    // next tick regardless of prior state, so ForceFeedbackEngine's own
    // internal ApplyDecision may call StopForceFeedback() once on top of
    // Stop()'s own explicit follow-up call -- >= 1, not necessarily == 1.
    const int callCountAfterFirstStop = device.stopForceFeedbackCallCount;
    REQUIRE(callCountAfterFirstStop >= 1);

    const auto second = session.Stop();
    const auto third = session.Stop();

    REQUIRE(device.stopForceFeedbackCallCount == callCountAfterFirstStop); // Repeat calls never re-touch the device.
    REQUIRE(first.Confirmed());
    REQUIRE(first.explicitStopStatus.IsOk());
    // Later calls return the same cached result, not a fresh (misleadingly
    // "fine") default -- proving Stop() does not silently discard state.
    REQUIRE(second.Confirmed() == first.Confirmed());
    REQUIRE(third.Confirmed() == first.Confirmed());
}

TEST_CASE("BridgeForceFeedbackSession: destructor stops the device even without an explicit Stop() call",
          "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    {
        rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());
        REQUIRE(session.Enable().IsOk());
        session.Tick(T(0));
        session.Tick(T(20));
    }
    REQUIRE(device.stopForceFeedbackCallCount >= 1);
    REQUIRE(device.endForceFeedbackSessionCallCount == 1);
}

TEST_CASE("BridgeForceFeedbackSession: a backend failure faults the session and stops the device, "
          "with no automatic re-arming",
          "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    REQUIRE(session.Enable().IsOk());
    session.Tick(T(0));
    device.nextApplyForceFeedbackFailure = Status::BackendError("simulated DIERR_NOTEXCLUSIVEACQUIRED");
    session.Tick(T(20));

    REQUIRE(session.IsFaulted());

    // Further ticks must never resume applying real force on their own.
    for (long long ms = 40; ms <= 200; ms += 20) {
        session.Tick(T(ms));
        REQUIRE(session.IsFaulted());
        if (!device.appliedCommands.empty()) {
            REQUIRE(device.appliedCommands.back().spring == 0.0f);
            REQUIRE(device.appliedCommands.back().damper == 0.0f);
        }
    }
}

TEST_CASE("BridgeForceFeedbackSession: a disconnect is recoverable, not a permanent fault", "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    REQUIRE(session.Enable().IsOk());
    session.Tick(T(0));
    device.nextApplyForceFeedbackFailure = Status::NotConnected("simulated disconnect");
    session.Tick(T(20));

    REQUIRE_FALSE(session.IsFaulted());
}

TEST_CASE("BridgeForceFeedbackSession: Stop() after a fault still calls StopForceFeedback and confirms it",
          "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    REQUIRE(session.Enable().IsOk());
    session.Tick(T(0));
    device.nextApplyForceFeedbackFailure = Status::BackendError("simulated failure");
    session.Tick(T(20));
    REQUIRE(session.IsFaulted());

    const auto stopResult = session.Stop();
    REQUIRE(device.stopForceFeedbackCallCount >= 1);
    REQUIRE(stopResult.Confirmed());
}

TEST_CASE("BridgeForceFeedbackSession: Stop() reports an unconfirmed result when the device's own "
          "StopForceFeedback() fails",
          "[Bridge][FFB]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    REQUIRE(session.Enable().IsOk());
    session.Tick(T(0));
    // Persistent (not single-shot): Stop()'s own internal engine-driven
    // StopForceFeedback() call may reach the device before Stop()'s
    // explicit follow-up call does, so the failure must still be in effect
    // for that second call too -- see FakeWheelDevice::StopForceFeedback().
    device.persistentStopForceFeedbackFailure = Status::BackendError("simulated stop failure");

    const auto stopResult = session.Stop();

    REQUIRE_FALSE(stopResult.Confirmed());
    REQUIRE(stopResult.explicitStopStatus.Code() == StatusCode::BackendError);
}

TEST_CASE("BridgeForceFeedbackSession: Begin failure leaves the engine disabled and never applies force",
          "[Bridge][FFB][Lifecycle]") {
    FakeWheelDevice device(MakeFakeInfo());
    device.nextBeginForceFeedbackSessionFailure = Status::BackendError("simulated autocenter preparation failure");
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());

    const Status enableStatus = session.Enable();
    session.Tick(T(20));

    REQUIRE(enableStatus.Code() == StatusCode::BackendError);
    REQUIRE(session.State() == ForceFeedbackState::Disabled);
    REQUIRE(device.beginForceFeedbackSessionCallCount == 1);
    REQUIRE(device.forceFeedbackCallCount == 0);
}

TEST_CASE("BridgeForceFeedbackSession: End failure makes final stop unconfirmed and stays idempotent",
          "[Bridge][FFB][Lifecycle]") {
    FakeWheelDevice device(MakeFakeInfo());
    rvwheel::tools::probe::BridgeForceFeedbackSession session(device, ValidatedConfig());
    REQUIRE(session.Enable().IsOk());
    device.persistentEndForceFeedbackSessionFailure = Status::BackendError("simulated autocenter restore failure");

    const auto first = session.Stop();
    const auto second = session.Stop();

    REQUIRE_FALSE(first.Confirmed());
    REQUIRE(first.explicitStopStatus.IsOk());
    REQUIRE(first.sessionEndStatus.Code() == StatusCode::BackendError);
    REQUIRE(device.endForceFeedbackSessionCallCount == 1);
    REQUIRE(second.sessionEndStatus.Code() == first.sessionEndStatus.Code());
}
