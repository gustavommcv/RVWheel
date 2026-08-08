#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "JsonlFormatter.hpp"
#include "MonitorFrameFormatter.hpp"
#include "ProbeFormatting.hpp"

#include "rvwheel/dal/DeviceId.hpp"
#include "rvwheel/dal/Status.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

using rvwheel::dal::ButtonMask;
using rvwheel::dal::DeviceBackend;
using rvwheel::dal::DeviceId;
using rvwheel::dal::kMaxPovCount;
using rvwheel::dal::PovDirection;
using rvwheel::dal::StatusCode;

using namespace rvwheel::tools::probe;

TEST_CASE("FormatDeviceIdHex: a valid id renders as 16 uppercase hex digits, zero-padded", "[DeviceProbe][Formatting]") {
    const auto id = DeviceId::FromValue(0xDEADBEEFULL);
    REQUIRE(FormatDeviceIdHex(id) == "0x00000000DEADBEEF");
}

TEST_CASE("FormatDeviceIdHex: exact value round-trips", "[DeviceProbe][Formatting]") {
    const auto id = DeviceId::FromValue(0x0123456789ABCDEFULL);
    REQUIRE(FormatDeviceIdHex(id) == "0x0123456789ABCDEF");
}

TEST_CASE("FormatDeviceIdHex: an invalid id does not crash and is clearly marked", "[DeviceProbe][Formatting]") {
    const DeviceId invalid; // Default-constructed DeviceId is not valid.
    REQUIRE(FormatDeviceIdHex(invalid) == "0x0000000000000000(invalid)");
}

TEST_CASE("FormatBackend: known backends", "[DeviceProbe][Formatting]") {
    REQUIRE(FormatBackend(DeviceBackend::DirectInput) == "DirectInput");
    REQUIRE(FormatBackend(DeviceBackend::Logitech) == "Logitech");
}

TEST_CASE("FormatVendorProductId: both known", "[DeviceProbe][Formatting]") {
    REQUIRE(FormatVendorProductId(std::uint16_t{0x046D}, std::uint16_t{0xC24F}) == "VID=0x046D PID=0xC24F");
}

TEST_CASE("FormatVendorProductId: both unknown does not invent a value", "[DeviceProbe][Formatting]") {
    REQUIRE(FormatVendorProductId(std::nullopt, std::nullopt) == "VID=unknown PID=unknown");
}

TEST_CASE("FormatVendorProductId: one known, one unknown", "[DeviceProbe][Formatting]") {
    REQUIRE(FormatVendorProductId(std::uint16_t{0x046D}, std::nullopt) == "VID=0x046D PID=unknown");
}

TEST_CASE("FormatStatusCode: every StatusCode maps to a distinct, stable name", "[DeviceProbe][Formatting]") {
    REQUIRE(FormatStatusCode(StatusCode::Success) == "Ok");
    REQUIRE(FormatStatusCode(StatusCode::NotConnected) == "NotConnected");
    REQUIRE(FormatStatusCode(StatusCode::NotSupported) == "NotSupported");
    REQUIRE(FormatStatusCode(StatusCode::InvalidArgument) == "InvalidArgument");
    REQUIRE(FormatStatusCode(StatusCode::BackendError) == "BackendError");
}

TEST_CASE("PressedButtonIndices: only reports bits within the reported capability", "[DeviceProbe][Formatting][Safety]") {
    ButtonMask buttons{};
    buttons.set(0);
    buttons.set(3);
    buttons.set(50); // Beyond the device's reported buttonCount below.
    buttons.set(127); // The last physically representable bit; must never crash operator[].

    const auto pressed = PressedButtonIndices(buttons, /*buttonCount=*/10);
    REQUIRE(pressed == std::vector<int>{0, 3});
}

TEST_CASE("PressedButtonIndices: an empty mask reports nothing", "[DeviceProbe][Formatting]") {
    const ButtonMask buttons{};
    REQUIRE(PressedButtonIndices(buttons, 32).empty());
}

TEST_CASE("PressedButtonIndices: a buttonCount larger than the mask never overruns", "[DeviceProbe][Formatting][Safety]") {
    ButtonMask buttons{};
    buttons.set(127);
    const auto pressed = PressedButtonIndices(buttons, /*buttonCount=*/60000);
    REQUIRE(pressed == std::vector<int>{127});
}

TEST_CASE("FormatPovDirection: every direction maps to a distinct name", "[DeviceProbe][Formatting]") {
    REQUIRE(FormatPovDirection(PovDirection::Centered) == "Centered");
    REQUIRE(FormatPovDirection(PovDirection::North) == "North");
    REQUIRE(FormatPovDirection(PovDirection::NorthEast) == "NorthEast");
    REQUIRE(FormatPovDirection(PovDirection::East) == "East");
    REQUIRE(FormatPovDirection(PovDirection::SouthEast) == "SouthEast");
    REQUIRE(FormatPovDirection(PovDirection::South) == "South");
    REQUIRE(FormatPovDirection(PovDirection::SouthWest) == "SouthWest");
    REQUIRE(FormatPovDirection(PovDirection::West) == "West");
    REQUIRE(FormatPovDirection(PovDirection::NorthWest) == "NorthWest");
}

