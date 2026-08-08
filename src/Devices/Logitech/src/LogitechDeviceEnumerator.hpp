#pragma once

// Backend-internal: constructs LogitechDevice instances during enumeration.
// Depends only on ILogitechSdk, so — like LogitechDevice itself — it
// compiles and is testable regardless of RVWHEEL_ENABLE_LOGITECH_SDK.

#include <memory>
#include <vector>

#include "rvwheel/dal/DeviceManager.hpp"
#include "rvwheel/dal/Diagnostics.hpp"
#include "rvwheel/devices/ILogitechSdk.hpp"

namespace rvwheel::devices {

class LogitechDeviceEnumerator final : public rvwheel::dal::IDeviceEnumerator {
public:
    LogitechDeviceEnumerator(std::shared_ptr<ILogitechSdk> sdk, rvwheel::dal::DiagnosticSink diagnostics);

    std::vector<std::unique_ptr<rvwheel::dal::IWheelDevice>> Enumerate() override;

private:
    std::shared_ptr<ILogitechSdk> sdk_;
    rvwheel::dal::DiagnosticSink diagnostics_;
    bool initialized_ = false;
    bool initFailedOnce_ = false;
};

} // namespace rvwheel::devices
