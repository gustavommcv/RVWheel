#include "VehicleTelemetryTransport.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <utility>
#include <vector>

namespace rvwheel::tools::probe {

namespace {

constexpr std::string_view kMagic = "RVT1";
constexpr std::size_t kExpectedTokenCount = 9;

// Generous sanity ceilings meant to catch corrupted/garbage numbers, not
// to model real vehicle dynamics precisely. ~200 m/s is far beyond any
// plausible speed for the vehicles this project targets; ~50 rad/s
// (~2865 deg/s) is likewise far beyond any real driving yaw rate.
constexpr float kMaxPlausibleSpeedMetersPerSecond = 200.0f;
constexpr float kMaxPlausibleYawRateRadiansPerSecond = 50.0f;

[[nodiscard]] std::vector<std::string_view> SplitOnWhitespace(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }
        if (pos >= line.size()) {
            break;
        }
        const std::size_t start = pos;
        while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }
        tokens.push_back(line.substr(start, pos - start));
    }
    return tokens;
}

[[nodiscard]] bool TryParseFiniteFloat(std::string_view token, float& out) noexcept {
    float value = 0.0f;
    const auto parseResult = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parseResult.ec != std::errc{} || parseResult.ptr != token.data() + token.size()) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

[[nodiscard]] bool TryParseSequence(std::string_view token, std::uint64_t& out) noexcept {
    std::uint64_t value = 0;
    const auto parseResult = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parseResult.ec != std::errc{} || parseResult.ptr != token.data() + token.size()) {
        return false;
    }
    out = value;
    return true;
}

[[nodiscard]] bool TryParseBoolToken(std::string_view token, bool& out) noexcept {
    if (token == "0") {
        out = false;
        return true;
    }
    if (token == "1") {
        out = true;
        return true;
    }
    return false;
}

[[nodiscard]] VehicleTelemetryParseResult Fail(std::string message) {
    VehicleTelemetryParseResult result;
    result.success = false;
    result.errorMessage = std::move(message);
    return result;
}

} // namespace

VehicleTelemetryParseResult ParseVehicleTelemetryLine(std::string_view line) {
    const auto tokens = SplitOnWhitespace(line);
    if (tokens.size() != kExpectedTokenCount) {
        return Fail("expected " + std::to_string(kExpectedTokenCount) + " tokens, got " +
                     std::to_string(tokens.size()));
    }
    if (tokens[0] != kMagic) {
        return Fail("not an RVT1 line");
    }

    std::uint64_t seqStart = 0;
    std::uint64_t seqEnd = 0;
    if (!TryParseSequence(tokens[1], seqStart) || !TryParseSequence(tokens[8], seqEnd)) {
        return Fail("sequence tokens must be non-negative integers");
    }
    if (seqStart != seqEnd) {
        return Fail("seqStart/seqEnd mismatch (partial read)");
    }

    bool valid = false;
    bool localPlayer = false;
    if (!TryParseBoolToken(tokens[2], valid) || !TryParseBoolToken(tokens[3], localPlayer)) {
        return Fail("valid/local must be exactly \"0\" or \"1\"");
    }

    float speed = 0.0f;
    float forward = 0.0f;
    float lateral = 0.0f;
    if (!TryParseFiniteFloat(tokens[4], speed) || !TryParseFiniteFloat(tokens[5], forward) ||
        !TryParseFiniteFloat(tokens[6], lateral)) {
        return Fail("speed/forward/lateral must be finite numbers");
    }
    if (speed < 0.0f || speed > kMaxPlausibleSpeedMetersPerSecond ||
        forward < -kMaxPlausibleSpeedMetersPerSecond || forward > kMaxPlausibleSpeedMetersPerSecond ||
        lateral < -kMaxPlausibleSpeedMetersPerSecond || lateral > kMaxPlausibleSpeedMetersPerSecond) {
        return Fail("speed/forward/lateral out of plausible domain");
    }

    std::optional<float> yawRate;
    if (tokens[7] != "-") {
        float yaw = 0.0f;
        if (!TryParseFiniteFloat(tokens[7], yaw)) {
            return Fail("yaw must be \"-\" or a finite number");
        }
        if (yaw < -kMaxPlausibleYawRateRadiansPerSecond || yaw > kMaxPlausibleYawRateRadiansPerSecond) {
            return Fail("yaw out of plausible domain");
        }
        yawRate = yaw;
    }

    VehicleTelemetryParseResult result;
    result.success = true;
    result.frame.sequence = seqStart;
    result.frame.valid = valid;
    result.frame.localPlayer = localPlayer;
    result.frame.speedMetersPerSecond = speed;
    result.frame.forwardMetersPerSecond = forward;
    result.frame.lateralMetersPerSecond = lateral;
    result.frame.yawRateRadiansPerSecond = yawRate;
    return result;
}

