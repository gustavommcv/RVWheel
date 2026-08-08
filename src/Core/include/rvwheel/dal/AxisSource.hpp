#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace rvwheel::dal {

// Opaque, backend-agnostic, serializable token identifying one physical
// analog channel. This is the vocabulary the DAL, the profile system, and
// on-disk JSON profiles all share for "which raw channel", so that no
// vendor type (DirectInput object IDs, DIJOFS_* offsets, GUIDs) ever needs
// to appear in a profile file or in a public DAL header.
//
// The named values currently mirror DirectInput's classic joystick object
// set (X/Y/Z/Rx/Ry/Rz/two sliders) because that is the only backend this
// project's axis-role resolution targets today; a future backend can reuse
// the same tokens for its own channels with equivalent meaning, or this
// enum can grow new values, without changing any profile that only uses
// the ones it needs.
enum class AxisSource : std::uint8_t {
    Unknown,
    X,
    Y,
    Z,
    RotationX,
    RotationY,
    RotationZ,
    Slider0,
    Slider1,
};

// Every enumerable value in declaration order (excluding Unknown), for
// code that needs to iterate "all possible sources" (raw axis discovery,
// schema validation). Kept in one place so backend and profile code never
// duplicate -- and risk disagreeing on -- this list.
inline constexpr AxisSource kAllAxisSources[] = {
    AxisSource::X,          AxisSource::Y,          AxisSource::Z,       AxisSource::RotationX,
    AxisSource::RotationY,  AxisSource::RotationZ,  AxisSource::Slider0, AxisSource::Slider1,
};

// Canonical string token for on-disk profiles and diagnostics (e.g. "X",
// "Rz", "Slider0"). Returns "Unknown" for AxisSource::Unknown.
[[nodiscard]] std::string_view ToString(AxisSource source) noexcept;

// Inverse of ToString(); recognizes exactly the tokens ToString() produces
// (case-sensitive, no aliases). Returns std::nullopt for anything else,
// including "Unknown" -- a profile should never need to spell that out,
// and treating it as a valid parse would let a typo silently become "no
// source" instead of a rejected profile.
[[nodiscard]] std::optional<AxisSource> AxisSourceFromString(std::string_view text) noexcept;

} // namespace rvwheel::dal
