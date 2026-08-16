#include "rvwheel/devices/DirectInputDevice.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "DirectInputAxisMapping.hpp"

namespace rvwheel::devices {

namespace {

namespace dal = rvwheel::dal;

using dal::LogLevel;
using dal::Status;
using dal::StatusCode;

// DirectInput force feedback magnitudes/coefficients/saturations are all
// expressed on a -10000..10000 (or 0..10000) integer scale regardless of
// effect type; this is the one constant every Apply* helper below scales
// its normalized [-1,1]/[0,1] input against.
constexpr LONG kDIForceScale = 10000;

// Per Microsoft's SetParameters documentation, DIEP_START explicitly
// restarts an effect that is already playing. Runtime magnitude changes
// only need the type-specific buffer; omitting START lets DirectInput keep
// playback continuous and restart only if the driver genuinely requires
// it for a non-dynamic parameter.
constexpr DWORD kEffectParameterUpdateFlags = DIEP_TYPESPECIFICPARAMS;
static_assert((kEffectParameterUpdateFlags & DIEP_START) == 0);

[[nodiscard]] float FiniteClamp(float value, float minimum, float maximum) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : 0.0f;
}

[[nodiscard]] DIPROPDWORD DeviceDwordProperty(DWORD value = 0) noexcept {
    DIPROPDWORD property{};
    property.diph.dwSize = sizeof(DIPROPDWORD);
    property.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    property.diph.dwObj = 0;
    property.diph.dwHow = DIPH_DEVICE;
    property.dwData = value;
    return property;
}

// DirectInput reports POVs in hundredths of a degree, clockwise from north,
// or 0xFFFFFFFF when centered/no input. Buckets into 8 sectors of 45
// degrees each, offset by half a sector so 0 degrees lands in the middle of
// the "North" bucket rather than on its edge. Pure integer math: no
// floating point, no possibility of NaN/Inf.
[[nodiscard]] dal::PovDirection PovFromHundredthsOfDegree(DWORD raw) noexcept {
    using dal::PovDirection;
    if (raw > 35999u) {
        return PovDirection::Centered; // Covers the documented centered sentinel 0xFFFFFFFF and any other out-of-range value.
    }
    const DWORD sector = ((raw + 2250u) / 4500u) % 8u;
    switch (sector) {
        case 0: return PovDirection::North;
        case 1: return PovDirection::NorthEast;
        case 2: return PovDirection::East;
        case 3: return PovDirection::SouthEast;
        case 4: return PovDirection::South;
        case 5: return PovDirection::SouthWest;
        case 6: return PovDirection::West;
        case 7: return PovDirection::NorthWest;
        default: return PovDirection::Centered;
    }
}

// Driver behavior for an effect type the device does not support varies
// (some fail CreateEffect outright, others fail later); broadly classify
// the commonly-observed "not supported" HRESULTs here so callers get a
// NotSupported status instead of a misleading BackendError.
[[nodiscard]] bool IsUnsupportedEffectError(HRESULT hr) noexcept {
    return hr == E_NOTIMPL || hr == DIERR_UNSUPPORTED || hr == DIERR_INVALIDPARAM;
}

// The generic Status::BackendError strings this file used to return gave
// no way to tell DIERR_INPUTLOST/DIERR_NOTACQUIRED (device temporarily
// lost, expected to be transient) apart from a genuine, unexpected
// failure. Always include the raw HRESULT so a real hardware run's log
// (see docs/FORCE_FEEDBACK_HARDWARE_TEST.md's incident log) is
// diagnosable after the fact instead of just saying "it failed".
[[nodiscard]] std::string FormatHresult(HRESULT hr) noexcept {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(buffer);
}

[[nodiscard]] std::string FormatForceFeedbackState(DWORD flags) {
    std::string result;
    const auto append = [&result](const char* label) {
        if (!result.empty()) {
            result += '|';
        }
        result += label;
    };
    if ((flags & DIGFFS_ACTUATORSON) != 0) append("ACTUATORSON");
    if ((flags & DIGFFS_ACTUATORSOFF) != 0) append("ACTUATORSOFF");
    if ((flags & DIGFFS_DEVICELOST) != 0) append("DEVICELOST");
    if ((flags & DIGFFS_EMPTY) != 0) append("EMPTY");
    if ((flags & DIGFFS_PAUSED) != 0) append("PAUSED");
    if ((flags & DIGFFS_POWEROFF) != 0) append("POWEROFF");
    if ((flags & DIGFFS_SAFETYSWITCHON) != 0) append("SAFETYSWITCHON");
    if ((flags & DIGFFS_SAFETYSWITCHOFF) != 0) append("SAFETYSWITCHOFF");
    if ((flags & DIGFFS_USERFFSWITCHON) != 0) append("USERFFSWITCHON");
    if ((flags & DIGFFS_USERFFSWITCHOFF) != 0) append("USERFFSWITCHOFF");
    if ((flags & DIGFFS_POWERON) != 0) append("POWERON");
    if (result.empty()) {
        result = "none-reported";
    }
    return result;
}

} // namespace

