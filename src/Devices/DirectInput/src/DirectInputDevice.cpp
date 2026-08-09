#include "rvwheel/devices/DirectInputDevice.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

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

} // namespace

DirectInputDevice::DirectInputDevice(Microsoft::WRL::ComPtr<IDirectInputDevice8A> device,
                                      rvwheel::dal::DeviceInfo info,
                                      std::vector<DiscoveredAxis> discoveredAxes,
                                      rvwheel::dal::DiagnosticSink diagnostics)
    : device_(std::move(device)),
      info_(std::move(info)),
      discoveredAxes_(std::move(discoveredAxes)),
      diagnostics_(std::move(diagnostics)) {}

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
        device_->Unacquire();
    }
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
        if (SUCCEEDED(device_->Acquire())) {
            hr = device_->GetDeviceState(sizeof(DIJOYSTATE2), &raw);
        }
    }

    if (FAILED(hr)) {
        state_.valid = false;
        state_.connected = false;
        return Status::NotConnected("DirectInput GetDeviceState failed; device may be disconnected");
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
    const float clamped = std::clamp(normalizedForce, -1.0f, 1.0f);

    DICONSTANTFORCE cf{};
    cf.lMagnitude = static_cast<LONG>(clamped * static_cast<float>(kDIForceScale));

    DWORD axes[1] = {SteeringAxisObjectOffset()};
    LONG direction[1] = {0};

    DIEFFECT eff{};
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwSamplePeriod = 0;
    eff.dwGain = kDIForceScale;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = direction;
    eff.lpEnvelope = nullptr;
    eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
    eff.lpvTypeSpecificParams = &cf;
    eff.dwStartDelay = 0;

    if (!constantForceEffect_) {
        const HRESULT hr = device_->CreateEffect(GUID_ConstantForce, &eff, &constantForceEffect_, nullptr);
        if (FAILED(hr)) {
            constantForceEffect_.Reset();
            return IsUnsupportedEffectError(hr) ? Status::NotSupported("Constant force not supported by this device")
                                                 : Status::BackendError("CreateEffect(GUID_ConstantForce) failed");
        }
        const HRESULT startHr = constantForceEffect_->Start(1, 0);
        return FAILED(startHr) ? Status::BackendError("Failed to start constant force effect") : Status::Ok();
    }

    const HRESULT hr = constantForceEffect_->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_START);
    return FAILED(hr) ? Status::BackendError("Failed to update constant force effect") : Status::Ok();
}

namespace {

Status ApplyConditionEffect(Microsoft::WRL::ComPtr<IDirectInputEffect>& effect,
                             IDirectInputDevice8A* device,
                             REFGUID effectType,
                             DWORD axisOffset,
                             float normalizedStrength,
                             const char* label) noexcept {
    const float clamped = std::clamp(normalizedStrength, 0.0f, 1.0f);
    const LONG coefficient = static_cast<LONG>(clamped * static_cast<float>(kDIForceScale));

    DICONDITION condition{};
    condition.lOffset = 0;
    condition.lPositiveCoefficient = coefficient;
    condition.lNegativeCoefficient = coefficient;
    condition.dwPositiveSaturation = static_cast<DWORD>(kDIForceScale);
    condition.dwNegativeSaturation = static_cast<DWORD>(kDIForceScale);
    condition.lDeadBand = 0;

    DWORD axes[1] = {axisOffset};
    LONG direction[1] = {0};

    DIEFFECT eff{};
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwSamplePeriod = 0;
    eff.dwGain = kDIForceScale;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = direction;
    eff.lpEnvelope = nullptr;
    eff.cbTypeSpecificParams = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams = &condition;
    eff.dwStartDelay = 0;

    if (!effect) {
        const HRESULT hr = device->CreateEffect(effectType, &eff, &effect, nullptr);
        if (FAILED(hr)) {
            effect.Reset();
            return IsUnsupportedEffectError(hr) ? Status::NotSupported(std::string(label) + " not supported by this device")
                                                 : Status::BackendError(std::string("CreateEffect failed for ") + label);
        }
        const HRESULT startHr = effect->Start(1, 0);
        return FAILED(startHr) ? Status::BackendError(std::string("Failed to start ") + label) : Status::Ok();
    }

    const HRESULT hr = effect->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_START);
    return FAILED(hr) ? Status::BackendError(std::string("Failed to update ") + label) : Status::Ok();
}

} // namespace

Status DirectInputDevice::ApplySpring(float normalizedStrength) noexcept {
    return ApplyConditionEffect(springEffect_, device_.Get(), GUID_Spring, SteeringAxisObjectOffset(), normalizedStrength, "spring effect");
}

Status DirectInputDevice::ApplyDamper(float normalizedStrength) noexcept {
    return ApplyConditionEffect(damperEffect_, device_.Get(), GUID_Damper, SteeringAxisObjectOffset(), normalizedStrength, "damper effect");
}

Status DirectInputDevice::ApplyGain(float normalizedGain) noexcept {
    const float clamped = std::clamp(normalizedGain, 0.0f, 1.0f);

    DIPROPDWORD prop{};
    prop.diph.dwSize = sizeof(DIPROPDWORD);
    prop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    prop.diph.dwObj = 0;
    prop.diph.dwHow = DIPH_DEVICE;
    prop.dwData = static_cast<DWORD>(clamped * static_cast<float>(kDIForceScale));

    const HRESULT hr = device_->SetProperty(DIPROP_FFGAIN, &prop.diph);
    return FAILED(hr) ? Status::BackendError("Failed to set global force feedback gain") : Status::Ok();
}

Status DirectInputDevice::ApplyForceFeedback(const rvwheel::dal::ForceFeedbackCommand& command) noexcept {
    if (!device_ || !state_.connected) {
        return Status::NotConnected("Cannot apply force feedback to a disconnected device");
    }
    if (!info_.capabilities.hasForceFeedback) {
        return Status::NotSupported("Device has no force feedback capability");
    }

    const Status gainStatus = ApplyGain(command.gain);
    const Status constantStatus = ApplyConstantForce(command.constantForce);
    const Status springStatus = ApplySpring(command.spring);
    const Status damperStatus = ApplyDamper(command.damper);

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

    bool anyFailed = false;
    for (IDirectInputEffect* effect : {constantForceEffect_.Get(), springEffect_.Get(), damperEffect_.Get()}) {
        if (effect != nullptr && FAILED(effect->Stop())) {
            anyFailed = true;
        }
    }

    // Device-wide safety net beyond the effects this instance tracks; see
    // the destructor's comment. Deliberately not folded into `anyFailed`:
    // DIERR_NOTEXCLUSIVEACQUIRED here is an expected, harmless outcome
    // whenever RVWheel is (as by default) running with nonexclusive input
    // access, not a real force-feedback failure.
    device_->SendForceFeedbackCommand(DISFFC_STOPALL);

    return anyFailed ? Status::BackendError("Failed to stop one or more force feedback effects") : Status::Ok();
}

} // namespace rvwheel::devices
