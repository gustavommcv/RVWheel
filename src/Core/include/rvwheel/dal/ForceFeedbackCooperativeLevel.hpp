#pragma once

#include <cstdint>

namespace rvwheel::dal {

// DirectInput requires exclusive cooperative access for force feedback.
// Background preserves the bridge's headless operation, but Microsoft
// explicitly does not guarantee that an exclusive-background owner keeps
// access if another application requests exclusivity. Foreground is an
// opt-in diagnostic policy: it requires the associated top-level window to
// own the foreground and is automatically unacquired when that window
// loses it. Input-only operation never consults this value.
enum class ForceFeedbackCooperativeLevel : std::uint8_t {
    Background,
    Foreground,
};

} // namespace rvwheel::dal