DirectInputDevice::DirectInputDevice(Microsoft::WRL::ComPtr<IDirectInputDevice8A> device,
                                      rvwheel::dal::DeviceInfo info,
                                      std::vector<DiscoveredAxis> discoveredAxes,
                                      rvwheel::dal::DiagnosticSink diagnostics,
                                      bool exclusiveForceFeedbackAccessRequested)
    : device_(std::move(device)),
      info_(std::move(info)),
      discoveredAxes_(std::move(discoveredAxes)),
      diagnostics_(std::move(diagnostics)),
      exclusiveForceFeedbackAccessRequested_(exclusiveForceFeedbackAccessRequested) {}

DirectInputDevice::~DirectInputDevice() {
    if (device_) {
        if (constantForceEffect_) constantForceEffect_->Stop();
        if (springEffect_) springEffect_->Stop();
        if (damperEffect_) damperEffect_->Stop();
        // Best-effort, device-wide safety net: covers any effect this
        // instance did not itself create/track (e.g. left over from a
        // previous crashed process). Only succeeds under exclusive
        // acquisition (confirmed via Microsoft's own documentation and
        // FFConst sample -- see docs/research/FORCE_FEEDBACK_FEASIBILITY.md);
        // its failure under nonexclusive access is expected and silently
        // ignored, since the per-effect Stop() calls above are already the
        // primary mechanism in that case.
        device_->SendForceFeedbackCommand(DISFFC_STOPALL);
        // EndForceFeedbackSession is the one place that restores a native
        // autocenter value changed for this session. It also unacquires so
        // the vendor driver/software can resume ownership. A final
        // Unacquire remains harmless defense in depth if teardown was only
        // partially initialized or restoration itself failed.
        static_cast<void>(EndForceFeedbackSession());
        device_->Unacquire();
    }
}

