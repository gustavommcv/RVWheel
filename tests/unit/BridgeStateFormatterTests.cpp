#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "BridgeStateFormatter.hpp"

using rvwheel::tools::probe::BridgeStateFormatter;
using rvwheel::tools::probe::BridgeStateRecord;

TEST_CASE("BridgeStateFormatter emits a versioned record with matching sequence guards", "[DeviceProbe][Bridge]") {
    BridgeStateRecord record{};
    record.sequence = 42;
    record.connected = true;
    record.valid = true;
    record.steering = -0.25f;
    record.throttle = 0.5f;
    record.brake = 0.75f;
    record.clutch = 1.0f;
    record.vendorId = 0x046D;
    record.productId = 0xC266;
    record.buttonWords = {0x00000001U, 0x80000000U, 0x1234ABCDU, 0x00000000U};

    REQUIRE(BridgeStateFormatter::Format(record) ==
            "RVW2 42 1 1 -0.250000 0.500000 0.750000 1.000000 046D C266 00000001 80000000 1234ABCD 00000000 42\n");
}

TEST_CASE("BridgeStateFormatter clamps axes and never serializes NaN", "[DeviceProbe][Bridge][Safety]") {
    BridgeStateRecord record{};
    record.sequence = 7;
    record.steering = 5.0f;
    record.throttle = -2.0f;
    record.brake = std::numeric_limits<float>::quiet_NaN();
    record.clutch = 3.0f;

    REQUIRE(BridgeStateFormatter::Format(record) ==
            "RVW2 7 0 0 1.000000 0.000000 0.000000 1.000000 0000 0000 00000000 00000000 00000000 00000000 7\n");
}
