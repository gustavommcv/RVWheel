#include "HiddenWindow.hpp"

namespace rvwheel::tools::probe {

namespace {

constexpr wchar_t kWindowClassName[] = L"RVWheelDeviceProbeHiddenWindowClass";

LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

HiddenWindow::HiddenWindow(HiddenWindowMode mode) : instance_(GetModuleHandleW(nullptr)), mode_(mode) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = &HiddenWindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kWindowClassName;

    classAtom_ = RegisterClassExW(&windowClass);
    if (classAtom_ == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return; // hwnd_ stays nullptr; IsValid() reports the failure to the caller.
    }

    if (mode_ == HiddenWindowMode::MessageOnly) {
        // No visual presence, no taskbar entry, no paint messages -- the
        // existing headless window used by all ordinary input paths.
        hwnd_ = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, nullptr);
    } else if (mode_ == HiddenWindowMode::TopLevelUnfocused) {
        // SetCooperativeLevel's documented contract requires a top-level
        // process-owned HWND. This diagnostic variant satisfies that part
        // while deliberately remaining invisible and unfocused, so a real
        // Acquire() result answers whether DISCL_FOREGROUND also requires
        // active foreground ownership before we add any focus-stealing UI.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClassName,
                                L"RVWheel FFB foreground acquisition diagnostic", WS_POPUP,
                                0, 0, 1, 1, nullptr, nullptr, instance_, nullptr);
    } else {
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName,
                                L"RVWheel FFB foreground acquisition test - do not switch windows",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 120, nullptr, nullptr, instance_, nullptr);
    }
}

HiddenWindow::~HiddenWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
    if (classAtom_ != 0) {
        UnregisterClassW(kWindowClassName, instance_);
    }
}

void HiddenWindow::PumpMessages() noexcept {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool HiddenWindow::ActivateForForegroundDiagnostic() noexcept {
    if (hwnd_ == nullptr) {
        return false;
    }
    if (mode_ != HiddenWindowMode::TopLevelFocused) {
        return IsForeground();
    }

    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    SetForegroundWindow(hwnd_);
    SetActiveWindow(hwnd_);
    SetFocus(hwnd_);
    PumpMessages();
    return IsForeground();
}

} // namespace rvwheel::tools::probe