Status DirectInputDevice::BeginForceFeedbackSession() noexcept {
    if (forceFeedbackSessionActive_) {
        return Status::Ok();
    }
    if (!device_) {
        return Status::NotConnected("Cannot begin force-feedback session on a disconnected device");
    }
    if (!exclusiveForceFeedbackAccessRequested_) {
        return Status::BackendError("Cannot begin force-feedback session without exclusive DirectInput access");
    }

    // Microsoft documents DIPROP_AUTOCENTER as a device-wide property and
    // documents that every property except DIPROP_FFGAIN can only be
    // changed while unacquired. The official FFConst sample likewise sets
    // AUTOCENTER=OFF before Acquire(). Keep this transition at the explicit
    // ownership boundary -- never in the per-frame ApplyForceFeedback path.
    const HRESULT unacquireHr = device_->Unacquire();
    if (FAILED(unacquireHr)) {
        return Status::BackendError("Failed to unacquire before configuring native autocenter: " +
                                    FormatHresult(unacquireHr));
    }

    DIPROPDWORD previous = DeviceDwordProperty();
    const HRESULT getHr = device_->GetProperty(DIPROP_AUTOCENTER, &previous.diph);
    if (FAILED(getHr)) {
        // Not all devices support this property. Preserve FFB compatibility
        // instead of rejecting an otherwise valid device, but make the
        // missing control explicit in diagnostics and change nothing we
        // cannot reliably restore.
        diagnostics_(LogLevel::Warning,
                     "GetProperty(DIPROP_AUTOCENTER) failed: " + FormatHresult(getHr) +
                         "; native autocenter left unchanged for this FFB session");
    } else if (previous.dwData == DIPROPAUTOCENTER_OFF) {
        diagnostics_(LogLevel::Info,
                     "DIPROP_AUTOCENTER was already OFF; preserving the pre-session setting");
    } else if (previous.dwData == DIPROPAUTOCENTER_ON) {
        DIPROPDWORD disabled = DeviceDwordProperty(DIPROPAUTOCENTER_OFF);
        const HRESULT setHr = device_->SetProperty(DIPROP_AUTOCENTER, &disabled.diph);
        if (setHr == DI_PROPNOEFFECT) {
            diagnostics_(LogLevel::Warning,
                         "SetProperty(DIPROP_AUTOCENTER=OFF) returned DI_PROPNOEFFECT; native autocenter may remain active");
        } else if (FAILED(setHr)) {
            diagnostics_(LogLevel::Warning,
                         "SetProperty(DIPROP_AUTOCENTER=OFF) failed: " + FormatHresult(setHr) +
                             "; force feedback will continue without native-autocenter control");
        } else {
            autoCenterRestoreValue_ = previous.dwData;

            DIPROPDWORD readback = DeviceDwordProperty();
            const HRESULT readbackHr = device_->GetProperty(DIPROP_AUTOCENTER, &readback.diph);
            if (SUCCEEDED(readbackHr) && readback.dwData == DIPROPAUTOCENTER_OFF) {
                diagnostics_(LogLevel::Info,
                             "DIPROP_AUTOCENTER changed ON -> OFF for the FFB ownership session (read-back confirmed)");
            } else if (SUCCEEDED(readbackHr)) {
                diagnostics_(LogLevel::Warning,
                             "DIPROP_AUTOCENTER OFF write succeeded but read-back was not OFF; hardware behavior is unconfirmed");
            } else {
                diagnostics_(LogLevel::Warning,
                             "DIPROP_AUTOCENTER OFF write succeeded but read-back failed: " +
                                 FormatHresult(readbackHr));
            }
        }
    } else {
        diagnostics_(LogLevel::Warning,
                     "GetProperty(DIPROP_AUTOCENTER) returned an unknown value; native autocenter left unchanged");
    }

    const HRESULT acquireHr = device_->Acquire();
    if (FAILED(acquireHr)) {
        // We are still unacquired here, so immediately undo a successful
        // OFF transition before reporting failure. Never strand the wheel
        // in a changed global state merely because exclusive reacquisition
        // failed.
        if (autoCenterRestoreValue_) {
            DIPROPDWORD restore = DeviceDwordProperty(*autoCenterRestoreValue_);
            const HRESULT restoreHr = device_->SetProperty(DIPROP_AUTOCENTER, &restore.diph);
            const bool restoreConfirmed = SUCCEEDED(restoreHr) && restoreHr != DI_PROPNOEFFECT;
            diagnostics_(restoreConfirmed ? LogLevel::Info : LogLevel::Error,
                         !restoreConfirmed
                             ? "Failed to restore DIPROP_AUTOCENTER after Acquire failure: " + FormatHresult(restoreHr)
                             : "Restored DIPROP_AUTOCENTER after Acquire failure");
            if (restoreConfirmed) {
                autoCenterRestoreValue_.reset();
            }
        }
        return Status::BackendError("Failed to reacquire after configuring native autocenter: " +
                                    FormatHresult(acquireHr));
    }

    forceFeedbackSessionActive_ = true;
    diagnostics_(LogLevel::Info, "Force-feedback ownership session begun; exclusive DirectInput device reacquired");
    return Status::Ok();
}

Status DirectInputDevice::EndForceFeedbackSession() noexcept {
    if (!device_) {
        return Status::NotConnected("Cannot end force-feedback session on a disconnected device");
    }
    if (!forceFeedbackSessionActive_ && !autoCenterRestoreValue_) {
        return Status::Ok();
    }

    std::string failures;
    const HRESULT unacquireHr = device_->Unacquire();
    if (FAILED(unacquireHr)) {
        failures = "Unacquire before native-autocenter restore: " + FormatHresult(unacquireHr);
    }

    if (autoCenterRestoreValue_) {
        DIPROPDWORD restore = DeviceDwordProperty(*autoCenterRestoreValue_);
        const HRESULT restoreHr = device_->SetProperty(DIPROP_AUTOCENTER, &restore.diph);
        HRESULT readbackHr = E_FAIL;
        DIPROPDWORD readback = DeviceDwordProperty();
        const bool setTookEffect = SUCCEEDED(restoreHr) && restoreHr != DI_PROPNOEFFECT;
        if (setTookEffect) {
            readbackHr = device_->GetProperty(DIPROP_AUTOCENTER, &readback.diph);
        }
        const bool restoreConfirmed =
            setTookEffect && SUCCEEDED(readbackHr) && readback.dwData == *autoCenterRestoreValue_;
        if (!restoreConfirmed) {
            if (!failures.empty()) {
                failures += "; ";
            }
            failures += "restore DIPROP_AUTOCENTER: set=" + FormatHresult(restoreHr);
            if (setTookEffect) {
                failures += ", read-back=" + FormatHresult(readbackHr);
            }
            diagnostics_(LogLevel::Error,
                         "Failed to confirm restoration of pre-session DIPROP_AUTOCENTER value");
        } else {
            diagnostics_(LogLevel::Info, "Restored pre-session DIPROP_AUTOCENTER value (read-back confirmed)");
            autoCenterRestoreValue_.reset();
        }
    }

    forceFeedbackSessionActive_ = false;
    lastForceFeedbackStateQueryResult_.reset();
    lastForceFeedbackStateFlags_.reset();
    exclusiveForceFeedbackAccessFailure_.reset();
    return failures.empty() ? Status::Ok()
                            : Status::BackendError("Failed to end force-feedback session cleanly: " + failures);
}

