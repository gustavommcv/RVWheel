// Regression guard for the DeviceCapabilities/DeviceCapabilitiesA Win32
// macro collision (see WheelTypes.hpp). <windows.h> defines
// DeviceCapabilities as an ANSI/Unicode macro; because macro expansion
// happens even for namespace-qualified identifiers, any public DAL symbol
// whose name collides with a Win32 macro would silently rename itself in
// any translation unit that includes <windows.h> first. This file includes
// <windows.h> BEFORE every public DAL header specifically to catch that
// class of regression at compile time, not just when the DirectInput
// backend happens to be built in some particular include order.
#include <windows.h>

#include "rvwheel/dal/AxisNormalizer.hpp"
#include "rvwheel/dal/DeviceId.hpp"
#include "rvwheel/dal/DeviceManager.hpp"
#include "rvwheel/dal/DeviceManagerFactory.hpp"
#include "rvwheel/dal/Diagnostics.hpp"
#include "rvwheel/dal/IWheelDevice.hpp"
#include "rvwheel/dal/Status.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Public DAL headers compile and behave correctly after <windows.h>", "[Win32][Smoke]") {
    // If WheelDeviceCapabilities had collided with a Win32 macro, this
    // would either fail to compile (unknown type
    // "WheelDeviceCapabilitiesA") or, worse, silently compile against some
    // unrelated Win32 type. Actually setting and reading fields back
    // catches both failure modes, not just "did it parse".
    rvwheel::dal::WheelDeviceCapabilities capabilities{};
    capabilities.hasSteering = true;
    capabilities.hasForceFeedback = true;
    capabilities.buttonCount = 12;
    capabilities.povCount = 1;

    rvwheel::dal::DeviceInfo info{};
    info.backend = rvwheel::dal::DeviceBackend::DirectInput;
    info.capabilities = capabilities;

    REQUIRE(info.capabilities.hasSteering);
    REQUIRE(info.capabilities.hasForceFeedback);
    REQUIRE(info.capabilities.buttonCount == 12);
    REQUIRE(info.capabilities.povCount == 1);

    // DeviceManagerFactory.hpp is the one public header that itself
    // requires <windows.h> (for HINSTANCE/HWND); confirm its own types are
    // usable here too, in the same "windows.h already included" order.
    rvwheel::dal::DeviceManagerInitParams params{};
    params.instance = nullptr;
    params.window = nullptr;
    REQUIRE(params.refreshInterval == rvwheel::dal::DeviceManager::kDefaultRefreshInterval);
}
