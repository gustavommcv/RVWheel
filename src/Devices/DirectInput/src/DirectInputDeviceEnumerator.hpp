#pragma once

// Backend-internal: constructs DirectInputDevice instances during
// enumeration. Not part of any public DAL header; only DeviceManagerFactory
// (composition root) and this backend's own tests include it.

#include <memory>
#include <vector>

#include "rvwheel/dal/DeviceManager.hpp"
#include "rvwheel/dal/Diagnostics.hpp"
#include "rvwheel/dal/ForceFeedbackCooperativeLevel.hpp"
#include "rvwheel/devices/DirectInputDevice.hpp"

namespace rvwheel::devices {

// IDeviceEnumerator implementation backed by DirectInput 8. Lazily creates
// the IDirectInput8A root object on first Enumerate() call (so construction
// itself cannot fail); if creation fails once, it is not retried on every
// subsequent Enumerate() call to avoid spamming the diagnostic sink every
// refresh interval — the enumerator simply keeps returning an empty list.
class DirectInputDeviceEnumerator final : public rvwheel::dal::IDeviceEnumerator {
public:
    DirectInputDeviceEnumerator(HINSTANCE moduleInstance,
                                HWND window,
                                rvwheel::dal::DiagnosticSink diagnostics,
                                bool requestExclusiveForceFeedbackAccess = false,
                                rvwheel::dal::ForceFeedbackCooperativeLevel forceFeedbackCooperativeLevel =
                                    rvwheel::dal::ForceFeedbackCooperativeLevel::Background);

    std::vector<std::unique_ptr<rvwheel::dal::IWheelDevice>> Enumerate() override;

private:
    bool EnsureDirectInput() noexcept;
    std::unique_ptr<rvwheel::dal::IWheelDevice> CreateDeviceFrom(const DIDEVICEINSTANCEA& instance) noexcept;

    HINSTANCE moduleInstance_;
    HWND window_;
    rvwheel::dal::DiagnosticSink diagnostics_;
    bool requestExclusiveForceFeedbackAccess_ = false;
    rvwheel::dal::ForceFeedbackCooperativeLevel forceFeedbackCooperativeLevel_ =
        rvwheel::dal::ForceFeedbackCooperativeLevel::Background;
    Microsoft::WRL::ComPtr<IDirectInput8A> directInput_;
    bool creationFailedOnce_ = false;
};

} // namespace rvwheel::devices
