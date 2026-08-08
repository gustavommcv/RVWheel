#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace rvwheel::tools::probe {

// Thin Win32 console writer: captures the cursor position on the first
// RenderFrame call and, on every subsequent call, rewinds to that position
// and overwrites the previous frame's lines in place -- this is how
// --monitor avoids "generating thousands of lines" while still updating.
// Callers are responsible for padding each line to a fixed width (see
// MonitorFrameFormatter) so a shorter new line still fully overwrites a
// longer previous one; this class does not clear-to-end-of-line itself.
class ConsoleRenderer {
public:
    ConsoleRenderer();

    void RenderFrame(const std::vector<std::string>& lines);

    // For one-off messages (enumeration output, connect/disconnect
    // transitions) that should scroll normally rather than overwrite a
    // frame in place.
    void RenderLine(const std::string& text);

private:
    HANDLE stdOut_;
    COORD origin_{};
    bool originCaptured_ = false;
};

} // namespace rvwheel::tools::probe
