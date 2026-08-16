#pragma once

#include <chrono>
#include <optional>

#include "rvwheel/dal/IWheelDevice.hpp"
#include "rvwheel/dal/Status.hpp"
#include "rvwheel/ffb/ForceFeedbackEngine.hpp"
#include "rvwheel/ffb/ForceFeedbackTypes.hpp"

namespace rvwheel::tools::probe {

// What actually happened the one time Stop() performed real work (repeat
// calls return this same, already-decided result without touching the
// device again -- see BridgeForceFeedbackSession::Stop()).
struct BridgeForceFeedbackStopResult {
    // Whether the safety controller's own final tick emitted a stopDevice
    // decision (ForceFeedbackDecision::stopDevice) -- edge-triggered, so
    // false here can simply mean an earlier tick already consumed that
    // edge, not that anything is wrong.
    bool engineStopRequested = false;

    // Result of the explicit, belt-and-suspenders device_.StopForceFeedback()
    // call Stop() always makes on top of whatever the engine's own decision
    // did. This is the one call site whose Status this component can
    // observe directly, so it is the authoritative answer to "was the stop
    // confirmed" -- see Confirmed().
    rvwheel::dal::Status explicitStopStatus = rvwheel::dal::Status::Ok();

    // Result of the ownership-session teardown. For DirectInput this is
    // where the pre-session DIPROP_AUTOCENTER value is restored and the
    // exclusive acquisition is released. Backends with no separate
    // lifecycle return Ok through IWheelDevice's default implementation.
    rvwheel::dal::Status sessionEndStatus = rvwheel::dal::Status::Ok();

    [[nodiscard]] bool Confirmed() const noexcept {
        return explicitStopStatus.IsOk() && sessionEndStatus.IsOk();
    }
};

// Owns the ForceFeedbackEngine lifecycle for one `--bridge --enable-force-feedback`
// run. Composes the same ForceFeedbackSafetyController/ForceFeedbackMixer
// already validated by the gated hardware tests -- nothing here
// reimplements clamping, watchdog, or stop logic. Exists so RunBridge()
// does not need to know engine wiring, and so the lifecycle invariants
// (enable only once explicitly armed, stop exactly once no matter how the
// run ends, never silently re-arm after a fault) are unit-testable with a
// fake device instead of real hardware.
//
// Which spring source the engine uses is decided once, at construction,
// from `config.speedSensitiveSpring.enabled`:
//   - false (the default): SpringDamperSource, exactly as before this
//     field existed -- a static, telemetry-independent spring/damper.
//   - true: SpeedSensitiveSpringSource, which scales the same spring by
//     vehicle speed. This class itself never reads a file or parses RVT1;
//     the caller (RunBridge) is responsible for supplying an
//     already-validated VehicleTelemetry via TickWithTelemetry(), built
//     from tools/device_probe/VehicleTelemetryTransport's
//     FreshVehicleTelemetrySample. This session never knows about
//     filesystem or RVT1 parsing.
// This is a construction-time choice, not a per-tick one: a session built
// with enabled=false can only ever call the static Tick(now) path below,
// so it can never end up depending on telemetry it was never given.
class BridgeForceFeedbackSession {
public:
    BridgeForceFeedbackSession(rvwheel::dal::IWheelDevice& device, const rvwheel::ffb::ForceFeedbackConfig& config);
    ~BridgeForceFeedbackSession();

    BridgeForceFeedbackSession(const BridgeForceFeedbackSession&) = delete;
    BridgeForceFeedbackSession& operator=(const BridgeForceFeedbackSession&) = delete;
    BridgeForceFeedbackSession(BridgeForceFeedbackSession&&) = delete;
    BridgeForceFeedbackSession& operator=(BridgeForceFeedbackSession&&) = delete;

    // Arms the session. No-op if the config's own `enabled` flag is false
    // (the profile-side gate) -- this method does not re-check that flag
    // itself, ForceFeedbackSafetyController::Enable() already does.
    [[nodiscard]] rvwheel::dal::Status Enable() noexcept;

    // Static-spring path (config.speedSensitiveSpring.enabled == false,
    // including the case where the whole forceFeedback block is absent).
    // Feeds the device's own current steering into the engine with an
    // empty VehicleTelemetry and `now` as both the "telemetry" and current
    // timestamps, so SpringDamperSource -- which needs neither -- is never
    // starved by a watchdog with nothing to feed it. Never reads any
    // telemetry source, by construction. Safe to call even before Enable()
    // or after Stop().
    void Tick(std::chrono::steady_clock::time_point now);

    // Adaptive-spring path (config.speedSensitiveSpring.enabled == true).
    // `telemetry` must already be fully validated by the caller -- not a
    // baseline/leftover frame, and not invalid/non-local -- e.g. built via
    // VehicleTelemetryTransport::ToVehicleTelemetry() from a
    // FreshVehicleTelemetrySample; pass std::nullopt when no such sample is
    // currently available (this session then only lets the watchdog/ramp
    // logic run, exactly like ForceFeedbackEngine::TickWithoutTelemetry).
    // `telemetryTimestamp` must be the sample's own capture time (e.g.
    // FreshVehicleTelemetrySample::receivedAt) -- never `now` -- so a stale
    // sample is caught by the safety controller's own watchdog exactly like
    // any other stale telemetry; it is ignored when `telemetry` is
    // std::nullopt. Safe to call even before Enable() or after Stop().
    void TickWithTelemetry(std::chrono::steady_clock::time_point now,
                            const std::optional<rvwheel::ffb::VehicleTelemetry>& telemetry,
                            std::chrono::steady_clock::time_point telemetryTimestamp);

    // Idempotent: safe to call multiple times, from any state, including
    // before Enable() was ever called. Emits an immediate stop through the
    // safety controller and, belt-and-suspenders, calls the device's own
    // StopForceFeedback() directly -- and, unlike a discarded void result,
    // returns what actually happened so a caller can confirm the stop
    // rather than merely hope for it. The first real call's result is
    // cached and returned again by any later call (which does not repeat
    // the device I/O).
    BridgeForceFeedbackStopResult Stop() noexcept;

    [[nodiscard]] bool IsFaulted() const noexcept;
    [[nodiscard]] rvwheel::ffb::ForceFeedbackState State() const noexcept;
    [[nodiscard]] rvwheel::ffb::ForceFeedbackDiagnostics Diagnostics(std::chrono::steady_clock::time_point now) const noexcept;

private:
    rvwheel::dal::IWheelDevice& device_;
    rvwheel::ffb::ForceFeedbackEngine engine_;
    bool configEnabled_ = false;
    bool ownershipSessionBegun_ = false;
    bool stopped_ = false;
    BridgeForceFeedbackStopResult lastStopResult_;
};

} // namespace rvwheel::tools::probe
