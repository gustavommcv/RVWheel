#include "BridgeForceFeedbackSession.hpp"

#include <memory>
#include <utility>

#include "rvwheel/ffb/ForceFeedbackMixer.hpp"
#include "rvwheel/ffb/ForceFeedbackSafetyController.hpp"
#include "rvwheel/ffb/IForceFeedbackSource.hpp"
#include "rvwheel/ffb/SpringDamperSource.hpp"

namespace rvwheel::tools::probe {

namespace {

[[nodiscard]] rvwheel::ffb::ForceFeedbackEngine BuildEngine(const rvwheel::ffb::ForceFeedbackConfig& config) {
    std::vector<std::unique_ptr<rvwheel::ffb::IForceFeedbackSource>> sources;
    sources.push_back(std::make_unique<rvwheel::ffb::SpringDamperSource>(config));
    return rvwheel::ffb::ForceFeedbackEngine(rvwheel::ffb::ForceFeedbackSafetyController(config),
                                              rvwheel::ffb::ForceFeedbackMixer{}, std::move(sources));
}

} // namespace

BridgeForceFeedbackSession::BridgeForceFeedbackSession(rvwheel::dal::IWheelDevice& device,
                                                         const rvwheel::ffb::ForceFeedbackConfig& config)
    : device_(device), engine_(BuildEngine(config)) {}

BridgeForceFeedbackSession::~BridgeForceFeedbackSession() {
    // Best-effort: the destructor cannot report a failure to anyone, but
    // RunBridge's own explicit Stop() call (which runs first on every
    // normal exit path) already captured and reported the observable
    // result -- see RunBridge().
    static_cast<void>(Stop());
}

void BridgeForceFeedbackSession::Enable() noexcept { engine_.Enable(); }

void BridgeForceFeedbackSession::Tick(std::chrono::steady_clock::time_point now) {
    if (stopped_) {
        return;
    }
    const rvwheel::ffb::VehicleTelemetry telemetry{};
    engine_.Tick(device_, telemetry, device_.State().steering, now, now);
}

BridgeForceFeedbackStopResult BridgeForceFeedbackSession::Stop() noexcept {
    if (stopped_) {
        return lastStopResult_;
    }
    stopped_ = true;
    engine_.EmergencyStop();
    const rvwheel::ffb::ForceFeedbackDecision decision =
        engine_.TickWithoutTelemetry(device_, std::chrono::steady_clock::now());
    // Belt-and-suspenders explicit device-wide stop, matching the gated
    // hardware test's own shutdown sequence -- harmless to call even if
    // the engine's own stop decision already reached the device. This is
    // the one Status this component can observe directly (the engine's own
    // internal StopForceFeedback call, if any, is not exposed by
    // ForceFeedbackEngine), so it is what Confirmed() reports on.
    const rvwheel::dal::Status explicitStopStatus = device_.StopForceFeedback();
    lastStopResult_ = BridgeForceFeedbackStopResult{decision.stopDevice, explicitStopStatus};
    return lastStopResult_;
}

bool BridgeForceFeedbackSession::IsFaulted() const noexcept {
    return engine_.State() == rvwheel::ffb::ForceFeedbackState::Faulted;
}

rvwheel::ffb::ForceFeedbackState BridgeForceFeedbackSession::State() const noexcept { return engine_.State(); }

rvwheel::ffb::ForceFeedbackDiagnostics BridgeForceFeedbackSession::Diagnostics(
    std::chrono::steady_clock::time_point now) const noexcept {
    return engine_.Diagnostics(now);
}

} // namespace rvwheel::tools::probe