std::optional<VehicleTelemetryFrame> ReadVehicleTelemetryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(input, line)) {
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    const auto parseResult = ParseVehicleTelemetryLine(line);
    if (!parseResult.success) {
        return std::nullopt;
    }

    // The writer only ever produces one line. A second, non-empty line
    // means something wrote more than expected (a racing process, a
    // writer bug, manual tampering) -- reject the whole read rather than
    // silently ignoring the extra content. A second line that exists but
    // is empty (e.g. a lone trailing newline) is harmless and accepted.
    std::string extra;
    if (std::getline(input, extra)) {
        if (!extra.empty() && extra.back() == '\r') {
            extra.pop_back();
        }
        if (!extra.empty()) {
            return std::nullopt;
        }
    }

    return parseResult.frame;
}

VehicleTelemetryFreshnessTracker::VehicleTelemetryFreshnessTracker(std::chrono::milliseconds staleAfter) noexcept
    : staleAfter_(staleAfter) {}

std::optional<FreshVehicleTelemetrySample> VehicleTelemetryFreshnessTracker::Observe(
    const std::optional<VehicleTelemetryFrame>& parsed, std::chrono::steady_clock::time_point now) noexcept {
    if (parsed.has_value()) {
        const bool isNewSequence = !lastKnownSequence_.has_value() || parsed->sequence != *lastKnownSequence_;
        if (isNewSequence) {
            const bool isFirstObservationEver = !lastKnownSequence_.has_value();
            lastKnownSequence_ = parsed->sequence;
            if (isFirstObservationEver) {
                // Baseline only -- see the class doc comment. Whatever was
                // already on disk when we started watching must never be
                // treated as fresh, no matter its valid/local flags.
                currentUsable_.reset();
            } else if (parsed->valid && parsed->localPlayer) {
                currentUsable_ = FreshVehicleTelemetrySample{*parsed, now};
            } else {
                // A new sequence that explicitly declares itself
                // invalid/non-local invalidates immediately -- never wait
                // for the previous usable sample to merely time out.
                currentUsable_.reset();
            }
        }
        // A repeated sequence number means the file simply hasn't changed
        // since the last read -- leave currentUsable_ (and its
        // receivedAt) exactly as it is.
    }
    // parsed == std::nullopt (a partial/torn read or missing file this
    // attempt): leave everything untouched; the staleness check below is
    // the only thing that can still clear currentUsable_ from here.

    if (!currentUsable_.has_value()) {
        return std::nullopt;
    }
    if (now - currentUsable_->receivedAt > staleAfter_) {
        return std::nullopt;
    }
    return currentUsable_;
}

rvwheel::ffb::VehicleTelemetry ToVehicleTelemetry(const FreshVehicleTelemetrySample& sample) {
    rvwheel::ffb::VehicleTelemetry telemetry;
    telemetry.speedMetersPerSecond = sample.frame.speedMetersPerSecond;
    telemetry.lateralVelocityMetersPerSecond = sample.frame.lateralMetersPerSecond;
    telemetry.yawRateRadiansPerSecond = sample.frame.yawRateRadiansPerSecond;
    telemetry.isLocallyControlled = sample.frame.localPlayer;
    telemetry.timestamp = sample.receivedAt;
    return telemetry;
}

} // namespace rvwheel::tools::probe
