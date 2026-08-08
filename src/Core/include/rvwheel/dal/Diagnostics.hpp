#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace rvwheel::dal {

enum class LogLevel : std::uint8_t {
    Info,
    Warning,
    Error,
};

// Injectable diagnostic sink. The DAL never throws or writes to stdout/files
// on its own; anything worth surfacing (backend init failures, unsupported
// force feedback requests, dedup decisions) is reported through this
// callback so the host application can route it to its own logging system.
using DiagnosticSink = std::function<void(LogLevel level, std::string_view message)>;

[[nodiscard]] inline DiagnosticSink NullDiagnosticSink() noexcept {
    return [](LogLevel, std::string_view) {};
}

} // namespace rvwheel::dal
