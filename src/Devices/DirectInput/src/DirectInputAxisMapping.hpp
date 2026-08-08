#pragma once

// Backend-internal helpers shared by DirectInputDevice.cpp (reading a live
// DIJOYSTATE2 sample) and DirectInputDeviceEnumerator.cpp (querying an
// axis's DIPROPRANGE before any sample exists). Kept out of the public
// header because nothing outside this backend needs them.

#include "rvwheel/dal/AxisSource.hpp"
#include "rvwheel/devices/DirectInputDevice.hpp"

namespace rvwheel::devices {

[[nodiscard]] inline LONG ReadAxisRaw(const DIJOYSTATE2& state, rvwheel::dal::AxisSource axis) noexcept {
    using rvwheel::dal::AxisSource;
    switch (axis) {
        case AxisSource::X: return state.lX;
        case AxisSource::Y: return state.lY;
        case AxisSource::Z: return state.lZ;
        case AxisSource::RotationX: return state.lRx;
        case AxisSource::RotationY: return state.lRy;
        case AxisSource::RotationZ: return state.lRz;
        case AxisSource::Slider0: return state.rglSlider[0];
        case AxisSource::Slider1: return state.rglSlider[1];
        case AxisSource::Unknown:
        default: return 0;
    }
}

[[nodiscard]] inline DWORD AxisObjectOffset(rvwheel::dal::AxisSource axis) noexcept {
    using rvwheel::dal::AxisSource;
    switch (axis) {
        case AxisSource::X: return DIJOFS_X;
        case AxisSource::Y: return DIJOFS_Y;
        case AxisSource::Z: return DIJOFS_Z;
        case AxisSource::RotationX: return DIJOFS_RX;
        case AxisSource::RotationY: return DIJOFS_RY;
        case AxisSource::RotationZ: return DIJOFS_RZ;
        case AxisSource::Slider0: return DIJOFS_SLIDER(0);
        case AxisSource::Slider1: return DIJOFS_SLIDER(1);
        case AxisSource::Unknown:
        default: return DIJOFS_X;
    }
}

} // namespace rvwheel::devices
