#include "HiddenWindow.hpp"

namespace rvwheel::tools::probe {

namespace {

constexpr wchar_t kWindowClassName[] = L"RVWheelDeviceProbeHiddenWindowClass";

LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

HiddenWindow::HiddenWindow() : instance_(GetModuleHandleW(nullptr)) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = &HiddenWindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kWindowClassName;

    classAtom_ = RegisterClassExW(&windowClass);
    if (classAtom_ == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return; // hwnd_ stays nullptr; IsValid() reports the failure to the caller.
    }

    // HWND_MESSAGE: a message-only window. No visual presence, no taskbar
    // entry, no paint messages -- exactly what a headless probe needs.
    hwnd_ = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, nullptr);
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

} // namespace rvwheel::tools::probe
