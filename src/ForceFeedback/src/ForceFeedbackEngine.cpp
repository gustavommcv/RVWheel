#include "rvwheel/ffb/ForceFeedbackEngine.hpp"

namespace rvwheel::ffb {

namespace {
using rvwheel::dal::Status;
using rvwheel::dal::StatusCode;
} // namespace

ForceFeedbackEngine::ForceFeedbackEngine(ForceFeedbackSafetyController safetyController, ForceFeedbackMixer mixer,
                                          std::vector<std::unique_ptr<IForceFeedbackSource>> sources)
    : safetyController_(std::move(safetyController)), mixer_(std::move(mixer)), sources_(std::move(sources)) {}

void ForceFeedbackEngine::Configure(const ForceFeedbackConfig& config) noexcept { safetyController_.Configure(config); }

ForceFeedbackDecision ForceFeedbackEngine::Tick(rvwheel::dal::IWheelDevice& device, const VehicleTelemetry& telemetry,
                                                 float wheelSteering, Clock::time_point telemetryTimestamp,
                                                 Clock::time_point now) {
    std::vector<rvwheel::dal::ForceFeedbackCommand> contributions;
    contributions.reserve(sources_.size());
    for (const auto& source : sources_) {
        contributions.push_back(source->Compute(telemetry, wheelSteering));
    }
    const rvwheel::dal::ForceFeedbackCommand mixed = mixer_.Mix(contributions);
    const ForceFeedbackDecision decision = safetyController_.Update(mixed, telemetryTimestamp, now);
    return ApplyDecision(device, decision);
}

ForceFeedbackDecision ForceFeedbackEngine::TickWithoutTelemetry(rvwheel::dal::IWheelDevice& device,
                                                                 Clock::time_point now) {
    const ForceFeedbackDecision decision = safetyController_.Tick(now);
    return ApplyDecision(device, decision);
}

ForceFeedbackDecision ForceFeedbackEngine::ApplyDecision(rvwheel::dal::IWheelDevice& device,
                                                           const ForceFeedbackDecision& decision) {
    if (decision.applyCommand) {
        const Status status = device.ApplyForceFeedback(decision.command);
        if (status.Code() == StatusCode::BackendError) {
            safetyController_.ReportBackendFailure(status.Message());
        } else if (status.Code() == StatusCode::NotConnected) {
            safetyController_.ReportDeviceUnavailable();
        }
    }
    if (decision.stopDevice) {
        const Status stopStatus = device.StopForceFeedback();
        if (stopStatus.Code() == StatusCode::BackendError) {
            safetyController_.ReportBackendFailure(stopStatus.Message());
        }
    }
    return decision;
}

} // namespace rvwheel::ffb
