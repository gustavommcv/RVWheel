#include "rvwheel/dal/DeviceManagerFactory.hpp"

#include "DirectInputDeviceEnumerator.hpp"

#ifdef RVWHEEL_ENABLE_LOGITECH_SDK
#include "LogitechDeviceEnumerator.hpp"
#include "LogitechGamingSdkAdapter.hpp"
#endif

namespace rvwheel::dal {

std::unique_ptr<DeviceManager> CreateDefaultDeviceManager(const DeviceManagerInitParams& params) {
    std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators;

#ifdef RVWHEEL_ENABLE_LOGITECH_SDK
    // DeviceManager's dedup rule explicitly prefers Logitech over
    // DirectInput for the same physical device regardless of registration
    // order (see DeviceManager.cpp), so this ordering is purely cosmetic.
    // Initialize() failing (e.g. no real adapter implementation yet, or no
    // Logitech driver installed) simply makes this enumerator report zero
    // devices on every refresh; DirectInput below still works normally.
    auto logitechSdk = std::make_shared<rvwheel::devices::LogitechGamingSdkAdapter>();
    enumerators.push_back(std::make_unique<rvwheel::devices::LogitechDeviceEnumerator>(std::move(logitechSdk), params.diagnostics));
#endif

    enumerators.push_back(std::make_unique<rvwheel::devices::DirectInputDeviceEnumerator>(
        params.instance, params.window, params.diagnostics, params.requestExclusiveForceFeedbackAccess,
        params.forceFeedbackCooperativeLevel));

    return std::make_unique<DeviceManager>(std::move(enumerators), params.refreshInterval, &std::chrono::steady_clock::now,
                                            params.diagnostics);
}

} // namespace rvwheel::dal
