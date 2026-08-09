// VehicleTelemetryTransport.hpp/.cpp deliberately has no dependency on
// rvwheel::dal's device contracts (IWheelDevice, DeviceManager, ...) --
// only <chrono>/<filesystem>/<optional>/<string...> and
// rvwheel::ffb::VehicleTelemetry (a plain data type). Every test below
// exercises only text parsing, freshness bookkeeping, and plain file
// reads; none of it can reach a real wheel, DirectInput, or any hardware.

#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "VehicleTelemetryTransport.hpp"

using rvwheel::tools::probe::FreshVehicleTelemetrySample;
using rvwheel::tools::probe::ParseVehicleTelemetryLine;
using rvwheel::tools::probe::ReadVehicleTelemetryFile;
using rvwheel::tools::probe::ToVehicleTelemetry;
using rvwheel::tools::probe::VehicleTelemetryFreshnessTracker;
using rvwheel::tools::probe::VehicleTelemetryFrame;

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point T(long long ms) { return Clock::time_point{} + std::chrono::milliseconds{ms}; }

VehicleTelemetryFrame MakeFrame(std::uint64_t sequence, bool valid = true, bool localPlayer = true) {
    VehicleTelemetryFrame frame;
    frame.sequence = sequence;
    frame.valid = valid;
    frame.localPlayer = localPlayer;
    return frame;
}
} // namespace

// ---------------------------------------------------------------------
// ParseVehicleTelemetryLine
// ---------------------------------------------------------------------

TEST_CASE("ParseVehicleTelemetryLine: a valid frame with yaw present parses exactly", "[Telemetry][Parse]") {
    const auto result = ParseVehicleTelemetryLine("RVT1 42 1 1 4.72 4.72 0.0012 0.0500 42");
    REQUIRE(result.success);
    REQUIRE(result.frame.sequence == 42);
    REQUIRE(result.frame.valid);
    REQUIRE(result.frame.localPlayer);
    REQUIRE(result.frame.speedMetersPerSecond == 4.72f);
    REQUIRE(result.frame.forwardMetersPerSecond == 4.72f);
    REQUIRE(result.frame.lateralMetersPerSecond == 0.0012f);
    REQUIRE(result.frame.yawRateRadiansPerSecond.has_value());
    REQUIRE(*result.frame.yawRateRadiansPerSecond == 0.0500f);
}

TEST_CASE("ParseVehicleTelemetryLine: yaw \"-\" parses as absent, never coerced to 0", "[Telemetry][Parse]") {
    const auto result = ParseVehicleTelemetryLine("RVT1 1 1 1 0.0 0.0 0.0 - 1");
    REQUIRE(result.success);
    REQUIRE_FALSE(result.frame.yawRateRadiansPerSecond.has_value());
}

TEST_CASE("ParseVehicleTelemetryLine: valid=0 and local=0 parse correctly (not rejected)", "[Telemetry][Parse]") {
    const auto result = ParseVehicleTelemetryLine("RVT1 5 0 0 0.0 0.0 0.0 - 5");
    REQUIRE(result.success);
    REQUIRE_FALSE(result.frame.valid);
    REQUIRE_FALSE(result.frame.localPlayer);
}

TEST_CASE("ParseVehicleTelemetryLine: a seqStart/seqEnd mismatch is rejected (partial-read guard)",
          "[Telemetry][Parse][Invalid]") {
    const auto result = ParseVehicleTelemetryLine("RVT1 42 1 1 4.72 4.72 0.0 - 43");
    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errorMessage.empty());
}

TEST_CASE("ParseVehicleTelemetryLine: a truncated line (missing tokens) is rejected",
          "[Telemetry][Parse][Invalid]") {
    const auto result = ParseVehicleTelemetryLine("RVT1 42 1 1 4.72 4.72");
    REQUIRE_FALSE(result.success);
}

