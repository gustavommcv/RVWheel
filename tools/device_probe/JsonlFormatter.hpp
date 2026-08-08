#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rvwheel::tools::probe {

// Plain-data record for one capture sample. Deliberately decoupled from
// rvwheel::dal types so this header (and JsonlFormatter) can be unit
// tested without constructing any DAL object.
//
// schemaVersion 2 adds profileId/profileOrigin/readinessState to schema
// version 1's fields; every version-1 field keeps its exact previous
// meaning and JSON key, so a lenient version-1 reader that ignores unknown
// keys still parses a version-2 line correctly. Bumped to 2 (rather than
// left as an undocumented silent extension) specifically so a strict
// reader can detect the difference instead of guessing whether the three
// new fields will be present.
struct WheelSampleRecord {
    int schemaVersion = 2;
    long long elapsedMilliseconds = 0;
    std::uint64_t deviceId = 0;
    std::string backend;
    bool connected = false;
    bool valid = false;
    std::uint64_t sampleCounter = 0;
    float steering = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    std::optional<float> clutch;
    std::vector<int> pressedButtons;
    std::vector<std::string> povs;
    std::string pollStatus;

    // New in schema version 2.
    std::string profileId;      // Empty when no profile is currently applied (see profileOrigin).
    std::string profileOrigin;  // "BuiltInProfile" / "UserProfile" / "ProvisionalFallback" / "Unconfigured" / "AmbiguousMatch" / "InvalidExactMatch".
    std::string readinessState; // "Unconfigured" / "WarmingUp" / "Stabilizing" / "Ready" / "TimedOut". `valid` is true iff this is "Ready".
};

// Minimal, dependency-free JSON Lines formatter. Deliberately hand-rolled
// rather than adding a JSON library dependency: the schema is small,
// fixed, and entirely under this project's control (see
// WheelSampleRecord above) -- a handful of scalars plus two flat arrays,
// not a use case that benefits from a general-purpose JSON library.
class JsonlFormatter {
public:
    JsonlFormatter() = delete;

    // Returns one line WITHOUT a trailing newline; the caller decides line
    // termination, which keeps this trivially testable against an exact
    // expected string.
    [[nodiscard]] static std::string FormatLine(const WheelSampleRecord& record);

    // Exposed separately (not just inlined into FormatLine) so escaping
    // correctness can be tested directly against strings the current
    // schema never actually produces (quotes, backslashes, control
    // characters), not just the handful of enum-like values callers
    // happen to pass today.
    [[nodiscard]] static std::string EscapeString(std::string_view value);
};

} // namespace rvwheel::tools::probe
