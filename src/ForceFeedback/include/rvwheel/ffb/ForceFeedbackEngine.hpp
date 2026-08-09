#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "rvwheel/dal/IWheelDevice.hpp"
#include "rvwheel/ffb/ForceFeedbackMixer.hpp"
#include "rvwheel/ffb/ForceFeedbackSafetyController.hpp"
#include "rvwheel/ffb/IForceFeedbackSource.hpp"

namespace rvwheel::ffb {

// Ties the whole force feedback pipeline together for one device: gathers
// every source's contribution, mixes them, runs the result through the
// safety controller, and only then (if the controller says so) calls
// IWheelDevice::ApplyForceFeedback/StopForceFeedback. This is the one class
// that knows about all the other pieces; sources and the mixer never see a
// device, and the safety controller never sees a source.
//
// A backend failure or disconnection reported by the device is fed straight
// back into the safety controller (ReportBackendFailure/
// ReportDeviceUnavailable), so a real hardware error always reaches the
// same fail-safe path a watchdog timeout does.
class ForceFeedbackEngine {
public:
    using Clock = std::chrono::steady_clock;

    ForceFeedbackEngine(ForceFeedbackSafetyController safetyController, ForceFeedbackMixer mixer,
                         std::vector<std::unique_ptr<IForceFeedbackSource>> sources);

    // One tick with fresh vehicle telemetry available. `telemetryTimestamp`
    // is when that telemetry was captured (not `now`); a stale timestamp is
    // handled by the safety controller exactly like no telemetry at all.
    ForceFeedbackDecision Tick(rvwheel::dal::IWheelDevice& device, const VehicleTelemetry& telemetry,
                                float wheelSteering, Clock::time_point telemetryTimestamp, Clock::time_point now);

    // One tick with no new telemetry (e.g. the telemetry channel is stale
    // or absent entirely). Only lets the watchdog/ramp-down logic run.
    ForceFeedbackDecision TickWithoutTelemetry(rvwheel::dal::IWheelDevice& device, Clock::time_point now);

    void Enable() noexcept { safetyController_.Enable(); }
    void Disable() noexcept { safetyController_.Disable(); }
    void EmergencyStop() noexcept { safetyController_.EmergencyStop(); }
    void ClearFault() noexcept { safetyController_.ClearFault(); }

    // Updates the safety controller's limits/gain/watchdog only. Sources
    // that need their own copy of the profile config (e.g. SpringDamperSource)
    // must be reconfigured by the caller directly -- the engine holds them
    // as IForceFeedbackSource, which has no Configure() of its own on
    // purpose, since not every source needs runtime reconfiguration.
    void Configure(const ForceFeedbackConfig& config) noexcept;

    [[nodiscard]] ForceFeedbackState State() const noexcept { return safetyController_.State(); }
    [[nodiscard]] ForceFeedbackDiagnostics Diagnostics(Clock::time_point now) const noexcept {
        return safetyController_.Diagnostics(now);
    }

private:
    ForceFeedbackDecision ApplyDecision(rvwheel::dal::IWheelDevice& device, const ForceFeedbackDecision& decision);

    ForceFeedbackSafetyController safetyController_;
    ForceFeedbackMixer mixer_;
    std::vector<std::unique_ptr<IForceFeedbackSource>> sources_;
};

} // namespace rvwheel::ffb
