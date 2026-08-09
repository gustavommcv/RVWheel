#pragma once

#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "rvwheel/ffb/ForceFeedbackTypes.hpp"

namespace rvwheel::tools::probe {

// One successfully parsed RVT1 line -- pure data, no clock, no I/O, no
// notion of "fresh" or "stale" (that is VehicleTelemetryFreshnessTracker's
// job, deliberately kept separate). `valid` and `localPlayer` are the
// wire format's own two flags, not reinterpreted here at parse time --
// VehicleTelemetryFreshnessTracker is what actually enforces what they
// mean for freshness; a caller reading this struct directly still sees
// the raw flags as parsed.
struct VehicleTelemetryFrame {
    std::uint64_t sequence = 0;
    bool valid = false;
    bool localPlayer = false;
    float speedMetersPerSecond = 0.0f;
    float forwardMetersPerSecond = 0.0f;
    float lateralMetersPerSecond = 0.0f;
    // Absent exactly when the wire line's yaw token was "-" (not yet
    // available from the Lua side) -- never coerced to 0.
    std::optional<float> yawRateRadiansPerSecond;
};

struct VehicleTelemetryParseResult {
    bool success = false;
    VehicleTelemetryFrame frame;
    std::string errorMessage; // Empty when success is true.
};

// Pure parsing: no filesystem, no clock, no device access. Parses exactly
// one RVT1 line: "RVT1 <seqStart> <valid> <local> <speedMps> <forwardMps>
// <lateralMps> <yawRateOrDash> <seqEnd>" (9 whitespace-separated tokens).
// Rejects, rather than coercing to a default:
//   - a token count other than 9 (truncated or extra tokens);
//   - a seqStart/seqEnd mismatch (the classic symptom of reading a file
//     the Lua side is mid-write on, since Lua has no atomic-replace
//     equivalent to MoveFileEx here);
//   - `valid`/`local` tokens that are not exactly "0" or "1";
//   - any NaN/Inf numeric token;
//   - a numeric token outside this function's plausible-domain bounds
//     (kMaxPlausibleSpeedMetersPerSecond / kMaxPlausibleYawRateRadiansPerSecond)
//     -- generous sanity ceilings, not physically exact limits, meant to
//     catch corruption/garbage, not to model real vehicle dynamics;
//   - a yaw token that is neither "-" nor a finite, in-domain number.
// Never silently turns a missing/invalid token into 0.
[[nodiscard]] VehicleTelemetryParseResult ParseVehicleTelemetryLine(std::string_view line);

// Reads and parses the transport file at `path` fresh from disk. Returns
// std::nullopt for a missing file, an empty file, a read failure, a first
// line that fails ParseVehicleTelemetryLine, OR a second line that is
// non-empty -- the writer only ever produces one line, so any further
// non-blank content means something wrote more than expected (a racing
// process, a writer bug, manual tampering) and the whole read is
// untrustworthy, not just "extra data to ignore". Never throws, never
// touches any device or hardware.
[[nodiscard]] std::optional<VehicleTelemetryFrame> ReadVehicleTelemetryFile(const std::filesystem::path& path);

// A frame that VehicleTelemetryFreshnessTracker::Observe() has confirmed
// is both genuinely new and valid/local, paired with the exact
// steady_clock reading at which its sequence number was first observed.
// This is the only timestamp ToVehicleTelemetry ever uses.
struct FreshVehicleTelemetrySample {
    VehicleTelemetryFrame frame;
    std::chrono::steady_clock::time_point receivedAt;
};

// Tracks which frame (if any) is currently safe to treat as live vehicle
// telemetry. Three rules, all enforced centrally here so no future
// consumer has to reimplement them:
//
// 1. Baseline-only first observation: the very first sequence number this
//    tracker ever observes -- which may be a stale leftover the Lua side
//    wrote in a *previous* session, still sitting on disk when a
//    consumer starts -- is recorded only as a comparison baseline. It is
//    never returned as fresh, regardless of its valid/local flags or how
//    soon after construction it is observed. Only a sequence number that
//    is *different* from whatever was last observed -- starting from
//    that baseline -- can ever become usable. This also means a Lua-side
//    restart (its own sequence counter resetting to a smaller number) is
//    still recoverable the moment a new number appears, because "new"
//    only ever means "different from last observed", never "larger".
// 2. valid/local are enforced here, not left to every future caller: a
//    newly observed sequence whose frame has valid == false or
//    localPlayer == false immediately clears whatever sample was
//    previously usable -- it does not wait for that sample to merely
//    time out. A read that cannot be parsed at all this call
//    (`parsed == std::nullopt`, e.g. a torn read or a momentarily missing
//    file) is different: it leaves the previous usable sample in place,
//    still subject to the ordinary staleness timeout below.
// 3. Repeated sequence numbers never renew freshness: observing the exact
//    same sequence again (the file simply has not changed since the last
//    read) preserves the original receivedAt exactly.
class VehicleTelemetryFreshnessTracker {
public:
    explicit VehicleTelemetryFreshnessTracker(std::chrono::milliseconds staleAfter) noexcept;

    // Call once per read of the transport file, with `now` and whatever
    // frame (if any) ReadVehicleTelemetryFile/ParseVehicleTelemetryLine
    // produced this time. Returns the currently usable sample -- subject
    // to all three rules above -- or std::nullopt if nothing usable is
    // available right now (never observed a non-baseline frame yet, the
    // last usable one was explicitly invalidated, or it has gone stale).
    [[nodiscard]] std::optional<FreshVehicleTelemetrySample> Observe(const std::optional<VehicleTelemetryFrame>& parsed,
                                                                       std::chrono::steady_clock::time_point now) noexcept;

    [[nodiscard]] std::chrono::milliseconds StaleAfter() const noexcept { return staleAfter_; }

private:
    std::chrono::milliseconds staleAfter_;
    std::optional<std::uint64_t> lastKnownSequence_;
    std::optional<FreshVehicleTelemetrySample> currentUsable_;
};

// Converts an already-validated fresh sample into the FFB engine's own
// VehicleTelemetry, always stamped with `sample.receivedAt` -- the exact
// moment its sequence was first observed as new and valid/local by
// VehicleTelemetryFreshnessTracker::Observe(). There is deliberately no
// way to pass a different timestamp here: a caller cannot substitute
// "now" and unintentionally make an old sample look freshly timestamped.
// Deliberately does not populate `steeringNormalized` (steering reaches
// the engine through the wheel device's own state, not this transport)
// and does not populate anything from `forwardMetersPerSecond` --
// VehicleTelemetry has no dedicated forward-speed field today.
[[nodiscard]] rvwheel::ffb::VehicleTelemetry ToVehicleTelemetry(const FreshVehicleTelemetrySample& sample);

} // namespace rvwheel::tools::probe
