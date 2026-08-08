#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "rvwheel/dal/DeviceManager.hpp"
#include "support/FakeDeviceEnumerator.hpp"
#include "support/FakeWheelDevice.hpp"

using rvwheel::dal::DeviceBackend;
using rvwheel::dal::DeviceId;
using rvwheel::dal::DeviceInfo;
using rvwheel::dal::DeviceManager;
using rvwheel::dal::IDeviceEnumerator;
using rvwheel::dal::IWheelDevice;
using rvwheel::testing::FakeDeviceEnumerator;

namespace {

// A clock DeviceManager can be handed directly (it stores Clock as
// std::function<time_point()>); tests advance it explicitly instead of
// depending on real elapsed wall-clock time.
class ManualClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point Now() const noexcept { return now_; }
    void Advance(std::chrono::milliseconds delta) noexcept { now_ += delta; }

private:
    std::chrono::steady_clock::time_point now_{};
};

DeviceInfo MakeInfo(std::uint64_t idValue, DeviceBackend backend, std::optional<std::uint16_t> vid = std::nullopt,
                     std::optional<std::uint16_t> pid = std::nullopt) {
    DeviceInfo info{};
    info.id = DeviceId::FromValue(idValue);
    info.name = "Fake Wheel";
    info.backend = backend;
    info.vendorId = vid;
    info.productId = pid;
    return info;
}

} // namespace

TEST_CASE("DeviceManager: refresh interval policy", "[DeviceManager][Refresh]") {
    ManualClock clock;

    auto enumeratorOwned = std::make_unique<FakeDeviceEnumerator>(std::vector<DeviceInfo>{MakeInfo(1, DeviceBackend::DirectInput)});
    FakeDeviceEnumerator* enumerator = enumeratorOwned.get();

    std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators;
    enumerators.push_back(std::move(enumeratorOwned));

    DeviceManager manager(std::move(enumerators), std::chrono::milliseconds{5000}, [&clock] { return clock.Now(); });

    SECTION("no enumeration happens before the first RefreshIfDue call") { REQUIRE(enumerator->enumerateCallCount == 0); }

    SECTION("the first RefreshIfDue call always enumerates") {
        REQUIRE(manager.RefreshIfDue().IsOk());
        REQUIRE(enumerator->enumerateCallCount == 1);
        REQUIRE(manager.DeviceCount() == 1);
    }

    SECTION("no re-enumeration before the interval elapses") {
        REQUIRE(manager.RefreshIfDue().IsOk());
        REQUIRE(enumerator->enumerateCallCount == 1);

        clock.Advance(std::chrono::milliseconds{4999});
        REQUIRE(manager.RefreshIfDue().IsOk());
        REQUIRE(enumerator->enumerateCallCount == 1);
    }

    SECTION("re-enumerates once the interval elapses") {
        REQUIRE(manager.RefreshIfDue().IsOk());
        REQUIRE(enumerator->enumerateCallCount == 1);

        clock.Advance(std::chrono::milliseconds{5000});
        REQUIRE(manager.RefreshIfDue().IsOk());
        REQUIRE(enumerator->enumerateCallCount == 2);
    }

    SECTION("ForceRefresh bypasses the interval entirely") {
        REQUIRE(manager.RefreshIfDue().IsOk());
        REQUIRE(enumerator->enumerateCallCount == 1);
        REQUIRE(manager.ForceRefresh().IsOk());
        REQUIRE(enumerator->enumerateCallCount == 2);
    }
}

TEST_CASE("DeviceManager: preserves the existing instance for an already-known DeviceId", "[DeviceManager][Refresh][Preservation]") {
    ManualClock clock;

    auto enumeratorOwned = std::make_unique<FakeDeviceEnumerator>(std::vector<DeviceInfo>{MakeInfo(42, DeviceBackend::DirectInput)});
    FakeDeviceEnumerator* enumerator = enumeratorOwned.get();

    std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators;
    enumerators.push_back(std::move(enumeratorOwned));

    DeviceManager manager(std::move(enumerators), std::chrono::milliseconds{5000}, [&clock] { return clock.Now(); });

    REQUIRE(manager.RefreshIfDue().IsOk());
    REQUIRE(manager.DeviceCount() == 1);
    const IWheelDevice* firstInstance = *manager.Devices().begin();

    clock.Advance(std::chrono::milliseconds{5000});
    REQUIRE(manager.RefreshIfDue().IsOk());
    REQUIRE(enumerator->enumerateCallCount == 2); // The enumerator DID create a new object...
    REQUIRE(manager.DeviceCount() == 1);
    const IWheelDevice* secondInstance = *manager.Devices().begin();

    // ...but DeviceManager recognized the same DeviceId and kept the
    // original instance rather than swapping in the enumerator's new one.
    REQUIRE(firstInstance == secondInstance);
}

TEST_CASE("DeviceManager: dedup prefers Logitech over DirectInput for the same VID/PID", "[DeviceManager][Dedup]") {
    ManualClock clock;

    auto directInputEnum = std::make_unique<FakeDeviceEnumerator>(
        std::vector<DeviceInfo>{MakeInfo(1, DeviceBackend::DirectInput, std::uint16_t{0x046D}, std::uint16_t{0xC24F})});
    auto logitechEnum = std::make_unique<FakeDeviceEnumerator>(
        std::vector<DeviceInfo>{MakeInfo(2, DeviceBackend::Logitech, std::uint16_t{0x046D}, std::uint16_t{0xC24F})});

    std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators;
    // Registered with DirectInput first on purpose: dedup preference is
    // backend-aware, not registration-order-aware.
    enumerators.push_back(std::move(directInputEnum));
    enumerators.push_back(std::move(logitechEnum));

    DeviceManager manager(std::move(enumerators), std::chrono::milliseconds{5000}, [&clock] { return clock.Now(); });

    REQUIRE(manager.ForceRefresh().IsOk());
    REQUIRE(manager.DeviceCount() == 1);

    const IWheelDevice* survivor = *manager.Devices().begin();
    REQUIRE(survivor->Info().backend == DeviceBackend::Logitech);
}

TEST_CASE("DeviceManager: devices without both VID and PID are never merged", "[DeviceManager][Dedup]") {
    ManualClock clock;

    auto enumA = std::make_unique<FakeDeviceEnumerator>(std::vector<DeviceInfo>{MakeInfo(1, DeviceBackend::DirectInput)});
    auto enumB = std::make_unique<FakeDeviceEnumerator>(std::vector<DeviceInfo>{MakeInfo(2, DeviceBackend::Logitech)});

    std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators;
    enumerators.push_back(std::move(enumA));
    enumerators.push_back(std::move(enumB));

    DeviceManager manager(std::move(enumerators), std::chrono::milliseconds{5000}, [&clock] { return clock.Now(); });

    REQUIRE(manager.ForceRefresh().IsOk());
    REQUIRE(manager.DeviceCount() == 2);
}
