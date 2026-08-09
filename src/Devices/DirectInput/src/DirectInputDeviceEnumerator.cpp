#include "DirectInputDeviceEnumerator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "DirectInputAxisMapping.hpp"
#include "rvwheel/devices/DirectInputCooperativeLevel.hpp"

namespace rvwheel::devices {

namespace {

using rvwheel::dal::AxisSource;
using rvwheel::dal::DeviceBackend;
using rvwheel::dal::DeviceId;
using rvwheel::dal::Fnv1aHash;
using rvwheel::dal::LogLevel;

BOOL CALLBACK CollectDeviceInstanceCallback(LPCDIDEVICEINSTANCEA instance, LPVOID context) {
    auto* instances = reinterpret_cast<std::vector<DIDEVICEINSTANCEA>*>(context);
    instances->push_back(*instance);
    return DIENUM_CONTINUE;
}

[[nodiscard]] bool TryGetAxisRange(IDirectInputDevice8A* device, DWORD offset, LONG& outMin, LONG& outMax) noexcept {
    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(DIPROPRANGE);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwObj = offset;
    range.diph.dwHow = DIPH_BYOFFSET;

    if (FAILED(device->GetProperty(DIPROP_RANGE, &range.diph))) {
        return false;
    }
    outMin = range.lMin;
    outMax = range.lMax;
    return true;
}

// Discovers every raw axis this device actually answers a DIPROP_RANGE
// query for, independent of any role assignment -- role/direction
// resolution is entirely the profile system's job now (see
// rvwheel::profiles::ProfileResolver); this backend no longer decides on
// its own initiative which channel means "steering" or "throttle".
[[nodiscard]] std::vector<DiscoveredAxis> DiscoverAxes(IDirectInputDevice8A* device) {
    std::vector<DiscoveredAxis> result;
    for (const AxisSource source : rvwheel::dal::kAllAxisSources) {
        LONG rawMin = 0;
        LONG rawMax = 0;
        if (TryGetAxisRange(device, AxisObjectOffset(source), rawMin, rawMax)) {
            result.push_back(DiscoveredAxis{source, rawMin, rawMax});
        }
    }
    return result;
}

// Per the documented Windows convention for HID-backed DirectInput devices,
// a device's product GUID packs VID (low word) and PID (high word) into
// Data1 and spells a fixed "PIDVID" signature into the remaining bytes. We
// verify that signature before trusting Data1, so a non-conforming driver
// yields "unknown" instead of a silently wrong VID/PID. This convention has
// not been validated against real hardware in this session -- treat the
// extracted values as best-effort.
[[nodiscard]] std::optional<std::pair<std::uint16_t, std::uint16_t>> ExtractVidPid(const GUID& guidProduct) noexcept {
    static constexpr unsigned char kExpectedData4[8] = {0x00, 0x00, 0x50, 0x49, 0x44, 0x56, 0x49, 0x44};

    if (guidProduct.Data2 != 0 || guidProduct.Data3 != 0) {
        return std::nullopt;
    }
    for (int i = 0; i < 8; ++i) {
        if (guidProduct.Data4[i] != kExpectedData4[i]) {
            return std::nullopt;
        }
    }

    const auto vid = static_cast<std::uint16_t>(guidProduct.Data1 & 0xFFFFu);
    const auto pid = static_cast<std::uint16_t>((guidProduct.Data1 >> 16) & 0xFFFFu);
    return std::make_pair(vid, pid);
}

[[nodiscard]] DeviceId MakeDirectInputDeviceId(const GUID& guidInstance) noexcept {
    unsigned char bytes[1 + sizeof(GUID)];
    bytes[0] = static_cast<unsigned char>(DeviceBackend::DirectInput);
    std::memcpy(bytes + 1, &guidInstance, sizeof(GUID));
    return DeviceId::FromValue(Fnv1aHash(bytes, sizeof(bytes)));
}

[[nodiscard]] std::string FormatHresult(HRESULT hr) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(buffer);
}

[[nodiscard]] const char* CooperativeLevelName(DWORD flags) noexcept {
    if ((flags & DISCL_EXCLUSIVE) != 0) {
        return (flags & DISCL_FOREGROUND) != 0 ? "EXCLUSIVE | FOREGROUND" : "EXCLUSIVE | BACKGROUND";
    }
    return (flags & DISCL_FOREGROUND) != 0 ? "NONEXCLUSIVE | FOREGROUND" : "NONEXCLUSIVE | BACKGROUND";
}

} // namespace

DirectInputDeviceEnumerator::DirectInputDeviceEnumerator(HINSTANCE moduleInstance,
                                                         HWND window,
                                                         rvwheel::dal::DiagnosticSink diagnostics,
                                                         bool requestExclusiveForceFeedbackAccess,
                                                         rvwheel::dal::ForceFeedbackCooperativeLevel forceFeedbackCooperativeLevel)
    : moduleInstance_(moduleInstance),
      window_(window),
      diagnostics_(std::move(diagnostics)),
      requestExclusiveForceFeedbackAccess_(requestExclusiveForceFeedbackAccess),
      forceFeedbackCooperativeLevel_(forceFeedbackCooperativeLevel) {}

bool DirectInputDeviceEnumerator::EnsureDirectInput() noexcept {
    if (directInput_) {
        return true;
    }
    if (creationFailedOnce_) {
        return false;
    }

    const HRESULT hr = DirectInput8Create(moduleInstance_, DIRECTINPUT_VERSION, IID_IDirectInput8A,
                                           reinterpret_cast<void**>(directInput_.GetAddressOf()), nullptr);
    if (FAILED(hr)) {
        creationFailedOnce_ = true;
        diagnostics_(LogLevel::Error, "DirectInput8Create failed; DirectInput backend disabled for this session");
        return false;
    }
    return true;
}

