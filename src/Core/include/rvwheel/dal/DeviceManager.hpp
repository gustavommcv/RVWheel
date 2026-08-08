#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <vector>

#include "rvwheel/dal/Diagnostics.hpp"
#include "rvwheel/dal/IWheelDevice.hpp"
#include "rvwheel/dal/Status.hpp"

namespace rvwheel::dal {

// Produces a fresh batch of device handles on demand. Each call to
// Enumerate() is expected to (re-)query the underlying driver/SDK, which is
// exactly why DeviceManager only calls it at most once per refresh interval
// rather than on every Poll(). Implementations are free to create brand new
// IWheelDevice instances on every call; DeviceManager is responsible for
// recognizing devices it has already seen (by DeviceId) and discarding the
// duplicate rather than the enumerator having to track that itself.
class IDeviceEnumerator {
public:
    virtual ~IDeviceEnumerator() = default;

    [[nodiscard]] virtual std::vector<std::unique_ptr<IWheelDevice>> Enumerate() = 0;
};

// Owns and refreshes the set of known wheel devices across all configured
// backends. Does not spawn any thread: RefreshIfDue() is expected to be
// called from the host's game/update loop, and only performs the (possibly
// expensive) backend enumeration when the configured interval has elapsed.
// Poll()-ing individual devices for per-frame input is unrelated to
// refreshing the device list and is done directly on the IWheelDevice
// instances returned by Devices().
//
// Handle validity policy: once a device has been discovered, it remains in
// this manager's collection (and its address stays stable) for the lifetime
// of the DeviceManager instance, even if a later refresh no longer reports
// it. Disconnection is signaled through IWheelDevice::IsConnected() /
// WheelState::connected (driven by the device's own Poll() failing against
// the backend), not by removing it from the collection. This trades
// unbounded growth over very long sessions with many different physical
// devices for simple, dangling-free references — acceptable for the
// single/few-wheel setups this DAL targets. See DeviceManager.cpp for the
// full rationale and the dedup policy across backends.
class DeviceManager {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    static constexpr std::chrono::milliseconds kDefaultRefreshInterval{5000};

    // enumerators: one per backend, in descending dedup priority (an
    //   enumerator earlier in this list does not automatically win dedup;
    //   see DeviceManager.cpp — priority is currently backend-specific:
    //   Logitech is preferred over DirectInput for the same physical
    //   device whenever both report it).
    // refreshInterval: minimum time between two calls to any enumerator's
    //   Enumerate(). Configurable so tests can use a short/zero interval.
    // clock: injectable time source for deterministic tests.
    explicit DeviceManager(std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators,
                            std::chrono::milliseconds refreshInterval = kDefaultRefreshInterval,
                            Clock clock = &std::chrono::steady_clock::now,
                            DiagnosticSink diagnostics = NullDiagnosticSink());

    // Copying is already impossible (it owns vector<unique_ptr<...>>
    // members) without an explicit deletion; leaving copy/move otherwise
    // untouched keeps the implicitly-generated move constructor/assignment
    // available, which callers rely on nowhere today but which costs
    // nothing to keep.

    // Enumerates immediately regardless of the configured interval and
    // resets the interval timer. Intended for an explicit "rescan now"
    // action; the normal per-frame path should use RefreshIfDue().
    Status ForceRefresh();

    // Enumerates only if at least refreshInterval has elapsed since the
    // last (successful or not) refresh, or if no refresh has happened yet.
    // Non-blocking and safe to call every frame: on the common case where
    // the interval has not elapsed, this does no work beyond a clock read
    // and a comparison.
    Status RefreshIfDue();

    // Read-only, non-owning, allocation-free view over the currently known
    // devices (including ones that may currently be disconnected).
    [[nodiscard]] auto Devices() const noexcept {
        return devices_ | std::views::transform([](const std::unique_ptr<IWheelDevice>& ptr) { return ptr.get(); });
    }

    [[nodiscard]] std::size_t DeviceCount() const noexcept { return devices_.size(); }

private:
    void MergeCandidates(std::vector<std::unique_ptr<IWheelDevice>> candidates);

    std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators_;
    std::vector<std::unique_ptr<IWheelDevice>> devices_;
    std::chrono::milliseconds refreshInterval_;
    Clock clock_;
    DiagnosticSink diagnostics_;
    std::chrono::steady_clock::time_point lastRefresh_{};
    bool hasRefreshedOnce_ = false;
};

} // namespace rvwheel::dal
