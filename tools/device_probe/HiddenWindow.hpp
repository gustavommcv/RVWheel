#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rvwheel::tools::probe {

// RAII message-only Win32 window (HWND_MESSAGE parent): receives no paint
// events and has no visual presence, but is a fully valid HWND for
// DirectInput's SetCooperativeLevel and for
// rvwheel::dal::DeviceManagerInitParams. Deliberately NOT
// GetConsoleWindow(): Windows Terminal/pseudoconsole hosts do not reliably
// provide a console window handle usable this way, whereas a message-only
// window this process creates itself always works.
class HiddenWindow {
public:
    HiddenWindow();
    ~HiddenWindow();

    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND Handle() const noexcept { return hwnd_; }
    [[nodiscard]] HINSTANCE Instance() const noexcept { return instance_; }
    [[nodiscard]] bool IsValid() const noexcept { return hwnd_ != nullptr; }

    // Drains the calling thread's Win32 message queue without blocking.
    // Must be called periodically from the same thread that constructed
    // this window (Win32 windows and DirectInput cooperative-level state
    // are thread-affine).
    void PumpMessages() noexcept;

private:
    HINSTANCE instance_;
    ATOM classAtom_ = 0;
    HWND hwnd_ = nullptr;
};

} // namespace rvwheel::tools::probe
