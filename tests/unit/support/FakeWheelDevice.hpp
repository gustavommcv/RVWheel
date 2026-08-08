#pragma once

#include <utility>

#include "rvwheel/dal/IWheelDevice.hpp"

namespace rvwheel::testing {

// Minimal IWheelDevice test double. Deliberately does not touch any real
// backend; used to test DeviceManager's refresh/dedup/preservation policy
// without hardware.
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

    rvwheel::dal::Status ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand&) noexcept override {
        ++forceFeedbackCallCount;
        return rvwheel::dal::Status::Ok();
    }

    rvwheel::dal::Status StopForceFeedback() noexcept override { return rvwheel::dal::Status::Ok(); }

    int pollCount = 0;
    int forceFeedbackCallCount = 0;
    int applyLayoutCallCount = 0;

private:
    rvwheel::dal::DeviceInfo info_;
    rvwheel::dal::WheelState state_{};
};

} // namespace rvwheel::testing
