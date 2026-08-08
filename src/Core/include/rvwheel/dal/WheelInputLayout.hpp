#pragma once

#include <optional>

#include "rvwheel/dal/AxisSource.hpp"

namespace rvwheel::dal {

// Whether increasing raw value should read as increasing role value
// (Normal) or the opposite (Inverted). Named as an enum rather than a bool
// so a profile field can never be an ambiguous "true"/"false" with no
// documented meaning; see AxisNormalizer.hpp for how a backend combines
// this with a runtime-queried raw range without ever hardcoding the
// endpoints themselves.
enum class AxisDirection : std::uint8_t {
    Normal,
    Inverted,
};

// How to locate an axis's center point. Only one policy exists today
// (the runtime-queried midpoint of the raw range), but this is an enum --
// not a bool "hasExplicitCenter" -- specifically so a future explicit
// override (e.g. a calibrated center that isn't the range midpoint) is a
// new enumerator, not a second, easily-confused field.
enum class AxisCenterPolicy : std::uint8_t {
    RangeMidpoint,
};

// Resolved "how to read this one physical channel for this one role"
// configuration. Deliberately merges what the task brief calls "binding"
// (which source) and "calibration" (direction/center) into a single type:
// nothing in this project ever needs to vary one without the other, and
// splitting them would add a type with no independent use. AxisBinding
// carries no numeric raw range -- ranges are always queried from the
// backend at runtime (see DirectInputDeviceEnumerator); direction only
// says which runtime-queried endpoint is "released"/"min" vs
// "pressed"/"max".
struct AxisBinding {
    AxisSource source = AxisSource::Unknown;
    AxisDirection direction = AxisDirection::Normal;
    std::optional<AxisCenterPolicy> center; // Only meaningful for steering; ignored for pedal roles.
};

// A fully-resolved, backend-agnostic input layout for one device: which
// raw source (if any) feeds each of the four roles the DAL knows about,
// and how to interpret it. Produced by the profile system (built-in,
// user, generated, or a conservative generic fallback) and consumed by
// IWheelDevice::ApplyLayout(); the DAL never constructs one of these on
// its own initiative for a specific vendor/model.
struct WheelInputLayout {
    std::optional<AxisBinding> steering;
    std::optional<AxisBinding> throttle;
    std::optional<AxisBinding> brake;
    std::optional<AxisBinding> clutch;

    [[nodiscard]] bool IsEmpty() const noexcept { return !steering && !throttle && !brake && !clutch; }
};

} // namespace rvwheel::dal