TEST_CASE("ParseVehicleTelemetryLine: an extra trailing token is rejected, not silently ignored",
          "[Telemetry][Parse][Invalid]") {
    const auto result = ParseVehicleTelemetryLine("RVT1 42 1 1 4.72 4.72 0.0 - 42 extra");
    REQUIRE_FALSE(result.success);
}

TEST_CASE("ParseVehicleTelemetryLine: NaN in any numeric field is rejected, not coerced",
          "[Telemetry][Parse][Invalid]") {
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 nan 0.0 0.0 - 1").success);
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 0.0 nan 0.0 - 1").success);
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 0.0 0.0 nan - 1").success);
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 0.0 0.0 0.0 nan 1").success);
}

TEST_CASE("ParseVehicleTelemetryLine: Inf in any numeric field is rejected, not coerced",
          "[Telemetry][Parse][Invalid]") {
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 inf 0.0 0.0 - 1").success);
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 0.0 -inf 0.0 - 1").success);
}

TEST_CASE("ParseVehicleTelemetryLine: an out-of-domain speed (negative magnitude or absurdly large) is rejected",
          "[Telemetry][Parse][Invalid]") {
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 -0.5 0.0 0.0 - 1").success); // Magnitude can't be negative.
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 5000.0 0.0 0.0 - 1").success); // Absurd for a ground vehicle.
}

TEST_CASE("ParseVehicleTelemetryLine: an out-of-domain yaw rate is rejected", "[Telemetry][Parse][Invalid]") {
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 1 0.0 0.0 0.0 999.0 1").success);
}

TEST_CASE("ParseVehicleTelemetryLine: valid/local outside {0,1} are rejected", "[Telemetry][Parse][Invalid]") {
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 2 1 0.0 0.0 0.0 - 1").success);
    REQUIRE_FALSE(ParseVehicleTelemetryLine("RVT1 1 1 true 0.0 0.0 0.0 - 1").success);
}

// ---------------------------------------------------------------------
// ReadVehicleTelemetryFile
// ---------------------------------------------------------------------

TEST_CASE("ReadVehicleTelemetryFile: a nonexistent file returns nullopt, never throws", "[Telemetry][File]") {
    const auto frame = ReadVehicleTelemetryFile(
        std::filesystem::temp_directory_path() / "rvwheel_telemetry_does_not_exist_12345.txt");
    REQUIRE_FALSE(frame.has_value());
}

TEST_CASE("ReadVehicleTelemetryFile: a real file with one valid RVT1 line parses", "[Telemetry][File]") {
    const auto path = std::filesystem::temp_directory_path() / "rvwheel_telemetry_read_test.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "RVT1 7 1 1 1.5 1.5 0.0 - 7";
    }

    const auto frame = ReadVehicleTelemetryFile(path);
    REQUIRE(frame.has_value());
    REQUIRE(frame->sequence == 7);

    std::filesystem::remove(path);
}

TEST_CASE("ReadVehicleTelemetryFile: a truncated/mid-write file (bad seqEnd) yields nullopt, not a crash",
          "[Telemetry][File]") {
    const auto path = std::filesystem::temp_directory_path() / "rvwheel_telemetry_truncated_test.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "RVT1 7 1 1 1.5 1.5"; // Simulates a partial write caught mid-line.
    }

    const auto frame = ReadVehicleTelemetryFile(path);
    REQUIRE_FALSE(frame.has_value());

    std::filesystem::remove(path);
}

TEST_CASE("ReadVehicleTelemetryFile: a blank second line is harmless and accepted", "[Telemetry][File]") {
    const auto path = std::filesystem::temp_directory_path() / "rvwheel_telemetry_blank_second_line_test.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "RVT1 7 1 1 1.5 1.5 0.0 - 7\n";
    }

    const auto frame = ReadVehicleTelemetryFile(path);
    REQUIRE(frame.has_value());

    std::filesystem::remove(path);
}

