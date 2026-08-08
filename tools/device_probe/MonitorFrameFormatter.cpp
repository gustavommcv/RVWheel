#include "MonitorFrameFormatter.hpp"

#include <iomanip>
#include <sstream>

namespace rvwheel::tools::probe {

namespace {

std::string FormatSigned(float value) {
    std::ostringstream oss;
    oss << std::showpos << std::fixed << std::setprecision(3) << value;
    return oss.str();
}

std::string FormatUnsigned(float value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << value;
    return oss.str();
}

std::string JoinInts(const std::vector<int>& values) {
    if (values.empty()) {
        return "(none)";
    }
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << values[i];
    }
    return oss.str();
}

std::string JoinStrings(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "(none)";
    }
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << values[i];
    }
    return oss.str();
}

std::string PadLine(std::string line) {
    if (line.size() < MonitorFrameFormatter::kLineWidth) {
        line.append(MonitorFrameFormatter::kLineWidth - line.size(), ' ');
    }
    return line;
}

} // namespace

std::vector<std::string> MonitorFrameFormatter::FormatFrame(const MonitorFrameData& data) {
    std::vector<std::string> lines;

    {
        std::ostringstream oss;
        oss << "RVWheel Device Probe -- Monitor  [" << data.backend << "] " << data.deviceName << " (" << data.deviceIdHex
            << ")";
        lines.push_back(PadLine(oss.str()));
    }
    {
        std::ostringstream oss;
        oss << "elapsed " << std::fixed << std::setprecision(1) << data.elapsedSeconds << "s / " << data.durationSeconds
            << "s"
            << "   connected=" << (data.connected ? "true" : "false") << "   valid=" << (data.valid ? "true" : "false");
        lines.push_back(PadLine(oss.str()));
    }
    {
        std::ostringstream oss;
        oss << "profile   " << (data.profileId.empty() ? std::string("(none)") : data.profileId) << "  origin="
            << data.profileOrigin << "  readiness=" << data.readinessState;
        lines.push_back(PadLine(oss.str()));
    }
    lines.push_back(PadLine(""));
    lines.push_back(PadLine("steering  [-1.000, +1.000]  -> " + FormatSigned(data.steering)));
    lines.push_back(PadLine("throttle  [ 0.000,  1.000]  -> " + FormatUnsigned(data.throttle)));
    lines.push_back(PadLine("brake     [ 0.000,  1.000]  -> " + FormatUnsigned(data.brake)));
    lines.push_back(
        PadLine("clutch    [ 0.000,  1.000]  -> " + (data.clutch ? FormatUnsigned(*data.clutch) : std::string("N/A"))));
    lines.push_back(PadLine("buttons   pressed: " + JoinInts(data.pressedButtons)));
    lines.push_back(PadLine("POV       " + JoinStrings(data.povs)));
    lines.push_back(PadLine(""));
    {
        std::ostringstream oss;
        oss << "sample #" << data.sampleCounter << "   poll " << std::fixed << std::setprecision(1) << data.observedPollHz
            << " Hz   dropped=" << data.droppedFrames << "   failed=" << data.failedPolls << "   last=" << data.lastPollStatus;
        lines.push_back(PadLine(oss.str()));
    }

    return lines;
}

} // namespace rvwheel::tools::probe
