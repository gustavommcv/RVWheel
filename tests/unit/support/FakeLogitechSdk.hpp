#pragma once

#include <optional>
#include <vector>

#include "rvwheel/devices/ILogitechSdk.hpp"

namespace rvwheel::testing {

// Fake ILogitechSdk implementation used to exercise LogitechDevice without
// the real, proprietary SDK. Every "slot" mimics one wheel index as the
// real SDK would report it.
class FakeLogitechSdk final : public rvwheel::devices::ILogitechSdk {
public:
    struct SlotConfig {
        bool connected = true;
        rvwheel::devices::LogitechDeviceIdentity identity;
        rvwheel::dal::WheelDeviceCapabilities capabilities;
        rvwheel::devices::LogitechRawWheelState state;
    };

    rvwheel::dal::Status Initialize() override {
        ++initializeCallCount;
        initialized_ = initializeShouldSucceed;
        return initializeShouldSucceed ? rvwheel::dal::Status::Ok() : rvwheel::dal::Status::BackendError("fake init failure");
    }

    void Shutdown() noexcept override {
        ++shutdownCallCount;
        initialized_ = false;
    }

    [[nodiscard]] std::size_t DeviceCount() const override { return slots.size(); }

    [[nodiscard]] bool IsConnected(std::size_t index) const override { return index < slots.size() && slots[index].connected; }

    [[nodiscard]] std::optional<rvwheel::devices::LogitechDeviceIdentity> GetIdentity(std::size_t index) const override {
        if (index >= slots.size()) {
            return std::nullopt;
        }
        return slots[index].identity;
    }

    [[nodiscard]] std::optional<rvwheel::dal::WheelDeviceCapabilities> GetCapabilities(std::size_t index) const override {
        if (index >= slots.size()) {
            return std::nullopt;
        }
        return slots[index].capabilities;
    }

    rvwheel::dal::Status GetState(std::size_t index, rvwheel::devices::LogitechRawWheelState& outState) const override {
        if (index >= slots.size()) {
            return rvwheel::dal::Status::NotConnected("fake: no such slot");
        }
        outState = slots[index].state;
        return rvwheel::dal::Status::Ok();
    }

    rvwheel::dal::Status PlayConstantForce(std::size_t /*index*/, float value) override {
        ++constantForceCallCount;
        lastConstantForce = value;
        return constantForceSupported ? rvwheel::dal::Status::Ok() : rvwheel::dal::Status::NotSupported("fake: constant force unsupported");
    }

    rvwheel::dal::Status PlaySpringForce(std::size_t /*index*/, float value) override {
        lastSpring = value;
        return springSupported ? rvwheel::dal::Status::Ok() : rvwheel::dal::Status::NotSupported("fake: spring unsupported");
    }

    rvwheel::dal::Status PlayDamperForce(std::size_t /*index*/, float value) override {
        lastDamper = value;
        return damperSupported ? rvwheel::dal::Status::Ok() : rvwheel::dal::Status::NotSupported("fake: damper unsupported");
    }

    rvwheel::dal::Status SetGain(std::size_t /*index*/, float value) override {
        lastGain = value;
        return rvwheel::dal::Status::Ok();
    }

    rvwheel::dal::Status StopAllForces(std::size_t /*index*/) override {
        ++stopAllCallCount;
        return rvwheel::dal::Status::Ok();
    }

    std::vector<SlotConfig> slots;
    bool initializeShouldSucceed = true;
    bool constantForceSupported = true;
    bool springSupported = true;
    bool damperSupported = true;

    int initializeCallCount = 0;
    int shutdownCallCount = 0;
    int constantForceCallCount = 0;
    int stopAllCallCount = 0;
    float lastConstantForce = 0.0f;
    float lastSpring = 0.0f;
    float lastDamper = 0.0f;
    float lastGain = 0.0f;

private:
    bool initialized_ = false;
};

} // namespace rvwheel::testing