TEST_CASE("ReadVehicleTelemetryFile: a non-empty second line rejects the whole read",
          "[Telemetry][File][Invalid]") {
    const auto path = std::filesystem::temp_directory_path() / "rvwheel_telemetry_extra_line_test.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "RVT1 7 1 1 1.5 1.5 0.0 - 7\n"
                "RVT1 8 1 1 1.5 1.5 0.0 - 8\n";
    }

    const auto frame = ReadVehicleTelemetryFile(path);
    REQUIRE_FALSE(frame.has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------
// VehicleTelemetryFreshnessTracker
// ---------------------------------------------------------------------

TEST_CASE("VehicleTelemetryFreshnessTracker: the very first observation is a baseline only, never fresh",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    // A leftover file from a previous session, already sitting on disk,
    // that happens to look perfectly valid/local: must still not count.
    REQUIRE_FALSE(tracker.Observe(MakeFrame(31594), T(0)).has_value());
    // Observing the exact same (baseline) sequence again immediately
    // afterward must not somehow become fresh either.
    REQUIRE_FALSE(tracker.Observe(MakeFrame(31594), T(10)).has_value());
}

TEST_CASE("VehicleTelemetryFreshnessTracker: a second, different sequence becomes fresh",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    REQUIRE_FALSE(tracker.Observe(MakeFrame(31594), T(0)).has_value()); // Baseline.

    const auto fresh = tracker.Observe(MakeFrame(31595), T(20));
    REQUIRE(fresh.has_value());
    REQUIRE(fresh->frame.sequence == 31595);
    REQUIRE(fresh->receivedAt == T(20));
}

TEST_CASE("VehicleTelemetryFreshnessTracker: a repeated sequence preserves the original receivedAt",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    static_cast<void>(tracker.Observe(MakeFrame(1), T(0))); // Baseline.
    const auto first = tracker.Observe(MakeFrame(2), T(20));
    REQUIRE(first.has_value());
    REQUIRE(first->receivedAt == T(20));

    // Same sequence, observed again much later but still within the
    // staleness window measured from the ORIGINAL receivedAt.
    const auto repeated = tracker.Observe(MakeFrame(2), T(400));
    REQUIRE(repeated.has_value());
    REQUIRE(repeated->receivedAt == T(20)); // Unchanged, not T(400).
}

TEST_CASE("ToVehicleTelemetry: uses the sample's own receivedAt, never a value the caller supplies separately",
          "[Telemetry][Convert]") {
    FreshVehicleTelemetrySample sample;
    sample.frame = MakeFrame(3);
    sample.frame.speedMetersPerSecond = 10.0f;
    sample.frame.lateralMetersPerSecond = -2.0f;
    sample.frame.yawRateRadiansPerSecond = 0.25f;
    sample.receivedAt = T(1000);

    const auto telemetry = ToVehicleTelemetry(sample);
    REQUIRE(telemetry.speedMetersPerSecond.has_value());
    REQUIRE(*telemetry.speedMetersPerSecond == 10.0f);
    REQUIRE(telemetry.lateralVelocityMetersPerSecond.has_value());
    REQUIRE(*telemetry.lateralVelocityMetersPerSecond == -2.0f);
    REQUIRE(telemetry.yawRateRadiansPerSecond.has_value());
    REQUIRE(*telemetry.yawRateRadiansPerSecond == 0.25f);
    REQUIRE(telemetry.isLocallyControlled.has_value());
    REQUIRE(*telemetry.isLocallyControlled);
    REQUIRE(telemetry.timestamp == T(1000)); // sample.receivedAt, exactly.
}

TEST_CASE("ToVehicleTelemetry: an absent yaw stays absent, never becomes 0", "[Telemetry][Convert]") {
    FreshVehicleTelemetrySample sample;
    sample.frame = MakeFrame(1);
    sample.frame.yawRateRadiansPerSecond = std::nullopt;
    sample.receivedAt = T(0);

    const auto telemetry = ToVehicleTelemetry(sample);
    REQUIRE_FALSE(telemetry.yawRateRadiansPerSecond.has_value());
}

TEST_CASE("VehicleTelemetryFreshnessTracker: a new sequence with valid=false invalidates immediately, not after "
          "a timeout",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    static_cast<void>(tracker.Observe(MakeFrame(1), T(0))); // Baseline.
    REQUIRE(tracker.Observe(MakeFrame(2, /*valid=*/true, /*local=*/true), T(10)).has_value());

    // A brand new sequence declaring itself invalid must clear the
    // previous sample right away -- well within the 500ms window.
    REQUIRE_FALSE(tracker.Observe(MakeFrame(3, /*valid=*/false, /*local=*/true), T(20)).has_value());
}

TEST_CASE("VehicleTelemetryFreshnessTracker: a new sequence with local=false invalidates immediately, not after "
          "a timeout",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    static_cast<void>(tracker.Observe(MakeFrame(1), T(0))); // Baseline.
    REQUIRE(tracker.Observe(MakeFrame(2, /*valid=*/true, /*local=*/true), T(10)).has_value());

    REQUIRE_FALSE(tracker.Observe(MakeFrame(3, /*valid=*/true, /*local=*/false), T(20)).has_value());
}

TEST_CASE("VehicleTelemetryFreshnessTracker: a partial/missing read preserves the last usable sample only "
          "until the staleness timeout",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{100});
    static_cast<void>(tracker.Observe(MakeFrame(1), T(0))); // Baseline.
    REQUIRE(tracker.Observe(MakeFrame(2), T(10)).has_value());

    // A read that couldn't be parsed at all this attempt (torn read,
    // momentarily missing file) must not invalidate immediately -- the
    // previous sample stays usable while still within the window.
    REQUIRE(tracker.Observe(std::nullopt, T(90)).has_value());
    // Past the 100ms window measured from the original receivedAt (T(10)):
    // now it must report unavailable.
    REQUIRE_FALSE(tracker.Observe(std::nullopt, T(111)).has_value());
}