DWORD DirectInputDevice::SteeringAxisObjectOffset() const noexcept {
    return AxisObjectOffset(steeringSource_.value_or(rvwheel::dal::AxisSource::X));
}

const DiscoveredAxis* DirectInputDevice::FindDiscoveredAxis(rvwheel::dal::AxisSource source) const noexcept {
    for (const auto& axis : discoveredAxes_) {
        if (axis.source == source) {
            return &axis;
        }
    }
    return nullptr;
}

rvwheel::dal::Status DirectInputDevice::ApplyLayout(const rvwheel::dal::WheelInputLayout& layout,
                                                     const rvwheel::dal::DeviceReadinessPolicy& readinessPolicy) noexcept {
    namespace dal = rvwheel::dal;
    using dal::AxisBinding;
    using dal::AxisCalibration;
    using dal::AxisDirection;
    using dal::AxisSource;
    using dal::PedalCalibration;

    // Resolve every requested role against discoveredAxes_ into LOCAL
    // variables first, so a layout referencing a source this device does
    // not have leaves the device's existing configuration completely
    // untouched (never half-applied).
    std::optional<AxisSource> newSteeringSource;
    std::optional<AxisSource> newThrottleSource;
    std::optional<AxisSource> newBrakeSource;
    std::optional<AxisSource> newClutchSource;
    AxisCalibration newSteeringCal{};
    PedalCalibration newThrottleCal{};
    PedalCalibration newBrakeCal{};
    PedalCalibration newClutchCal{};

    const auto resolveSteering = [&](const AxisBinding& binding) -> bool {
        const DiscoveredAxis* axis = FindDiscoveredAxis(binding.source);
        if (axis == nullptr) {
            return false;
        }
        newSteeringCal = dal::AxisNormalizer::ResolveSteeringCalibration(axis->rawMin, axis->rawMax, binding.direction);
        newSteeringSource = binding.source;
        return true;
    };

    const auto resolvePedal = [&](const AxisBinding& binding, PedalCalibration& outCal, std::optional<AxisSource>& outSource) -> bool {
        const DiscoveredAxis* axis = FindDiscoveredAxis(binding.source);
        if (axis == nullptr) {
            return false;
        }
        outCal = dal::AxisNormalizer::ResolvePedalCalibration(axis->rawMin, axis->rawMax, binding.direction);
        outSource = binding.source;
        return true;
    };

    if (layout.steering && !resolveSteering(*layout.steering)) {
        return Status::InvalidArgument("ApplyLayout: steering source not found on this device");
    }
    if (layout.throttle && !resolvePedal(*layout.throttle, newThrottleCal, newThrottleSource)) {
        return Status::InvalidArgument("ApplyLayout: throttle source not found on this device");
    }
    if (layout.brake && !resolvePedal(*layout.brake, newBrakeCal, newBrakeSource)) {
        return Status::InvalidArgument("ApplyLayout: brake source not found on this device");
    }
    if (layout.clutch && !resolvePedal(*layout.clutch, newClutchCal, newClutchSource)) {
        return Status::InvalidArgument("ApplyLayout: clutch source not found on this device");
    }

    steeringSource_ = newSteeringSource;
    throttleSource_ = newThrottleSource;
    brakeSource_ = newBrakeSource;
    clutchSource_ = newClutchSource;
    steeringCalibration_ = newSteeringCal;
    throttleCalibration_ = newThrottleCal;
    brakeCalibration_ = newBrakeCal;
    clutchCalibration_ = newClutchCal;

    info_.capabilities.hasSteering = steeringSource_.has_value();
    info_.capabilities.hasThrottle = throttleSource_.has_value();
    info_.capabilities.hasBrake = brakeSource_.has_value();
    info_.capabilities.hasClutch = clutchSource_.has_value();

    hasLayoutApplied_ = !layout.IsEmpty();
    readiness_.SetPolicy(readinessPolicy);
    readiness_.Reset(std::chrono::steady_clock::now(), hasLayoutApplied_);
    if (!hasLayoutApplied_) {
        state_.valid = false;
        state_.readiness = rvwheel::dal::ReadinessState::Unconfigured;
    }

    return Status::Ok();
}

