#pragma once

#ifndef _WIN32
#error "DirectInput cooperative-level policy requires Windows."
#endif

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include "rvwheel/dal/ForceFeedbackCooperativeLevel.hpp"

namespace rvwheel::devices {

// Pure policy seam kept separate from IDirectInputDevice8 calls so the
// safety-critical distinction between ordinary shared input and explicit
// FFB ownership is unit-testable without hardware.
[[nodiscard]] constexpr DWORD SelectDirectInputCooperativeFlags(
    bool deviceHasForceFeedback,
    bool requestExclusiveForceFeedbackAccess,
    rvwheel::dal::ForceFeedbackCooperativeLevel forceFeedbackLevel) noexcept {
    if (!deviceHasForceFeedback || !requestExclusiveForceFeedbackAccess) {
        return DISCL_NONEXCLUSIVE | DISCL_BACKGROUND;
    }

    const DWORD focusFlag = forceFeedbackLevel == rvwheel::dal::ForceFeedbackCooperativeLevel::Foreground
                                ? DISCL_FOREGROUND
                                : DISCL_BACKGROUND;
    return DISCL_EXCLUSIVE | focusFlag;
}

} // namespace rvwheel::devices