std::unique_ptr<rvwheel::dal::IWheelDevice> DirectInputDeviceEnumerator::CreateDeviceFrom(const DIDEVICEINSTANCEA& instance) noexcept {
    namespace dal = rvwheel::dal;

    Microsoft::WRL::ComPtr<IDirectInputDevice8A> device;
    if (FAILED(directInput_->CreateDevice(instance.guidInstance, &device, nullptr))) {
        diagnostics_(LogLevel::Warning, "CreateDevice failed for an enumerated DirectInput controller");
        return nullptr;
    }

    if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))) {
        diagnostics_(LogLevel::Warning, "SetDataFormat failed; skipping device");
        return nullptr;
    }

    DIDEVCAPS caps{};
    caps.dwSize = sizeof(DIDEVCAPS);
    if (FAILED(device->GetCapabilities(&caps)) || caps.dwAxes == 0) {
        return nullptr; // No useful axes: not a wheel/pedal-like controller.
    }

    const bool hasForceFeedback = (caps.dwFlags & DIDC_FORCEFEEDBACK) != 0;

    // FFB-capable hardware is not automatically acquired exclusively.
    // Input-only clients must coexist with the game and vendor software;
    // only an explicit FFB owner opts into exclusive access.
    const DWORD cooperativeFlags = SelectDirectInputCooperativeFlags(
        hasForceFeedback, requestExclusiveForceFeedbackAccess_, forceFeedbackCooperativeLevel_);
    const HRESULT cooperativeHr = device->SetCooperativeLevel(window_, cooperativeFlags);
    if (FAILED(cooperativeHr)) {
        diagnostics_(LogLevel::Warning,
                     std::string("SetCooperativeLevel(") + CooperativeLevelName(cooperativeFlags) + ") failed: " +
                         FormatHresult(cooperativeHr) + "; skipping device");
        return nullptr;
    }

    std::vector<DiscoveredAxis> discoveredAxes = DiscoverAxes(device.Get());
    if (discoveredAxes.empty()) {
        return nullptr; // Reports axes via dwAxes, but none answered a range query; not usable.
    }

    const HRESULT acquireHr = device->Acquire();
    if (FAILED(acquireHr)) {
        diagnostics_(LogLevel::Warning,
                     std::string("Initial Acquire() with ") + CooperativeLevelName(cooperativeFlags) + " failed: " +
                         FormatHresult(acquireHr) + "; the device will retry acquisition on first Poll()");
        // Not fatal: DirectInputDevice::Poll() re-attempts Acquire() every call while unacquired.
    } else if ((cooperativeFlags & DISCL_EXCLUSIVE) != 0) {
        diagnostics_(LogLevel::Info,
                     std::string("Initial Acquire() succeeded with ") + CooperativeLevelName(cooperativeFlags));
    }

    dal::DeviceInfo info{};
    info.id = MakeDirectInputDeviceId(instance.guidInstance);
    info.name = instance.tszProductName;
    // DirectInput does not expose a separate manufacturer string; left
    // empty rather than guessed. Manufacturer, if needed, would have to
    // come from a higher layer (e.g. a VID lookup table), which is out of
    // scope for the DAL.
    info.manufacturer.clear();
    info.backend = dal::DeviceBackend::DirectInput;
    if (const auto vidPid = ExtractVidPid(instance.guidProduct)) {
        info.vendorId = vidPid->first;
        info.productId = vidPid->second;
    }
    // Role capability flags (hasSteering/hasThrottle/hasBrake/hasClutch)
    // start false: no WheelInputLayout has been applied yet (see
    // IWheelDevice::ApplyLayout). Only the hardware-fixed facts are set
    // here.
    info.capabilities.hasForceFeedback = hasForceFeedback;
    info.capabilities.buttonCount = static_cast<std::uint16_t>(std::min<DWORD>(caps.dwButtons, static_cast<DWORD>(dal::kMaxButtons)));
    info.capabilities.povCount = static_cast<std::uint8_t>(std::min<DWORD>(caps.dwPOVs, static_cast<DWORD>(dal::kMaxPovCount)));

    const bool exclusiveFfbRequested = (cooperativeFlags & DISCL_EXCLUSIVE) != 0;
    return std::make_unique<DirectInputDevice>(std::move(device), std::move(info), std::move(discoveredAxes), diagnostics_,
                                               exclusiveFfbRequested);
}

std::vector<std::unique_ptr<rvwheel::dal::IWheelDevice>> DirectInputDeviceEnumerator::Enumerate() {
    std::vector<std::unique_ptr<rvwheel::dal::IWheelDevice>> result;

    if (!EnsureDirectInput()) {
        return result;
    }

    std::vector<DIDEVICEINSTANCEA> instances;
    const HRESULT hr = directInput_->EnumDevices(DI8DEVCLASS_GAMECTRL, &CollectDeviceInstanceCallback, &instances, DIEDFL_ATTACHEDONLY);
    if (FAILED(hr)) {
        diagnostics_(LogLevel::Error, "DirectInput EnumDevices failed");
        return result;
    }

    for (const auto& instance : instances) {
        if (auto device = CreateDeviceFrom(instance)) {
            result.push_back(std::move(device));
        }
    }
    return result;
}

} // namespace rvwheel::devices