TEST_CASE("VehicleTelemetryFreshnessTracker: staleness can never be renewed merely by repeating the same "
          "sequence",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{100});
    static_cast<void>(tracker.Observe(MakeFrame(1), T(0))); // Baseline.
    REQUIRE(tracker.Observe(MakeFrame(2), T(10)).has_value());

    // Repeating the same sequence well past staleness must not resurrect it.
    REQUIRE_FALSE(tracker.Observe(MakeFrame(2), T(500)).has_value());
    REQUIRE_FALSE(tracker.Observe(MakeFrame(2), T(1000)).has_value());
}

TEST_CASE("VehicleTelemetryFreshnessTracker: a Lua-side sequence restart (a smaller number) is recoverable the "
          "moment it next advances",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{500});
    static_cast<void>(tracker.Observe(MakeFrame(1), T(0))); // Baseline.
    REQUIRE(tracker.Observe(MakeFrame(100), T(10)).has_value());

    // Lua mod reloads; its own counter restarts from a small number. This
    // is a genuinely new/different sequence (not the tracker's original
    // one-time baseline), so it must become usable immediately -- no
    // special re-baseline is needed after the very first observation ever.
    const auto afterRestart = tracker.Observe(MakeFrame(1, true, true), T(20));
    REQUIRE(afterRestart.has_value());
    REQUIRE(afterRestart->frame.sequence == 1);

    // And it keeps advancing normally from there.
    const auto advanced = tracker.Observe(MakeFrame(2, true, true), T(40));
    REQUIRE(advanced.has_value());
    REQUIRE(advanced->frame.sequence == 2);
}

TEST_CASE("VehicleTelemetryFreshnessTracker: never having observed anything is unavailable, not a crash",
          "[Telemetry][Freshness]") {
    VehicleTelemetryFreshnessTracker tracker(std::chrono::milliseconds{100});
    REQUIRE_FALSE(tracker.Observe(std::nullopt, T(0)).has_value());
}