void DirectInputDevice::ApplyRawState(const DIJOYSTATE2& raw) noexcept {
    namespace dal = rvwheel::dal;

    if (steeringSource_) {
        const auto result = dal::AxisNormalizer::NormalizeSteering(ReadAxisRaw(raw, *steeringSource_), steeringCalibration_);
        state_.steering = result.value;
        if (!result.status.IsOk()) {
            diagnostics_(LogLevel::Warning, "Steering calibration is degenerate: " + result.status.Message());
        }
    } else {
        state_.steering = 0.0f;
    }

    const auto applyPedal = [&](const std::optional<dal::AxisSource>& source, const dal::PedalCalibration& cal, float& out,
                                 const char* label) {
        if (!source) {
            out = 0.0f;
            return;
        }
        const auto result = dal::AxisNormalizer::NormalizePedal(ReadAxisRaw(raw, *source), cal);
        out = result.value;
        if (!result.status.IsOk()) {
            diagnostics_(LogLevel::Warning, std::string(label) + " calibration is degenerate: " + result.status.Message());
        }
    };

    applyPedal(throttleSource_, throttleCalibration_, state_.throttle, "Throttle");
    applyPedal(brakeSource_, brakeCalibration_, state_.brake, "Brake");
    applyPedal(clutchSource_, clutchCalibration_, state_.clutch, "Clutch");

    for (std::size_t i = 0; i < dal::kMaxButtons; ++i) {
        state_.buttons[i] = (raw.rgbButtons[i] & 0x80) != 0;
    }

    const auto povCount = static_cast<std::uint8_t>(std::min<std::size_t>(info_.capabilities.povCount, dal::kMaxPovCount));
    state_.povCount = povCount;
    for (std::uint8_t i = 0; i < povCount; ++i) {
        state_.povs[i] = PovFromHundredthsOfDegree(raw.rgdwPOV[i]);
    }
    for (std::uint8_t i = povCount; i < dal::kMaxPovCount; ++i) {
        state_.povs[i] = dal::PovDirection::Centered;
    }
}

Status DirectInputDevice::Poll() noexcept {
    namespace dal = rvwheel::dal;

    const bool wasConnected = state_.connected;

    if (!device_) {
        state_.valid = false;
        state_.connected = false;
        return Status::NotConnected("DirectInput device handle is null");
    }

    // Required by some DirectInput devices/drivers even outside buffered
    // mode; the return value is informational (DI_NOEFFECT is normal) and
    // deliberately ignored here, real failures surface via GetDeviceState.
    device_->Poll();

    DIJOYSTATE2 raw{};
    HRESULT hr = device_->GetDeviceState(sizeof(DIJOYSTATE2), &raw);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        // Diagnostic only (see docs/FORCE_FEEDBACK_HARDWARE_TEST.md's
        // incident log, open question 5(a)): records exactly when input
        // polling itself first notices the device needs reacquiring, so a
        // real hardware run can correlate this against a force-feedback
        // SetParameters/Stop failure elsewhere instead of guessing whether
        // the two are related.
        diagnostics_(LogLevel::Warning, "Input poll lost acquisition (" + FormatHresult(hr) + "); reacquiring");
        const HRESULT acquireHr = device_->Acquire();
        diagnostics_(LogLevel::Warning, SUCCEEDED(acquireHr) ? "Reacquired successfully"
                                                              : "Reacquire failed: " + FormatHresult(acquireHr));
        if (SUCCEEDED(acquireHr)) {
            hr = device_->GetDeviceState(sizeof(DIJOYSTATE2), &raw);
        }
    }

    if (FAILED(hr)) {
        state_.valid = false;
        state_.connected = false;
        return Status::NotConnected("DirectInput GetDeviceState failed; device may be disconnected");
    }

    if (exclusiveForceFeedbackAccessRequested_) {
        DWORD forceFeedbackState = 0;
        const HRESULT stateHr = device_->GetForceFeedbackState(&forceFeedbackState);
        const bool resultChanged = !lastForceFeedbackStateQueryResult_ || *lastForceFeedbackStateQueryResult_ != stateHr;
        const bool flagsChanged = SUCCEEDED(stateHr) &&
                                  (!lastForceFeedbackStateFlags_ || *lastForceFeedbackStateFlags_ != forceFeedbackState);
        if (FAILED(stateHr) && resultChanged) {
            diagnostics_(LogLevel::Warning,
                         "GetForceFeedbackState failed: " + FormatHresult(stateHr));
        } else if (SUCCEEDED(stateHr) && (resultChanged || flagsChanged)) {
            diagnostics_(LogLevel::Info,
                         "GetForceFeedbackState: " + FormatForceFeedbackState(forceFeedbackState));
        }
        lastForceFeedbackStateQueryResult_ = stateHr;
        if (SUCCEEDED(stateHr)) {
            lastForceFeedbackStateFlags_ = forceFeedbackState;
            exclusiveForceFeedbackAccessFailure_.reset();
        } else {
            lastForceFeedbackStateFlags_.reset();
            exclusiveForceFeedbackAccessFailure_ = stateHr;
        }
    }

    ApplyRawState(raw);
    state_.connected = true;

    if (!wasConnected && hasLayoutApplied_) {
        // First successful poll after construction, or a reacquire
        // following a disconnect: restart the readiness clock so a fresh
        // startup transient (see the G923's ~2.05s midpoint read) is never
        // mistaken for stable, gameplay-valid input.
        readiness_.Reset(std::chrono::steady_clock::now(), true);
    }

    if (!hasLayoutApplied_) {
        state_.valid = false;
        state_.readiness = dal::ReadinessState::Unconfigured;
    } else {
        dal::ReadinessAxisSample sample{};
        sample.steering = state_.steering;
        sample.throttle = state_.throttle;
        sample.brake = state_.brake;
        sample.clutch = state_.clutch;
        sample.hasClutch = clutchSource_.has_value();

        const dal::ReadinessState readinessState = readiness_.Update(std::chrono::steady_clock::now(), sample);
        state_.readiness = readinessState;
        state_.valid = (readinessState == dal::ReadinessState::Ready);
    }

    state_.sampleCounter = ++sampleCounter_;
    state_.timestamp = std::chrono::steady_clock::now();
    return Status::Ok();
}

