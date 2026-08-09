#pragma once

// This header is backend-internal: it is used by DirectInputDeviceEnumerator
// (which constructs instances) and by this backend's own tests. Consumers
// of the DAL only ever see IWheelDevice/ICalibratableWheelDevice; nothing
// outside the DirectInput backend needs to include this file. It
// intentionally DOES use DirectInput COM types in its own declaration
// (unlike IWheelDevice, which must stay vendor-agnostic) because
// DirectInputDevice's whole purpose is to own and drive those types with
// RAII.

#ifndef _WIN32
#error "DirectInputDevice requires Windows (DirectInput 8)."
#endif

#define DIRECTINPUT_VERSION 0x0800

// NOMINMAX: without it, <windows.h> defines min/max function-like macros.
// Every std::min/std::max call in this backend currently uses an explicit
// template argument (e.g. std::min<DWORD>(...)), which happens to be
// immune (the macro only fires when the name is directly followed by '('),
// but defining NOMINMAX removes the hazard entirely for any future,
// less-careful call added here.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dinput.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "rvwheel/dal/AxisNormalizer.hpp"
#include "rvwheel/dal/AxisSource.hpp"
#include "rvwheel/dal/DeviceReadinessPolicy.hpp"
#include "rvwheel/dal/DeviceReadinessTracker.hpp"
#include "rvwheel/dal/Diagnostics.hpp"
#include "rvwheel/dal/ICalibratableWheelDevice.hpp"
#include "rvwheel/dal/IWheelDevice.hpp"
#include "rvwheel/dal/WheelInputLayout.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::devices {

// A single raw axis this device was found to have at enumeration time,
// with its runtime-queried DIPROPRANGE. Populated once by
// DirectInputDeviceEnumerator for every rvwheel::dal::AxisSource that
// actually answered a DIPROP_RANGE query, independent of whether any role
// is bound to it -- this is the backing data for
// ICalibratableWheelDevice::EnumerateRawAxes() and for resolving whatever
// WheelInputLayout ApplyLayout() is given.
struct DiscoveredAxis {
    rvwheel::dal::AxisSource source = rvwheel::dal::AxisSource::Unknown;
    LONG rawMin = 0;
    LONG rawMax = 0;
};

// DirectInput does not copy the buffers referenced by DIEFFECT. These
// structures therefore live beside their corresponding COM effect for its
// entire lifetime; using function-local DICONDITION/DICONSTANTFORCE values
// here would leave the driver holding dangling pointers after CreateEffect
// or SetParameters returns.
struct ConstantForceEffectStorage {
    DICONSTANTFORCE parameters{};
    DWORD axis[1]{};
    LONG direction[1]{};
    std::optional<float> lastAppliedNormalized;
};

struct ConditionEffectStorage {
    DICONDITION parameters{};
    DWORD axis[1]{};
    LONG direction[1]{};
    std::optional<float> lastAppliedNormalized;
};

// Owns a single acquired IDirectInputDevice8A, the (lazily created)
// force-feedback effects derived from it, and the readiness state machine
// for whatever WheelInputLayout is currently applied. See
// DirectInputDevice.cpp for the cooperative-level rationale, reacquire
// policy, and force feedback effect lifecycle.
//
// Implements both IWheelDevice (gameplay-facing) and
// ICalibratableWheelDevice (tooling-facing raw axis discovery) against the
// SAME acquired COM object -- never a second, competing acquisition of
// the same physical device.
class DirectInputDevice final : public rvwheel::dal::IWheelDevice, public rvwheel::dal::ICalibratableWheelDevice {
public:
    DirectInputDevice(Microsoft::WRL::ComPtr<IDirectInputDevice8A> device,
                       rvwheel::dal::DeviceInfo info,
                       std::vector<DiscoveredAxis> discoveredAxes,
                       rvwheel::dal::DiagnosticSink diagnostics,
                       bool exclusiveForceFeedbackAccessRequested = false);

    ~DirectInputDevice() override;

    // IWheelDevice
    [[nodiscard]] const rvwheel::dal::DeviceInfo& Info() const noexcept override { return info_; }
    [[nodiscard]] bool IsConnected() const noexcept override { return state_.connected; }

    rvwheel::dal::Status Poll() noexcept override;
    [[nodiscard]] const rvwheel::dal::WheelState& State() const noexcept override { return state_; }