TEST_CASE("FormatActivePovs: only reports POVs within the reported capability", "[DeviceProbe][Formatting][Safety]") {
    std::array<PovDirection, kMaxPovCount> povs{};
    povs[0] = PovDirection::North;
    povs[1] = PovDirection::East; // Beyond povCount below; must not be read.

    const auto active = FormatActivePovs(povs, /*povCount=*/1);
    REQUIRE(active == std::vector<std::string>{"North"});
}

TEST_CASE("JsonlFormatter::EscapeString escapes quotes, backslashes and control characters", "[DeviceProbe][Jsonl]") {
    REQUIRE(JsonlFormatter::EscapeString("plain") == "plain");
    REQUIRE(JsonlFormatter::EscapeString("a\"b") == "a\\\"b");
    REQUIRE(JsonlFormatter::EscapeString("a\\b") == "a\\\\b");
    REQUIRE(JsonlFormatter::EscapeString("a\nb") == "a\\nb");
    REQUIRE(JsonlFormatter::EscapeString("a\tb") == "a\\tb");
    REQUIRE(JsonlFormatter::EscapeString("a\rb") == "a\\rb");
    // Adjacent string literals, not one literal "a\x01b": a hex escape
    // consumes every following hex digit, and 'b' is itself a valid hex
    // digit, so "a\x01b" as a single literal would actually mean 'a'
    // followed by the ONE character 0x1B, not 0x01 followed by 'b'.
    REQUIRE(JsonlFormatter::EscapeString(std::string("a" "\x01" "b")) == "a\\u0001b");
    REQUIRE(JsonlFormatter::EscapeString("a/b") == "a/b"); // Forward slash needs no escaping in JSON.
}

TEST_CASE("JsonlFormatter::FormatLine produces the documented schema with clutch present", "[DeviceProbe][Jsonl]") {
    WheelSampleRecord record{};
    record.schemaVersion = 2;
    record.elapsedMilliseconds = 1234;
    record.deviceId = 0x0123456789ABCDEFULL;
    record.backend = "DirectInput";
    record.connected = true;
    record.valid = true;
    record.sampleCounter = 42;
    record.steering = -0.5f;
    record.throttle = 0.25f;
    record.brake = 0.0f;
    record.clutch = 0.75f;
    record.pressedButtons = {1, 4};
    record.povs = {"North"};
    record.pollStatus = "Ok";
    record.profileId = "logitech-g923-ps-pc-directinput";
    record.profileOrigin = "BuiltInProfile";
    record.readinessState = "Ready";

    const std::string expected =
        "{\"schemaVersion\":2,\"elapsedMilliseconds\":1234,\"deviceId\":\"0x0123456789ABCDEF\",\"backend\":\"DirectInput\","
        "\"connected\":true,\"valid\":true,\"sampleCounter\":42,\"steering\":-0.500000,\"throttle\":0.250000,"
        "\"brake\":0.000000,\"clutch\":0.750000,\"pressedButtons\":[1,4],\"povs\":[\"North\"],\"pollStatus\":\"Ok\","
        "\"profileId\":\"logitech-g923-ps-pc-directinput\",\"profileOrigin\":\"BuiltInProfile\",\"readinessState\":\"Ready\"}";

    REQUIRE(JsonlFormatter::FormatLine(record) == expected);
}

TEST_CASE("JsonlFormatter::FormatLine represents an unsupported clutch as null, not a fabricated value",
          "[DeviceProbe][Jsonl]") {
    WheelSampleRecord record{};
    record.backend = "DirectInput";
    record.pollStatus = "NotConnected";
    // record.clutch left as std::nullopt.

    const std::string line = JsonlFormatter::FormatLine(record);
    REQUIRE(line.find("\"clutch\":null") != std::string::npos);
}

TEST_CASE("JsonlFormatter::FormatLine emits valid empty JSON arrays for no buttons/POVs", "[DeviceProbe][Jsonl]") {
    WheelSampleRecord record{};
    record.backend = "DirectInput";
    record.pollStatus = "Ok";

    const std::string line = JsonlFormatter::FormatLine(record);
    REQUIRE(line.find("\"pressedButtons\":[]") != std::string::npos);
    REQUIRE(line.find("\"povs\":[]") != std::string::npos);
}

TEST_CASE("MonitorFrameFormatter::FormatFrame pads every line to the fixed width", "[DeviceProbe][MonitorFrame]") {
    MonitorFrameData data{};
    data.deviceName = "Test Wheel";
    data.backend = "DirectInput";
    data.deviceIdHex = "0x1";
    data.steering = -0.123f;
    data.throttle = 0.5f;
    data.brake = 0.0f;
    data.clutch = std::nullopt;
    data.pressedButtons = {2, 5};
    data.povs = {"Centered"};
    data.lastPollStatus = "Ok";

    const auto lines = MonitorFrameFormatter::FormatFrame(data);
    REQUIRE_FALSE(lines.empty());
    for (const auto& line : lines) {
        REQUIRE(line.size() == MonitorFrameFormatter::kLineWidth);
    }
}

TEST_CASE("MonitorFrameFormatter::FormatFrame shows N/A for an unsupported clutch, not a fabricated value",
          "[DeviceProbe][MonitorFrame]") {
    MonitorFrameData data{};
    data.clutch = std::nullopt;

    const auto lines = MonitorFrameFormatter::FormatFrame(data);
    const bool foundClutchLine =
        std::any_of(lines.begin(), lines.end(), [](const std::string& line) { return line.find("N/A") != std::string::npos; });
    REQUIRE(foundClutchLine);
}
