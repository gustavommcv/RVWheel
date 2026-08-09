#pragma once

// This is the one public DAL header that is inherently Windows-specific:
// it is the composition root that wires DeviceManager (itself
// Windows-agnostic and backend-agnostic) up to the real DirectInput
// backend, and to the Logitech backend when RVWHEEL_ENABLE_LOGITECH_SDK is
// compiled in. Consumers who only care about IWheelDevice/DeviceManager
// never need to know DirectInputDevice or LogitechDevice exist; this
// factory is the seam that connects them.
//
// This header needs the real HINSTANCE/HWND types, so it includes
// <windows.h> rather than hand-rolling forward declarations: HINSTANCE and
// HWND only expand to distinct struct-pointer types when STRICT is
// defined; without it they collapse to plain `void*` typedefs, and a
// hand-rolled forward declaration would then conflict with <windows.h>'s
// own typedef if a consumer includes both. WIN32_LEAN_AND_MEAN keeps the
// footprint reasonable.
#ifndef _WIN32
#error "DeviceManagerFactory requires Windows (it wires up the DirectInput/Logitech backends)."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <memory>

#include "rvwheel/dal/DeviceManager.hpp"
#include "rvwheel/dal/Diagnostics.hpp"
#include "rvwheel/dal/ForceFeedbackCooperativeLevel.hpp"

namespace rvwheel::dal {

struct DeviceManagerInitParams {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    std::chrono::milliseconds refreshInterval = DeviceManager::kDefaultRefreshInterval;
    DiagnosticSink diagnostics = NullDiagnosticSink();

    // DirectInput force-feedback effects generally require exclusive
    // cooperative access. Input-only consumers must leave this false so
    // the game and vendor software can keep using the wheel concurrently.
    bool requestExclusiveForceFeedbackAccess = false;

    // Only consulted when the explicit exclusive-FFB request above is
    // true. Background preserves the existing headless behavior;
    // Foreground is an opt-in diagnostic experiment and requires `window`
    // to be a foreground, top-level HWND owned by this process.
    ForceFeedbackCooperativeLevel forceFeedbackCooperativeLevel = ForceFeedbackCooperativeLevel::Background;
};

// Builds a production DeviceManager wired to the DirectInput backend
// (always) and the Logitech backend (only when RVWHEEL_ENABLE_LOGITECH_SDK
// was defined at build time AND the vendor SDK reports itself usable at
// runtime; see DeviceManagerFactory.cpp). Never throws; if even the
// DirectInput backend cannot be constructed, returns a DeviceManager with
// zero enumerators (Devices() will simply stay empty) rather than a null
// pointer, so callers do not need extra null-checking on top of Status.
[[nodiscard]] std::unique_ptr<DeviceManager> CreateDefaultDeviceManager(const DeviceManagerInitParams& params);

} // namespace rvwheel::dal
