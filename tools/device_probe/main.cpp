#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include "CliOptions.hpp"
#include "DeviceProbeApp.hpp"

namespace {

// SetConsoleCtrlHandler's callback signature carries no user context
// pointer, so a single process-wide flag is the only mechanism the Win32
// API leaves for signaling a running loop to stop cleanly on Ctrl+C. This
// is the one deliberate exception to "no mutable globals" in this
// codebase, forced by this specific OS integration point -- not a pattern
// used anywhere else in the probe or the DAL.
std::atomic<bool> g_stopRequested{false};

BOOL WINAPI OnConsoleCtrl(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            g_stopRequested.store(true);
            return TRUE;
        default:
            return FALSE;
    }
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    using rvwheel::tools::probe::CliParser;

    const std::vector<std::wstring> args(argv + 1, argv + argc);

    const auto parseResult = CliParser::Parse(args);
    if (!parseResult.success) {
        std::cerr << parseResult.errorMessage << "\n\n" << CliParser::UsageText() << "\n";
        return 1;
    }

    if (!SetConsoleCtrlHandler(&OnConsoleCtrl, TRUE)) {
        std::cerr << "Warning: could not install a Ctrl+C handler; Ctrl+C may terminate abruptly instead of "
                     "shutting down cleanly.\n";
    }

    rvwheel::tools::probe::DeviceProbeApp app(parseResult.options, g_stopRequested);
    return app.Run();
}
