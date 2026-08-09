#pragma once

#include <chrono>

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

    [[nodiscard]] bool Confirmed() const noexcept { return explicitStopStatus.IsOk(); }
};

// Owns the ForceFeedbackEngine lifecycle for one `--bridge --enable-force-feedback`
// run. Composes the same ForceFeedbackSafetyController/ForceFeedbackMixer/
// SpringDamperSource already validated by the gated hardware tests --
// nothing here reimplements clamping, watchdog, or stop logic. Exists so
// RunBridge() does not need to know engine wiring, and so the lifecycle
// invariants (enable only once explicitly armed, stop exactly once no
// matter how the run ends, never silently re-arm after a fault) are
// unit-testable with a fake device instead of real hardware.
//
// Only ever uses `config`'s springStrength/damperStrength through
// SpringDamperSource; no telemetry-derived source is added here on
// purpose -- vehicle telemetry is not part of this integration.
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
    void Enable() noexcept;

    // Call once per bridge tick while this session exists. Feeds the
    // device's own current steering into the engine with an empty
    // VehicleTelemetry (this integration has none) and `now` as both the
    // "telemetry" and current timestamps, so SpringDamperSource -- which
    // needs neither -- is never starved by a watchdog with nothing to feed
    // it. Safe to call even before Enable() or after Stop().
    void Tick(std::chrono::steady_clock::time_point now);

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
    bool stopped_ = false;
    BridgeForceFeedbackStopResult lastStopResult_;
};

} // namespace rvwheel::tools::probe
