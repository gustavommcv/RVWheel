#pragma once

#include "rvwheel/dal/Status.hpp"
#include "rvwheel/dal/WheelInputLayout.hpp"
#include "rvwheel/dal/DeviceReadinessPolicy.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::dal {

// Pure abstract, backend-agnostic contract for a single wheel/pedal set.
// Implementations (DirectInputDevice, LogitechDevice) own their underlying
// driver/SDK handles and are responsible for translating backend-specific
// data into the types declared in WheelTypes.hpp. No DirectInput or
// Logitech type ever appears in this interface.
//
// Lifetime/ownership: instances are owned exclusively via
// std::unique_ptr<IWheelDevice> by whoever created them (an
// IDeviceEnumerator, ultimately DeviceManager). Consumers borrow raw
// pointers/references; they never take ownership. Copying and moving are
// disabled because instances are polymorphic and hold non-trivial backend
// resources (COM objects, SDK handles) with backend-specific move
// semantics, if any.
//
// Poll() and ApplyForceFeedback() never throw: backend/SDK failures are
// converted into a Status and/or reflected in WheelState::valid /
// WheelState::connected. This keeps the per-frame path predictable for
// callers that cannot tolerate exceptions crossing into game/engine code.
class IWheelDevice {
public:
    virtual ~IWheelDevice() = default;

    IWheelDevice(const IWheelDevice&) = delete;
    IWheelDevice& operator=(const IWheelDevice&) = delete;
    IWheelDevice(IWheelDevice&&) = delete;
    IWheelDevice& operator=(IWheelDevice&&) = delete;

    // Immutable identity/capabilities, valid for the lifetime of the device.
    [[nodiscard]] virtual const DeviceInfo& Info() const noexcept = 0;

    // Reflects the outcome of the most recent Poll(); does not itself query
    // hardware. Use Poll() to refresh.
    [[nodiscard]] virtual bool IsConnected() const noexcept = 0;

    // Refreshes the cached WheelState snapshot from the underlying backend.
    // Must be cheap and allocation-free on the success path; must not
    // trigger a hardware re-enumeration (that is DeviceManager's job, at
    // most once per refresh interval). Returns Status::NotConnected if the
    // device is currently unreachable; State() still returns the last
    // known-good snapshot with WheelState::valid == false in that case.
    virtual Status Poll() noexcept = 0;

    // The snapshot produced by the most recent Poll() call.
    [[nodiscard]] virtual const WheelState& State() const noexcept = 0;

    // Resolves which raw channel (if any) feeds each of the four roles,
    // how to interpret it, and how long to wait before trusting it. This
    // is the ONLY place role/direction knowledge enters the DAL: backends
    // never guess a vendor's wiring on their own initiative. Calling this
    // updates Info().capabilities' role flags to match `layout` and
    // restarts the readiness state machine (see ReadinessState) from
    // WarmingUp. Passing an empty WheelInputLayout{} returns the device to
    // Unconfigured. Never throws; returns InvalidArgument if `layout`
    // references a source this device does not actually have (see
    // ICalibratableWheelDevice::EnumerateRawAxes to discover valid
    // sources beforehand).
    virtual Status ApplyLayout(const WheelInputLayout& layout, const DeviceReadinessPolicy& readinessPolicy) noexcept = 0;

    // Applies a force feedback command. If the device/backend cannot honor
    // one or more requested components (e.g. no spring support), the
    // components that ARE supported are still applied and the returned
    // Status reports NotSupported with a message naming what was skipped,
    // rather than silently dropping the whole command or failing outright.
    virtual Status ApplyForceFeedback(const ForceFeedbackCommand& command) noexcept = 0;

    // Halts any active force feedback effects without releasing underlying
    // effect resources, so a subsequent ApplyForceFeedback can resume
    // cheaply (no effect recreation).
    virtual Status StopForceFeedback() noexcept = 0;

protected:
    IWheelDevice() = default;
};

} // namespace rvwheel::dal