std::vector<rvwheel::dal::RawAxisInfo> DirectInputDevice::EnumerateRawAxes() const {
    std::vector<rvwheel::dal::RawAxisInfo> result;
    result.reserve(discoveredAxes_.size());
    for (const auto& axis : discoveredAxes_) {
        rvwheel::dal::RawAxisInfo info{};
        info.source = axis.source;
        // name left empty: this backend does not query
        // DIDEVICEOBJECTINSTANCE names in this iteration (documented
        // limitation) -- AxisSource is the reliable identifier.
        info.rawMin = axis.rawMin;
        info.rawMax = axis.rawMax;
        result.push_back(info);
    }
    return result;
}

rvwheel::dal::Status DirectInputDevice::PollRawAxes(rvwheel::dal::RawAxisSnapshot& outSnapshot) noexcept {
    if (!device_) {
        return Status::NotConnected("DirectInput device handle is null");
    }

    device_->Poll();

    DIJOYSTATE2 raw{};
    HRESULT hr = device_->GetDeviceState(sizeof(DIJOYSTATE2), &raw);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        if (SUCCEEDED(device_->Acquire())) {
            hr = device_->GetDeviceState(sizeof(DIJOYSTATE2), &raw);
        }
    }
    if (FAILED(hr)) {
        return Status::NotConnected("DirectInput GetDeviceState failed; device may be disconnected");
    }

    outSnapshot.count = 0;
    for (const auto& axis : discoveredAxes_) {
        if (outSnapshot.count >= rvwheel::dal::kMaxRawAxes) {
            break;
        }
        outSnapshot.samples[outSnapshot.count].source = axis.source;
        outSnapshot.samples[outSnapshot.count].rawValue = ReadAxisRaw(raw, axis.source);
        ++outSnapshot.count;
    }
    return Status::Ok();
}

Status DirectInputDevice::ApplyConstantForce(float normalizedForce) noexcept {
    const float clamped = FiniteClamp(normalizedForce, -1.0f, 1.0f);
    if (constantForceEffect_ && constantForceStorage_.lastAppliedNormalized == clamped) {
        return Status::Ok();
    }

    const DWORD axisOffset = SteeringAxisObjectOffset();
    if (constantForceEffect_ && constantForceStorage_.axis[0] != axisOffset) {
        return Status::BackendError("Cannot change constant-force axis while the effect is active; stop it first");
    }

    const DICONSTANTFORCE previousParameters = constantForceStorage_.parameters;
    constantForceStorage_.parameters.lMagnitude =
        static_cast<LONG>(clamped * static_cast<float>(kDIForceScale));
    constantForceStorage_.axis[0] = axisOffset;
    constantForceStorage_.direction[0] = 0;

    DIEFFECT eff{};
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwSamplePeriod = 0;
    eff.dwGain = kDIForceScale;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    eff.rgdwAxes = constantForceStorage_.axis;
    eff.rglDirection = constantForceStorage_.direction;
    eff.lpEnvelope = nullptr;
    eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
    eff.lpvTypeSpecificParams = &constantForceStorage_.parameters;
    eff.dwStartDelay = 0;

    if (!constantForceEffect_) {
        const HRESULT hr = device_->CreateEffect(GUID_ConstantForce, &eff, &constantForceEffect_, nullptr);
        if (FAILED(hr)) {
            constantForceEffect_.Reset();
            constantForceStorage_.parameters = previousParameters;
            return IsUnsupportedEffectError(hr)
                       ? Status::NotSupported("Constant force not supported by this device")
                       : Status::BackendError("CreateEffect(GUID_ConstantForce) failed: " + FormatHresult(hr));
        }
        const HRESULT startHr = constantForceEffect_->Start(1, 0);
        if (FAILED(startHr)) {
            constantForceEffect_.Reset();
            constantForceStorage_.parameters = previousParameters;
            return Status::BackendError("Failed to start constant force effect: " + FormatHresult(startHr));
        }
        constantForceStorage_.lastAppliedNormalized = clamped;
        return Status::Ok();
    }

    const HRESULT hr = constantForceEffect_->SetParameters(&eff, kEffectParameterUpdateFlags);
    if (FAILED(hr)) {
        constantForceStorage_.parameters = previousParameters;
        return Status::BackendError("Failed to update constant force effect: " + FormatHresult(hr));
    }
    constantForceStorage_.lastAppliedNormalized = clamped;
    return Status::Ok();
}

