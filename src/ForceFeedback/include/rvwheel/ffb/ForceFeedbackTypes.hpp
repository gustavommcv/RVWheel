#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::ffb {

// Vehicle-side facts a telemetry source can supply to compute force
// feedback. Deliberately game/engine-agnostic: nothing here mentions
// Unreal, AVS, or Lua. A field is std::nullopt when the source could not
// obtain it (e.g. the game does not expose it, or it has not been verified
// safe to read yet -- see docs/research/FORCE_FEEDBACK_FEASIBILITY.md).
// Consumers must treat a missing field as "unknown", never as zero.
struct VehicleTelemetry {
    std::optional<float> speedMetersPerSecond;
    std::optional<float> steeringNormalized;  // [-1, 1], same convention as WheelState::steering.
    std::optional<float> lateralVelocityMetersPerSecond;
    std::optional<float> yawRateRadiansPerSecond;
    std::optional<bool> isLocallyControlled; // False for a remote/AI-driven vehicle in multiplayer.

    std::chrono::steady_clock::time_point timestamp{};

    [[nodiscard]] bool HasAnyData() const noexcept {
        return speedMetersPerSecond || steeringNormalized || lateralVelocityMetersPerSecond || yawRateRadiansPerSecond;
    }
};

// Opt-in speed-dependent scaling of the centering spring: at rest the
// spring is `minimumScale` of the profile's springStrength, ramping via a
// smoothstep curve (never a step) up to full springStrength at
// fullStrengthSpeedMetersPerSecond and beyond -- see SpeedSensitiveSpringSource.
// Absent from a profile, or enabled=false, preserves the exact static
// SpringDamperSource behavior; this struct's own defaults keep it inert.
struct SpeedSensitiveSpringConfig {
    bool enabled = false;
    float minimumScale = 0.25f;                    // [0, 1] fraction of springStrength applied at zero speed.
    float fullStrengthSpeedMetersPerSecond = 5.0f;  // (0, kMaxFullStrengthSpeedMetersPerSecond]; conservative, not a real top speed.

    // Sanity ceiling for fullStrengthSpeedMetersPerSecond -- generous
    // enough for any reasonable "full strength" threshold, documented as
    // conservative rather than physically exact, the same way this
    // project's other numeric domains are (see e.g. VehicleTelemetryTransport.cpp's
    // kMaxPlausibleSpeedMetersPerSecond).
    static constexpr float kMaxFullStrengthSpeedMetersPerSecond = 50.0f;
};

// Per-profile force feedback tuning and safety limits. Every field defaults
// to "off"/conservative so a profile that omits this block entirely, or an
// older profile written before this field existed, behaves identically to
// FFB being disabled. See ADDING_A_WHEEL.md / FORCE_FEEDBACK.md for what
// each field means to a player; ProfileLoader enforces the numeric ranges
// documented here.
struct ForceFeedbackConfig {
    bool enabled = false;         // Master switch. False is always the safe default.
    float masterGain = 0.0f;      // [0, 1]. Independent of `enabled` so a saved "off" profile never carries a hot gain.
    bool invertDirection = false;

    float springStrength = 0.0f; // [0, 1] centering spring.
    float damperStrength = 0.0f; // [0, 1] velocity damping.
    float selfAligningTorqueStrength = 0.0f; // [0, 1]; inert until a telemetry source provides real data.

    float maxTorqueNormalized = 0.3f; // Absolute per-command ceiling, [0, 1]; conservative, not 1.0.
    float deadband = 0.0f;            // [0, 1] steering deadband before any centering force applies.
    float slewRatePerSecond = 2.0f;   // Max normalized-force change per second the safety controller allows.

    std::chrono::milliseconds watchdogTimeout{200}; // How long a command/telemetry sample stays "fresh".

    SpeedSensitiveSpringConfig speedSensitiveSpring; // Opt-in; defaults to inert (enabled=false).
};

enum class ForceFeedbackState : std::uint8_t {
    Disabled, // No force ever leaves the safety controller. The only state a fresh controller starts in.
    Armed,    // Enabled, but waiting for a fresh, valid input before producing nonzero output.
    Active,   // Producing a nonzero (post-clamp) command.
    Stopping, // Ramping output down to zero after Disable/watchdog/fault; transient.
    Faulted,  // A backend failure was reported; stays here until ClearFault() is called explicitly.
};

[[nodiscard]] constexpr const char* ToString(ForceFeedbackState state) noexcept {
    switch (state) {
        case ForceFeedbackState::Disabled: return "Disabled";
        case ForceFeedbackState::Armed: return "Armed";
        case ForceFeedbackState::Active: return "Active";
        case ForceFeedbackState::Stopping: return "Stopping";
        case ForceFeedbackState::Faulted: return "Faulted";
    }
    return "Unknown";
}

// Snapshot for inspection/diagnostics (--ffb-simulate, --list, logs). Never
// used internally to make control decisions -- it is a read-only view of
// what the safety controller most recently did.
struct ForceFeedbackDiagnostics {
    ForceFeedbackState state = ForceFeedbackState::Disabled;
    rvwheel::dal::ForceFeedbackCommand lastAppliedCommand{};
    std::chrono::milliseconds telemetryAge{0};
    std::chrono::milliseconds commandAge{0};
    std::uint64_t updateCount = 0;
    std::uint64_t watchdogStopCount = 0;
    std::string lastFaultReason;
};

} // namespace rvwheel::ffb