    rvwheel::dal::Status ApplyLayout(const rvwheel::dal::WheelInputLayout& layout,
                                      const rvwheel::dal::DeviceReadinessPolicy& readinessPolicy) noexcept override;

    rvwheel::dal::Status ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand& command) noexcept override;
    rvwheel::dal::Status StopForceFeedback() noexcept override;

    // ICalibratableWheelDevice
    [[nodiscard]] std::vector<rvwheel::dal::RawAxisInfo> EnumerateRawAxes() const override;
    rvwheel::dal::Status PollRawAxes(rvwheel::dal::RawAxisSnapshot& outSnapshot) noexcept override;

private:
    void ApplyRawState(const DIJOYSTATE2& raw) noexcept;
    [[nodiscard]] DWORD SteeringAxisObjectOffset() const noexcept;
    [[nodiscard]] const DiscoveredAxis* FindDiscoveredAxis(rvwheel::dal::AxisSource source) const noexcept;

    rvwheel::dal::Status ApplyConstantForce(float normalizedForce) noexcept;
    rvwheel::dal::Status ApplySpring(float normalizedStrength) noexcept;
    rvwheel::dal::Status ApplyDamper(float normalizedStrength) noexcept;
    rvwheel::dal::Status ApplyGain(float normalizedGain) noexcept;

    // Explicitly the ANSI ("A") interface throughout this backend so its
    // behavior does not depend on whether the consuming project defines
    // UNICODE/_UNICODE; DIDEVICEINSTANCE product-name strings are narrow
    // either way.
    Microsoft::WRL::ComPtr<IDirectInputDevice8A> device_;
    rvwheel::dal::DeviceInfo info_;
    std::vector<DiscoveredAxis> discoveredAxes_;
    rvwheel::dal::DiagnosticSink diagnostics_;

    // Which source (if any) currently feeds each role, and the resolved
    // runtime calibration for it. Both set only by ApplyLayout(), from
    // `discoveredAxes_` combined with the layout's requested direction;
    // never guessed independently.
    std::optional<rvwheel::dal::AxisSource> steeringSource_;
    std::optional<rvwheel::dal::AxisSource> throttleSource_;
    std::optional<rvwheel::dal::AxisSource> brakeSource_;
    std::optional<rvwheel::dal::AxisSource> clutchSource_;
    rvwheel::dal::AxisCalibration steeringCalibration_{};
    rvwheel::dal::PedalCalibration throttleCalibration_{};
    rvwheel::dal::PedalCalibration brakeCalibration_{};
    rvwheel::dal::PedalCalibration clutchCalibration_{};
    bool hasLayoutApplied_ = false;

    rvwheel::dal::DeviceReadinessTracker readiness_{rvwheel::dal::DeviceReadinessPolicy{}};

    rvwheel::dal::WheelState state_{};
    std::uint64_t sampleCounter_ = 0;

    // Diagnostic-only knowledge of how this DirectInput instance was
    // acquired. Ordinary shared-input devices leave this false and retain
    // exactly their prior behavior. Exclusive FFB diagnostics use it to
    // query actuator state and to treat a failed device-wide STOPALL as a
    // real loss of exclusivity instead of an expected nonexclusive result.
    bool exclusiveForceFeedbackAccessRequested_ = false;
    std::optional<HRESULT> lastForceFeedbackStateQueryResult_;
    std::optional<DWORD> lastForceFeedbackStateFlags_;
    std::optional<HRESULT> exclusiveForceFeedbackAccessFailure_;

    // Created lazily on first successful ApplyForceFeedback call for each
    // effect type and then updated in place (SetParameters) rather than
    // recreated, per device capability. Null when unsupported or unused.
    // Declared before the COM effects so C++ destroys the effects first
    // (member destruction is reverse declaration order), while every
    // buffer they reference is still alive.
    ConstantForceEffectStorage constantForceStorage_{};
    ConditionEffectStorage springStorage_{};
    ConditionEffectStorage damperStorage_{};
    Microsoft::WRL::ComPtr<IDirectInputEffect> constantForceEffect_;
    Microsoft::WRL::ComPtr<IDirectInputEffect> springEffect_;
    Microsoft::WRL::ComPtr<IDirectInputEffect> damperEffect_;
    std::optional<float> lastAppliedGain_;
};

} // namespace rvwheel::devices