namespace {

Status ApplyConditionEffect(Microsoft::WRL::ComPtr<IDirectInputEffect>& effect,
                             ConditionEffectStorage& storage,
                             IDirectInputDevice8A* device,
                             REFGUID effectType,
                             DWORD axisOffset,
                             float normalizedStrength,
                             const char* label) noexcept {
    const float clamped = FiniteClamp(normalizedStrength, 0.0f, 1.0f);
    if (effect && storage.lastAppliedNormalized == clamped) {
        return Status::Ok();
    }
    if (effect && storage.axis[0] != axisOffset) {
        return Status::BackendError(std::string("Cannot change axis for ") + label + " while it is active; stop it first");
    }
    const LONG coefficient = static_cast<LONG>(clamped * static_cast<float>(kDIForceScale));

    const DICONDITION previousParameters = storage.parameters;
    storage.parameters.lOffset = 0;
    storage.parameters.lPositiveCoefficient = coefficient;
    storage.parameters.lNegativeCoefficient = coefficient;
    storage.parameters.dwPositiveSaturation = static_cast<DWORD>(kDIForceScale);
    storage.parameters.dwNegativeSaturation = static_cast<DWORD>(kDIForceScale);
    storage.parameters.lDeadBand = 0;
    storage.axis[0] = axisOffset;
    storage.direction[0] = 0;

    DIEFFECT eff{};
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwSamplePeriod = 0;
    eff.dwGain = kDIForceScale;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    eff.rgdwAxes = storage.axis;
    eff.rglDirection = storage.direction;
    eff.lpEnvelope = nullptr;
    eff.cbTypeSpecificParams = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams = &storage.parameters;
    eff.dwStartDelay = 0;

    if (!effect) {
        const HRESULT hr = device->CreateEffect(effectType, &eff, &effect, nullptr);
        if (FAILED(hr)) {
            effect.Reset();
            storage.parameters = previousParameters;
            return IsUnsupportedEffectError(hr)
                       ? Status::NotSupported(std::string(label) + " not supported by this device")
                       : Status::BackendError(std::string("CreateEffect failed for ") + label + ": " + FormatHresult(hr));
        }
        const HRESULT startHr = effect->Start(1, 0);
        if (FAILED(startHr)) {
            effect.Reset();
            storage.parameters = previousParameters;
            return Status::BackendError(std::string("Failed to start ") + label + ": " + FormatHresult(startHr));
        }
        storage.lastAppliedNormalized = clamped;
        return Status::Ok();
    }

    const HRESULT hr = effect->SetParameters(&eff, kEffectParameterUpdateFlags);
    if (FAILED(hr)) {
        storage.parameters = previousParameters;
        return Status::BackendError(std::string("Failed to update ") + label + ": " + FormatHresult(hr));
    }
    storage.lastAppliedNormalized = clamped;
    return Status::Ok();
}

} // namespace

Status DirectInputDevice::ApplySpring(float normalizedStrength) noexcept {
    return ApplyConditionEffect(springEffect_, springStorage_, device_.Get(), GUID_Spring, SteeringAxisObjectOffset(),
                                normalizedStrength, "spring effect");
}

Status DirectInputDevice::ApplyDamper(float normalizedStrength) noexcept {
    return ApplyConditionEffect(damperEffect_, damperStorage_, device_.Get(), GUID_Damper, SteeringAxisObjectOffset(),
                                normalizedStrength, "damper effect");
}

