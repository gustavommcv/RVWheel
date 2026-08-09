#pragma once

#include <chrono>
#include <cmath>
#include <optional>

#include "rvwheel/dal/DeviceReadinessPolicy.hpp"
#include "rvwheel/dal/ReadinessState.hpp"

namespace rvwheel::dal {

// Snapshot of the normalized axes readiness judges stability against.
// Independent of AxisRole naming so it works whether a device has 2, 3,
// or 4 relevant axes; hasClutch lets a 2-pedal device skip clutch
// stability entirely rather than being judged against a value that is
// always 0 and trivially "stable".
struct ReadinessAxisSample {
    float steering = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    float clutch = 0.0f;
    bool hasClutch = false;
};

// Pure, clock-injectable state machine: no hardware, no Win32, no I/O.
// One instance per connected device; the owning backend calls Reset()
// whenever a layout is (re)applied or the device reacquires after a
// disconnect, and Update() once per successful poll.
//
// This class intentionally never reads a raw pedal value and calls it
// "released" -- readiness is judged purely by policy (elapsed time,
// then a window of low variance), never by comparing against a
// hardcoded midpoint or endpoint. That is what lets the exact same logic
// correctly reject the G923's real ~2.05s startup transient (which is
// stable-looking but arrives too early, before minimumWarmup elapses)
// without encoding "0.499992" anywhere in generic code.
class DeviceReadinessTracker {
public:
    explicit DeviceReadinessTracker(DeviceReadinessPolicy policy) noexcept : policy_(policy) {}

    // Starts (or restarts) the clock from `now`, moving to WarmingUp if a
    // layout is considered applied, or leaving/returning to Unconfigured
    // otherwise. Called on initial layout application and on every
    // detected reconnect.
    void Reset(std::chrono::steady_clock::time_point now, bool layoutApplied) noexcept {
        state_ = layoutApplied ? (policy_.requireAxisActivation ? ReadinessState::AwaitingActivation
                                                                : ReadinessState::WarmingUp)
                               : ReadinessState::Unconfigured;
        startTime_ = now;
        activationBaseline_.reset();
        stableWindowStart_.reset();
        stableWindowBaseline_.reset();
    }

    void SetPolicy(DeviceReadinessPolicy policy) noexcept { policy_ = policy; }
    [[nodiscard]] const DeviceReadinessPolicy& Policy() const noexcept { return policy_; }

    // Advances the state machine using the current normalized sample.
    // Safe to call every poll; a no-op once in Ready/TimedOut/Unconfigured
    // until Reset() is called again.
    [[nodiscard]] ReadinessState Update(std::chrono::steady_clock::time_point now, const ReadinessAxisSample& sample) noexcept {
        if (state_ == ReadinessState::Unconfigured || state_ == ReadinessState::Ready || state_ == ReadinessState::TimedOut) {
            return state_;
        }
        if (!startTime_) {
            startTime_ = now; // Defensive: Update() called before any Reset().
        }

        if (state_ == ReadinessState::AwaitingActivation) {
            if (!activationBaseline_) {
                activationBaseline_ = sample;
                return state_;
            }
            if (!HasActivationMovement(sample, *activationBaseline_)) {
                return state_;
            }

            // Start normal warmup only after the first genuine physical
            // report. This prevents a stable driver placeholder from
            // becoming Ready and does not punish a user who leaves the
            // wheel untouched at a menu.
            state_ = ReadinessState::WarmingUp;
            startTime_ = now;
            stableWindowStart_.reset();
            stableWindowBaseline_.reset();
            return state_;
        }

        const auto elapsedSinceStart = now - *startTime_;
        if (elapsedSinceStart >= policy_.maximumWait) {
            state_ = ReadinessState::TimedOut;
            return state_;
        }

        if (state_ == ReadinessState::WarmingUp) {
            if (elapsedSinceStart >= policy_.minimumWarmup) {
                state_ = ReadinessState::Stabilizing;
                stableWindowStart_ = now;
                stableWindowBaseline_ = sample;
            }
            return state_;
        }

        // Stabilizing.
        if (!stableWindowBaseline_.has_value() || !WithinTolerance(sample, *stableWindowBaseline_)) {
            stableWindowStart_ = now;
            stableWindowBaseline_ = sample;
            return state_;
        }
        if (now - *stableWindowStart_ >= policy_.stableSample) {
            state_ = ReadinessState::Ready;
        }
        return state_;
    }

    [[nodiscard]] ReadinessState CurrentState() const noexcept { return state_; }

private:
    [[nodiscard]] bool HasActivationMovement(const ReadinessAxisSample& a,
                                             const ReadinessAxisSample& b) const noexcept {
        const float threshold = policy_.activationThreshold;
        if (std::abs(a.steering - b.steering) >= threshold) return true;
        if (std::abs(a.throttle - b.throttle) >= threshold) return true;
        if (std::abs(a.brake - b.brake) >= threshold) return true;
        if (a.hasClutch && b.hasClutch && std::abs(a.clutch - b.clutch) >= threshold) return true;
        return false;
    }

    [[nodiscard]] bool WithinTolerance(const ReadinessAxisSample& a, const ReadinessAxisSample& b) const noexcept {
        const float tol = policy_.stabilityTolerance;
        if (std::abs(a.steering - b.steering) > tol) return false;
        if (std::abs(a.throttle - b.throttle) > tol) return false;
        if (std::abs(a.brake - b.brake) > tol) return false;
        if (a.hasClutch && b.hasClutch && std::abs(a.clutch - b.clutch) > tol) return false;
        return true;
    }

    DeviceReadinessPolicy policy_;
    ReadinessState state_ = ReadinessState::Unconfigured;
    std::optional<std::chrono::steady_clock::time_point> startTime_;
    std::optional<ReadinessAxisSample> activationBaseline_;
    std::optional<std::chrono::steady_clock::time_point> stableWindowStart_;
    std::optional<ReadinessAxisSample> stableWindowBaseline_;
};

} // namespace rvwheel::dal
