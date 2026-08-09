#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "rvwheel/dal/IWheelDevice.hpp"

namespace rvwheel::testing {

// Minimal IWheelDevice test double. Deliberately does not touch any real
// backend; used to test DeviceManager's refresh/dedup/preservation policy
// AND the force feedback engine/safety controller without hardware.
//
// Every ApplyForceFeedback/StopForceFeedback call is recorded (parameters,
// call count, ordering via the shared `callLog`), and both can be made to
// fail on demand via `nextApplyForceFeedbackFailure`/
// `nextStopForceFeedbackFailure`, so a test can assert exactly what the
// force feedback engine sent to "hardware" and how it reacts to an
// injected backend failure -- this class never starts a real DirectInput
// effect, so nothing here can ever apply real force.
class FakeWheelDevice final : public rvwheel::dal::IWheelDevice {
public:
    explicit FakeWheelDevice(rvwheel::dal::DeviceInfo info) : info_(std::move(info)) {
        state_.connected = true;
        state_.valid = true;
        state_.readiness = rvwheel::dal::ReadinessState::Ready;
    }

    [[nodiscard]] const rvwheel::dal::DeviceInfo& Info() const noexcept override { return info_; }
    [[nodiscard]] bool IsConnected() const noexcept override { return state_.connected; }

    rvwheel::dal::Status Poll() noexcept override {
        ++pollCount;
        state_.sampleCounter = static_cast<std::uint64_t>(pollCount);
        return rvwheel::dal::Status::Ok();
    }

    [[nodiscard]] const rvwheel::dal::WheelState& State() const noexcept override { return state_; }

    rvwheel::dal::Status ApplyLayout(const rvwheel::dal::WheelInputLayout&, const rvwheel::dal::DeviceReadinessPolicy&) noexcept override {
        ++applyLayoutCallCount;
        return rvwheel::dal::Status::Ok();
    }

    rvwheel::dal::Status ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand& command) noexcept override {
        ++forceFeedbackCallCount;
        appliedCommands.push_back(command);
        callLog.push_back("ApplyForceFeedback");
        if (nextApplyForceFeedbackFailure) {
            const rvwheel::dal::Status failure = *nextApplyForceFeedbackFailure;
            nextApplyForceFeedbackFailure.reset();
            return failure;
        }
        return rvwheel::dal::Status::Ok();
    }

    rvwheel::dal::Status StopForceFeedback() noexcept override {
        ++stopForceFeedbackCallCount;
        callLog.push_back("StopForceFeedback");
        if (nextStopForceFeedbackFailure) {
            const rvwheel::dal::Status failure = *nextStopForceFeedbackFailure;
            nextStopForceFeedbackFailure.reset();
            return failure;
        }
        return rvwheel::dal::Status::Ok();
    }

    int pollCount = 0;
    int forceFeedbackCallCount = 0;
    int stopForceFeedbackCallCount = 0;
    int applyLayoutCallCount = 0;

    std::vector<rvwheel::dal::ForceFeedbackCommand> appliedCommands;
    std::vector<const char*> callLog;

    std::optional<rvwheel::dal::Status> nextApplyForceFeedbackFailure;
    std::optional<rvwheel::dal::Status> nextStopForceFeedbackFailure;

private:
    rvwheel::dal::DeviceInfo info_;
    rvwheel::dal::WheelState state_{};
};

} // namespace rvwheel::testing