Status DirectInputDevice::ApplyGain(float normalizedGain) noexcept {
    const float clamped = FiniteClamp(normalizedGain, 0.0f, 1.0f);
    if (lastAppliedGain_ == clamped) {
        return Status::Ok();
    }

    DIPROPDWORD prop{};
    prop.diph.dwSize = sizeof(DIPROPDWORD);
    prop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    prop.diph.dwObj = 0;
    prop.diph.dwHow = DIPH_DEVICE;
    prop.dwData = static_cast<DWORD>(clamped * static_cast<float>(kDIForceScale));

    const HRESULT hr = device_->SetProperty(DIPROP_FFGAIN, &prop.diph);
    if (FAILED(hr)) {
        return Status::BackendError("Failed to set global force feedback gain: " + FormatHresult(hr));
    }
    lastAppliedGain_ = clamped;
    return Status::Ok();
}

Status DirectInputDevice::ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand& command) noexcept {
    if (!device_ || !state_.connected) {
        return Status::NotConnected("Cannot apply force feedback to a disconnected device");
    }
    if (!info_.capabilities.hasForceFeedback) {
        return Status::NotSupported("Device has no force feedback capability");
    }
    if (exclusiveForceFeedbackAccessRequested_ && !forceFeedbackSessionActive_) {
        return Status::BackendError("BeginForceFeedbackSession must succeed before applying an exclusive effect");
    }
    if (exclusiveForceFeedbackAccessFailure_) {
        return Status::BackendError("Exclusive force-feedback state is unavailable: " +
                                    FormatHresult(*exclusiveForceFeedbackAccessFailure_));
    }

    // Only touch an effect channel that either already exists (so it can be
    // ramped back down to zero, including all the way to Stop()) or is
    // being asked for a genuinely nonzero value now. Without this guard, a
    // spring-only command would also silently create and Start() zero-
    // magnitude constant-force and damper effects the caller never asked
    // for -- three concurrent effects on the same axis instead of one,
    // which is exactly the kind of unrequested complexity a first real
    // hardware run should not be exposed to. See
    // docs/FORCE_FEEDBACK_HARDWARE_TEST.md's incident log.
    const Status gainStatus = ApplyGain(command.gain);
    const Status constantStatus =
        (command.constantForce != 0.0f || constantForceEffect_) ? ApplyConstantForce(command.constantForce) : Status::Ok();
    const Status springStatus = (command.spring != 0.0f || springEffect_) ? ApplySpring(command.spring) : Status::Ok();
    const Status damperStatus = (command.damper != 0.0f || damperEffect_) ? ApplyDamper(command.damper) : Status::Ok();

    const std::array<const Status*, 4> results{&gainStatus, &constantStatus, &springStatus, &damperStatus};

    for (const Status* result : results) {
        if (!result->IsOk()) {
            diagnostics_(LogLevel::Warning, result->Message());
        }
    }
    for (const Status* result : results) {
        if (result->Code() == StatusCode::BackendError) {
            return *result;
        }
    }
    for (const Status* result : results) {
        if (result->Code() == StatusCode::NotSupported) {
            return *result;
        }
    }
    return Status::Ok();
}

Status DirectInputDevice::StopForceFeedback() noexcept {
    if (!device_) {
        return Status::NotConnected("Cannot stop force feedback on a disconnected device");
    }

    std::string failures;
    const std::array<std::pair<IDirectInputEffect*, const char*>, 3> tracked{
        {{constantForceEffect_.Get(), "constant force"}, {springEffect_.Get(), "spring"}, {damperEffect_.Get(), "damper"}}};
    for (const auto& [effect, label] : tracked) {
        if (effect == nullptr) {
            continue;
        }
        const HRESULT hr = effect->Stop();
        if (FAILED(hr)) {
            if (!failures.empty()) {
                failures += "; ";
            }
            failures += std::string(label) + ": " + FormatHresult(hr);
        }
    }

    // A stopped effect must be started afresh on the next activation.
    // Releasing it here makes that lifecycle explicit and invalidates the
    // cached values that would otherwise suppress the required restart.
    constantForceEffect_.Reset();
    springEffect_.Reset();
    damperEffect_.Reset();
    constantForceStorage_.lastAppliedNormalized.reset();
    springStorage_.lastAppliedNormalized.reset();
    damperStorage_.lastAppliedNormalized.reset();

    // Device-wide safety net beyond the effects this instance tracks; see
    // the destructor's comment. A failure stays harmless/ignored for the
    // default nonexclusive input path. For an explicit exclusive FFB owner
    // it is evidence that exclusivity or the device was lost, so surface it
    // to the diagnostic rather than reporting a false-positive stop.
    const HRESULT stopAllHr = device_->SendForceFeedbackCommand(DISFFC_STOPALL);
    if (exclusiveForceFeedbackAccessRequested_ && FAILED(stopAllHr)) {
        if (!failures.empty()) {
            failures += "; ";
        }
        failures += "device-wide STOPALL: " + FormatHresult(stopAllHr);
    }

    return failures.empty() ? Status::Ok() : Status::BackendError("Failed to stop: " + failures);
}

} // namespace rvwheel::devices
