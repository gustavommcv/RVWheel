#include "ConsoleRenderer.hpp"

#include <iostream>

namespace rvwheel::tools::probe {

ConsoleRenderer::ConsoleRenderer() : stdOut_(GetStdHandle(STD_OUTPUT_HANDLE)) {}

void ConsoleRenderer::RenderFrame(const std::vector<std::string>& lines) {
    if (stdOut_ != INVALID_HANDLE_VALUE && stdOut_ != nullptr) {
        if (!originCaptured_) {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (GetConsoleScreenBufferInfo(stdOut_, &info)) {
                origin_ = info.dwCursorPosition;
                originCaptured_ = true;
            }
        }
        if (originCaptured_) {
            SetConsoleCursorPosition(stdOut_, origin_);
        }
    }

    // If no console cursor position could be captured (e.g. output
    // redirected to a file/pipe), this simply falls back to plain
    // scrolling output via std::cout below, which still works correctly.
    for (const auto& line : lines) {
        std::cout << line << "\n";
    }
    std::cout.flush();
}

void ConsoleRenderer::RenderLine(const std::string& text) {
    std::cout << text << "\n";
    // A later RenderFrame call re-captures its origin from wherever the
    // cursor now is, so interleaving RenderLine (scrolling) and
    // RenderFrame (overwriting) calls stays visually coherent.
    originCaptured_ = false;
}

} // namespace rvwheel::tools::probe
