#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rvwheel::tools::probe {

enum class HiddenWindowMode {
    // Existing mode for every ordinary input/tool path.
    MessageOnly,

    // A valid top-level HWND that is deliberately left invisible and
    // unfocused. Used only by the first DISCL_FOREGROUND diagnostic to
    // distinguish foreground-priority requirements from effect behavior.
    TopLevelUnfocused,

    // Small visible tool window used only by the second gated foreground
    // acquisition experiment. It is explicitly activated before DirectInput
    // is initialized and never used by ordinary input/bridge paths.
    TopLevelFocused,
};

// RAII message-only Win32 window (HWND_MESSAGE parent): receives no paint
// events and has no visual presence, but is a fully valid HWND for
// DirectInput's SetCooperativeLevel and for
// rvwheel::dal::DeviceManagerInitParams. Deliberately NOT
// GetConsoleWindow(): Windows Terminal/pseudoconsole hosts do not reliably
// provide a console window handle usable this way, whereas a message-only
// window this process creates itself always works.
class HiddenWindow {
public:
    explicit HiddenWindow(HiddenWindowMode mode = HiddenWindowMode::MessageOnly);
    ~HiddenWindow();

    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND Handle() const noexcept { return hwnd_; }
    [[nodiscard]] HINSTANCE Instance() const noexcept { return instance_; }
    [[nodiscard]] bool IsValid() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] bool IsForeground() const noexcept { return hwnd_ != nullptr && GetForegroundWindow() == hwnd_; }

    // Shows and activates TopLevelFocused. Returns true only when Windows
    // confirms this exact HWND owns the foreground. Other modes return
    // their current foreground state without changing visibility/focus.
    [[nodiscard]] bool ActivateForForegroundDiagnostic() noexcept;

    // Drains the calling thread's Win32 message queue without blocking.
    // Must be called periodically from the same thread that constructed
    // this window (Win32 windows and DirectInput cooperative-level state
    // are thread-affine).
    void PumpMessages() noexcept;

private:
    HINSTANCE instance_;
    ATOM classAtom_ = 0;
    HWND hwnd_ = nullptr;
    HiddenWindowMode mode_ = HiddenWindowMode::MessageOnly;
};

} // namespace rvwheel::tools::probe
