#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "rvwheel/devices/LogitechDevice.hpp"
#include "support/FakeLogitechSdk.hpp"

using rvwheel::dal::DeviceBackend;
using rvwheel::dal::WheelDeviceCapabilities;
using rvwheel::dal::DeviceInfo;
using rvwheel::dal::ForceFeedbackCommand;
using rvwheel::dal::StatusCode;
using rvwheel::devices::LogitechDevice;
using rvwheel::testing::FakeLogitechSdk;

namespace {

DeviceInfo MakeInfo(WheelDeviceCapabilities caps) {
    DeviceInfo info{};
    info.name = "Fake Logitech Wheel";
    info.backend = DeviceBackend::Logitech;
    info.capabilities = caps;
    return info;
}

} // namespace

TEST_CASE("LogitechDevice: Poll copies state and masks unsupported axes to zero", "[LogitechDevice][Poll]") {
    auto sdk = std::make_shared<FakeLogitechSdk>();
    FakeLogitechSdk::SlotConfig slot{};
    slot.connected = true;
    slot.state.connected = true;
    slot.state.steering = 0.5f;
    slot.state.throttle = 0.8f;
    slot.state.brake = 0.3f; // The device below reports hasBrake = false; must be masked to 0.
    slot.state.clutch = 0.1f;
    sdk->slots.push_back(slot);

    WheelDeviceCapabilities caps{};
    caps.hasSteering = true;
    caps.hasThrottle = true;
    caps.hasBrake = false;
    caps.hasClutch = true;

    LogitechDevice device(MakeInfo(caps), sdk, 0, [](auto, auto) {});

    const auto status = device.Poll();
    REQUIRE(status.IsOk());
    REQUIRE(device.IsConnected());
    REQUIRE(device.State().steering == Catch::Approx(0.5f));
    REQUIRE(device.State().throttle == Catch::Approx(0.8f));
    REQUIRE(device.State().brake == Catch::Approx(0.0f));
    REQUIRE(device.State().clutch == Catch::Approx(0.1f));
    REQUIRE(device.State().sampleCounter == 1);
}

TEST_CASE("LogitechDevice: Poll reports NotConnected when the wheel itself is disconnected", "[LogitechDevice][Poll]") {
    auto sdk = std::make_shared<FakeLogitechSdk>();
    FakeLogitechSdk::SlotConfig slot{};
    slot.connected = true;      // SDK slot is reachable...
    slot.state.connected = false; // ...but the wheel reports itself disconnected.
    sdk->slots.push_back(slot);

    LogitechDevice device(MakeInfo(WheelDeviceCapabilities{}), sdk, 0, [](auto, auto) {});

    const auto status = device.Poll();
    REQUIRE(status.Code() == StatusCode::NotConnected);
    REQUIRE_FALSE(device.IsConnected());
}

TEST_CASE("LogitechDevice: ApplyForceFeedback is NotSupported without the capability", "[LogitechDevice][ForceFeedback]") {
    auto sdk = std::make_shared<FakeLogitechSdk>();
    FakeLogitechSdk::SlotConfig slot{};
    slot.connected = true;
    slot.state.connected = true;
    sdk->slots.push_back(slot);

    WheelDeviceCapabilities caps{};
    caps.hasForceFeedback = false;

    LogitechDevice device(MakeInfo(caps), sdk, 0, [](auto, auto) {});
    REQUIRE(device.Poll().IsOk());

    const auto status = device.ApplyForceFeedback(ForceFeedbackCommand{});
    REQUIRE(status.Code() == StatusCode::NotSupported);
    REQUIRE(sdk->constantForceCallCount == 0);
}

TEST_CASE("LogitechDevice: ApplyForceFeedback reports unsupported components but still applies the rest",
          "[LogitechDevice][ForceFeedback]") {
    auto sdk = std::make_shared<FakeLogitechSdk>();
    FakeLogitechSdk::SlotConfig slot{};
    slot.connected = true;
    slot.state.connected = true;
    sdk->slots.push_back(slot);
    sdk->springSupported = false;

    WheelDeviceCapabilities caps{};
    caps.hasForceFeedback = true;

    LogitechDevice device(MakeInfo(caps), sdk, 0, [](auto, auto) {});
    REQUIRE(device.Poll().IsOk());

    ForceFeedbackCommand command{};
    command.constantForce = 0.75f;
    command.spring = 0.5f;
    command.gain = 1.0f;

    const auto status = device.ApplyForceFeedback(command);
    REQUIRE(status.Code() == StatusCode::NotSupported);
    REQUIRE(sdk->constantForceCallCount == 1);
    REQUIRE(sdk->lastConstantForce == Catch::Approx(0.75f));
}

TEST_CASE("LogitechDevice: StopForceFeedback delegates to the SDK", "[LogitechDevice][ForceFeedback]") {
    auto sdk = std::make_shared<FakeLogitechSdk>();
    FakeLogitechSdk::SlotConfig slot{};
    slot.connected = true;
    slot.state.connected = true;
    sdk->slots.push_back(slot);

    LogitechDevice device(MakeInfo(WheelDeviceCapabilities{}), sdk, 0, [](auto, auto) {});
    REQUIRE(device.Poll().IsOk());
    REQUIRE(device.StopForceFeedback().IsOk());
    REQUIRE(sdk->stopAllCallCount == 1);
}
