#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rvwheel::tools::probe {

// Plain-data snapshot of everything the monitor screen displays.
// Deliberately decoupled from rvwheel::dal types (see WheelSampleRecord
// for the same rationale) so formatting can be unit tested directly.
struct MonitorFrameData {
    std::string deviceName;
    std::string backend;
    std::string deviceIdHex;

    // Already formatted by the caller (see ProbeFormatting::FormatProfileOrigin/
    // FormatReadinessState) so this header stays decoupled from
    // rvwheel::profiles/rvwheel::dal enum types, matching the rest of this struct.
    std::string profileId;    // Empty when no profile is applied.
    std::string profileOrigin;
    std::string readinessState;

    double elapsedSeconds = 0.0;
    double durationSeconds = 0.0;

    bool connected = false;
    bool valid = false;
    std::uint64_t sampleCounter = 0;

    float steering = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    std::optional<float> clutch;

    std::vector<int> pressedButtons;
    std::vector<std::string> povs;

    double observedPollHz = 0.0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t failedPolls = 0;

    std::string lastPollStatus;
};

// Formats one full monitor screen "frame" as a fixed set of lines, each
// padded to kLineWidth so ConsoleRenderer can blindly overwrite the
// previous frame in place without leftover characters from a longer
// previous line. Pure string formatting only: no console/Win32 calls.
class MonitorFrameFormatter {
public:
    MonitorFrameFormatter() = delete;

    static constexpr std::size_t kLineWidth = 110;

    [[nodiscard]] static std::vector<std::string> FormatFrame(const MonitorFrameData& data);
};

} // namespace rvwheel::tools::probe
