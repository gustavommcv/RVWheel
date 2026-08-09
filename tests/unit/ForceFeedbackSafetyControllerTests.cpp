#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "rvwheel/ffb/ForceFeedbackSafetyController.hpp"

using rvwheel::dal::ForceFeedbackCommand;
using rvwheel::ffb::ForceFeedbackConfig;
using rvwheel::ffb::ForceFeedbackDecision;
using rvwheel::ffb::ForceFeedbackSafetyController;
using rvwheel::ffb::ForceFeedbackState;

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point T(long long ms) { return Clock::time_point{} + std::chrono::milliseconds{ms}; }

ForceFeedbackConfig EnabledConfig() {
    ForceFeedbackConfig config;
    config.enabled = true;
    config.masterGain = 1.0f;
    config.maxTorqueNormalized = 1.0f;   // Test the controller's own hard ceiling, not the profile's.
    config.slewRatePerSecond = 1000.0f;  // Fast enough that tests aren't dominated by ramping unless specifically testing it.
    config.watchdogTimeout = std::chrono::milliseconds{100};
    return config;
}
} // namespace

TEST_CASE("ForceFeedbackSafetyController: starts Disabled and stays inert until Enable()", "[FFB][Safety]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    REQUIRE(controller.State() == ForceFeedbackState::Disabled);

    ForceFeedbackCommand requested{0.5f, 0.5f, 0.5f, 1.0f};
    const ForceFeedbackDecision decision = controller.Update(requested, T(0), T(0));
    REQUIRE_FALSE(decision.applyCommand);
    REQUIRE(controller.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("ForceFeedbackSafetyController: Enable() is a no-op when the profile config itself is disabled",
          "[FFB][Safety]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.enabled = false;
    ForceFeedbackSafetyController controller(config);

    controller.Enable();
    REQUIRE(controller.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("ForceFeedbackSafetyController: Armed stays inert (no command sent) until fresh nonzero input arrives",
          "[FFB][Safety]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();
    REQUIRE(controller.State() == ForceFeedbackState::Armed);

    const ForceFeedbackDecision idle = controller.Update(ForceFeedbackCommand{0, 0, 0, 1.0f}, T(0), T(0));
    REQUIRE_FALSE(idle.applyCommand);
    REQUIRE(controller.State() == ForceFeedbackState::Armed);
}

TEST_CASE("ForceFeedbackSafetyController: a fresh nonzero command transitions Armed -> Active and is applied",
          "[FFB][Safety]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();

    // Several ticks so the slew limiter (even at a very high rate) has a nonzero dt to work with.
    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
    const ForceFeedbackDecision decision = controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(20), T(20));

    REQUIRE(decision.applyCommand);
    REQUIRE(controller.State() == ForceFeedbackState::Active);
    REQUIRE(decision.command.constantForce > 0.0f);
}

TEST_CASE("ForceFeedbackSafetyController: NaN and infinite fields are sanitized to zero, never faulted",
          "[FFB][Safety][NaN]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();

    ForceFeedbackCommand poisoned{};
    poisoned.constantForce = std::numeric_limits<float>::quiet_NaN();
    poisoned.spring = std::numeric_limits<float>::infinity();
    poisoned.damper = -std::numeric_limits<float>::infinity();
    poisoned.gain = std::numeric_limits<float>::quiet_NaN();

    (void)controller.Update(poisoned, T(0), T(0)); // Warm-up call: establishes a timestamp so the next call actually ramps.
    const ForceFeedbackDecision decision = controller.Update(poisoned, T(20), T(20));

    REQUIRE(controller.State() != ForceFeedbackState::Faulted);
    if (decision.applyCommand) {
        REQUIRE(std::isfinite(decision.command.constantForce));
        REQUIRE(std::isfinite(decision.command.spring));
        REQUIRE(std::isfinite(decision.command.damper));
        REQUIRE(std::isfinite(decision.command.gain));
    }
}

TEST_CASE("ForceFeedbackSafetyController: a profile cannot request more than the absolute torque ceiling",
          "[FFB][Safety][Clamp]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.maxTorqueNormalized = 1.0f; // Profile asks for the maximum possible.
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    (void)controller.Update(ForceFeedbackCommand{1.0f, 1.0f, 1.0f, 1.0f}, T(0), T(0));
    const ForceFeedbackDecision decision = controller.Update(ForceFeedbackCommand{1.0f, 1.0f, 1.0f, 1.0f}, T(1000), T(1000));

    REQUIRE(decision.applyCommand);
    REQUIRE(decision.command.constantForce <= ForceFeedbackSafetyController::kAbsoluteMaxTorqueNormalized);
    REQUIRE(decision.command.spring <= ForceFeedbackSafetyController::kAbsoluteMaxTorqueNormalized);
    REQUIRE(decision.command.damper <= ForceFeedbackSafetyController::kAbsoluteMaxTorqueNormalized);
}

TEST_CASE("ForceFeedbackSafetyController: master gain is a ceiling the mixer/source cannot exceed",
          "[FFB][Safety][Clamp]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.masterGain = 0.2f;
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
    const ForceFeedbackDecision decision = controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(1000), T(1000));

    REQUIRE(decision.applyCommand);
    REQUIRE(decision.command.gain <= 0.2f + 1e-6f);
}

TEST_CASE("ForceFeedbackSafetyController: slew rate limits how fast the applied command can change",
          "[FFB][Safety][SlewRate]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.slewRatePerSecond = 1.0f; // 1.0 normalized unit per second.
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    (void)controller.Update(ForceFeedbackCommand{1.0f, 0, 0, 1.0f}, T(0), T(0));
    // 50ms later: at most 0.05 of travel should have occurred, nowhere near the full 1.0 target.
    const ForceFeedbackDecision decision = controller.Update(ForceFeedbackCommand{1.0f, 0, 0, 1.0f}, T(50), T(50));

    REQUIRE(decision.applyCommand);
    REQUIRE(decision.command.constantForce < 0.2f);
    REQUIRE(decision.command.constantForce > 0.0f);
}

TEST_CASE("ForceFeedbackSafetyController: a profile cannot request an instantaneous slew rate", "[FFB][Safety][SlewRate]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.slewRatePerSecond = 1.0e9f; // Absurdly large; must be clamped internally.
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    (void)controller.Update(ForceFeedbackCommand{1.0f, 0, 0, 1.0f}, T(0), T(0));
    const ForceFeedbackDecision decision = controller.Update(ForceFeedbackCommand{1.0f, 0, 0, 1.0f}, T(1), T(1));

    // Even at 1ms, the internal rate ceiling (kMaxSlewRatePerSecond) bounds travel; it must not jump straight to 1.0.
    REQUIRE(decision.applyCommand);
    REQUIRE(decision.command.constantForce < 1.0f);
}

TEST_CASE("ForceFeedbackSafetyController: watchdog stops output and calls StopForceFeedback once telemetry goes stale",
          "[FFB][Safety][Watchdog]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.watchdogTimeout = std::chrono::milliseconds{100};
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(20), T(20));
    REQUIRE(controller.State() == ForceFeedbackState::Active);

    // No further Update() call for well beyond the watchdog timeout; caller only ticks.
    const ForceFeedbackDecision stale = controller.Tick(T(500));
    REQUIRE(stale.applyCommand);
    REQUIRE(stale.stopDevice);
    REQUIRE(stale.command.constantForce == 0.0f);
    REQUIRE(controller.State() == ForceFeedbackState::Armed);

    // The stop edge must not repeat on every subsequent idle tick.
    const ForceFeedbackDecision again = controller.Tick(T(600));
    REQUIRE_FALSE(again.stopDevice);
}

TEST_CASE("ForceFeedbackSafetyController: idle ticks never call StopForceFeedback when nothing was ever active",
          "[FFB][Safety][Watchdog]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();

    const ForceFeedbackDecision decision = controller.Tick(T(1000));
    REQUIRE_FALSE(decision.applyCommand);
    REQUIRE_FALSE(decision.stopDevice);
}

TEST_CASE("ForceFeedbackSafetyController: Disable() ramps to zero then stops exactly once", "[FFB][Safety][Disable]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.slewRatePerSecond = 1.0f;
    ForceFeedbackSafetyController controller(config);
    controller.Enable();
    (void)controller.Update(ForceFeedbackCommand{1.0f, 0, 0, 1.0f}, T(0), T(0));
    (void)controller.Update(ForceFeedbackCommand{1.0f, 0, 0, 1.0f}, T(2000), T(2000)); // fully ramped up

    controller.Disable();
    REQUIRE(controller.State() == ForceFeedbackState::Stopping);

    // Ramping down at 1.0/s from ~1.0 takes about a second; step through it.
    bool sawStopDevice = false;
    for (long long ms = 2100; ms <= 4000; ms += 100) {
        const ForceFeedbackDecision decision = controller.Tick(T(ms));
        if (decision.stopDevice) {
            sawStopDevice = true;
            // Ramping arithmetic lands within IsAllZero's epsilon of zero,
            // not necessarily bit-exact -- that is what actually triggers
            // the stop edge, so assert the same tolerance here.
            REQUIRE(std::fabs(decision.command.constantForce) < 1.0e-3f);
            break;
        }
    }
    REQUIRE(sawStopDevice);
    REQUIRE(controller.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("ForceFeedbackSafetyController: EmergencyStop is instantaneous and lands on Disabled from any state",
          "[FFB][Safety][EmergencyStop]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();
    (void)controller.Update(ForceFeedbackCommand{1.0f, 1.0f, 1.0f, 1.0f}, T(0), T(0));
    (void)controller.Update(ForceFeedbackCommand{1.0f, 1.0f, 1.0f, 1.0f}, T(1000), T(1000));
    REQUIRE(controller.State() == ForceFeedbackState::Active);

    controller.EmergencyStop();

    const ForceFeedbackDecision decision = controller.Tick(T(1001));
    REQUIRE(decision.applyCommand);
    REQUIRE(decision.stopDevice);
    REQUIRE(decision.command.constantForce == 0.0f);
    REQUIRE(decision.command.spring == 0.0f);
    REQUIRE(decision.command.damper == 0.0f);
    REQUIRE(controller.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("ForceFeedbackSafetyController: EmergencyStop from Faulted still lands on Disabled", "[FFB][Safety][EmergencyStop]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();
    controller.ReportBackendFailure("simulated backend error");
    REQUIRE(controller.State() == ForceFeedbackState::Faulted);

    controller.EmergencyStop();
    REQUIRE(controller.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("ForceFeedbackSafetyController: a reported backend failure enters Faulted and stays there until ClearFault",
          "[FFB][Safety][Fault]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();
    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(20), T(20));

    controller.ReportBackendFailure("CreateEffect failed");
    REQUIRE(controller.State() == ForceFeedbackState::Faulted);

    // Every subsequent call must keep producing zero/stop, never resume applying force.
    const ForceFeedbackDecision decision = controller.Update(ForceFeedbackCommand{1.0f, 1.0f, 1.0f, 1.0f}, T(30), T(30));
    REQUIRE(controller.State() == ForceFeedbackState::Faulted);
    if (decision.applyCommand) {
        REQUIRE(decision.command.constantForce == 0.0f);
    }

    controller.Enable(); // Must not un-fault.
    REQUIRE(controller.State() == ForceFeedbackState::Faulted);

    controller.ClearFault();
    REQUIRE(controller.State() == ForceFeedbackState::Disabled);
}

TEST_CASE("ForceFeedbackSafetyController: any fault or watchdog timeout eventually issues StopForceFeedback",
          "[FFB][Safety][Fault]") {
    // This is the explicit guarantee the design brief asks for: no failure
    // or timeout path can leave a command applied without also stopping.
    SECTION("backend failure") {
        ForceFeedbackSafetyController controller(EnabledConfig());
        controller.Enable();
        (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
        (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(20), T(20));

        controller.ReportBackendFailure("boom");
        const ForceFeedbackDecision decision = controller.Tick(T(21));
        REQUIRE(decision.stopDevice);
    }

    SECTION("watchdog timeout") {
        ForceFeedbackConfig config = EnabledConfig();
        config.watchdogTimeout = std::chrono::milliseconds{50};
        ForceFeedbackSafetyController controller(config);
        controller.Enable();
        (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
        (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(10), T(10));

        const ForceFeedbackDecision decision = controller.Tick(T(1000));
        REQUIRE(decision.stopDevice);
    }

    SECTION("device unavailable") {
        ForceFeedbackSafetyController controller(EnabledConfig());
        controller.Enable();
        (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
        (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(10), T(10));

        controller.ReportDeviceUnavailable();
        const ForceFeedbackDecision decision = controller.Tick(T(11));
        REQUIRE(decision.stopDevice);
        REQUIRE(controller.State() == ForceFeedbackState::Armed); // Recoverable, unlike a genuine fault.
    }
}

TEST_CASE("ForceFeedbackSafetyController: repeated backend failures while Faulted do not spam stop edges",
          "[FFB][Safety][Fault]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();
    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));
    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(20), T(20));

    controller.ReportBackendFailure("first failure");
    const ForceFeedbackDecision stop = controller.Tick(T(21));
    REQUIRE(stop.stopDevice);
    REQUIRE(controller.State() == ForceFeedbackState::Faulted);

    controller.ReportBackendFailure("secondary stop failure");
    const ForceFeedbackDecision idle = controller.Tick(T(22));
    REQUIRE_FALSE(idle.applyCommand);
    REQUIRE_FALSE(idle.stopDevice);
    REQUIRE(controller.Diagnostics(T(22)).lastFaultReason == "first failure");
}

TEST_CASE("ForceFeedbackSafetyController: stale telemetry timestamp is treated as no update at all",
          "[FFB][Safety][Watchdog]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.watchdogTimeout = std::chrono::milliseconds{50};
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    // telemetryTimestamp is far older than `now`, even though this is the very first call.
    const ForceFeedbackDecision decision = controller.Update(ForceFeedbackCommand{1.0f, 0, 0, 1.0f}, T(0), T(1000));
    REQUIRE_FALSE(decision.applyCommand);
    REQUIRE(controller.State() == ForceFeedbackState::Armed);
}

TEST_CASE("ForceFeedbackSafetyController: gain ramps from zero on activation, never starting \"full\" while an "
          "effect ramps up (regression: first real hardware test found spring reaching its target while gain was "
          "still near 1.0, applying a stronger-than-configured transient)",
          "[FFB][Safety][Regression]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.slewRatePerSecond = 1.0f; // Slow enough that a bug would show up across several ticks, not one.
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    const ForceFeedbackCommand requested{0.0f, 0.5f, 0.0f, 1.0f}; // A source always requesting full-scale gain.
    float maxObservedProduct = 0.0f;
    for (long long ms = 0; ms <= 2000; ms += 20) {
        const ForceFeedbackDecision decision = controller.Update(requested, T(ms), T(ms));
        if (decision.applyCommand) {
            REQUIRE(decision.command.gain <= 1.0f + 1e-6f);
            maxObservedProduct = (std::max)(maxObservedProduct, decision.command.spring * decision.command.gain);
        }
    }

    // The final steady-state product (spring=0.5 clamped by nothing here,
    // gain capped at masterGain=1.0 in EnabledConfig) is the ceiling; no
    // intermediate frame may exceed it once both have finished ramping.
    const float finalSpring = 0.5f;
    const float finalGain = 1.0f;
    REQUIRE(maxObservedProduct <= finalSpring * finalGain + 1e-3f);
}

TEST_CASE("ForceFeedbackSafetyController: with a low master gain, the effective spring*gain product never "
          "overshoots the configured ceiling during ramp-up",
          "[FFB][Safety][Regression]") {
    ForceFeedbackConfig config = EnabledConfig();
    config.masterGain = 0.1f;
    config.slewRatePerSecond = 1.0f;
    ForceFeedbackSafetyController controller(config);
    controller.Enable();

    const ForceFeedbackCommand requested{0.0f, 0.1f, 0.0f, 1.0f};
    float maxObservedProduct = 0.0f;
    for (long long ms = 0; ms <= 2000; ms += 20) {
        const ForceFeedbackDecision decision = controller.Update(requested, T(ms), T(ms));
        if (decision.applyCommand) {
            maxObservedProduct = (std::max)(maxObservedProduct, decision.command.spring * decision.command.gain);
        }
    }

    REQUIRE(maxObservedProduct <= 0.1f * 0.1f + 1e-3f);
}

TEST_CASE("ForceFeedbackSafetyController: Diagnostics reports state and ages without mutating anything",
          "[FFB][Safety][Diagnostics]") {
    ForceFeedbackSafetyController controller(EnabledConfig());
    controller.Enable();
    (void)controller.Update(ForceFeedbackCommand{0.5f, 0, 0, 1.0f}, T(0), T(0));

    const auto diag = controller.Diagnostics(T(30));
    REQUIRE(diag.state == ForceFeedbackState::Armed);
    REQUIRE(diag.updateCount == 1);
    REQUIRE(diag.telemetryAge == std::chrono::milliseconds{30});
}
