#include "JsonlFormatter.hpp"

namespace rvwheel::tools::probe {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

void AppendHex4(std::string& out, unsigned value) {
    out.push_back(kHexDigits[(value >> 12) & 0xF]);
    out.push_back(kHexDigits[(value >> 8) & 0xF]);
    out.push_back(kHexDigits[(value >> 4) & 0xF]);
    out.push_back(kHexDigits[value & 0xF]);
}

void AppendHexU64Upper(std::string& out, std::uint64_t value) {
    constexpr char kUpperHexDigits[] = "0123456789ABCDEF";
    out += "0x";
    for (int shift = 60; shift >= 0; shift -= 4) {
        out.push_back(kUpperHexDigits[(value >> shift) & 0xF]);
    }
}

void AppendJsonString(std::string& out, std::string_view value) {
    out.push_back('"');
    out += JsonlFormatter::EscapeString(value);
    out.push_back('"');
}

void AppendJsonArrayOfInts(std::string& out, const std::vector<int>& values) {
    out.push_back('[');
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        out += std::to_string(values[i]);
    }
    out.push_back(']');
}

void AppendJsonArrayOfStrings(std::string& out, const std::vector<std::string>& values) {
    out.push_back('[');
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        AppendJsonString(out, values[i]);
    }
    out.push_back(']');
}

void AppendFloat(std::string& out, float value) {
    // std::to_string(float) is fixed-notation with 6 decimals, which is
    // ample precision for normalized [-1,1]/[0,1] values and avoids
    // pulling in <format>/std::to_chars formatting-mode edge cases for
    // this simple, internal-tool use case.
    out += std::to_string(value);
}

} // namespace

std::string JsonlFormatter::EscapeString(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    out += "\\u";
                    AppendHex4(out, ch);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

std::string JsonlFormatter::FormatLine(const WheelSampleRecord& record) {
    std::string out;
    out.reserve(256);

    out += "{\"schemaVersion\":";
    out += std::to_string(record.schemaVersion);

    out += ",\"elapsedMilliseconds\":";
    out += std::to_string(record.elapsedMilliseconds);

    out += ",\"deviceId\":\"";
    AppendHexU64Upper(out, record.deviceId);
    out += "\"";

    out += ",\"backend\":";
    AppendJsonString(out, record.backend);

    out += ",\"connected\":";
    out += record.connected ? "true" : "false";

    out += ",\"valid\":";
    out += record.valid ? "true" : "false";

    out += ",\"sampleCounter\":";
    out += std::to_string(record.sampleCounter);

    out += ",\"steering\":";
    AppendFloat(out, record.steering);

    out += ",\"throttle\":";
    AppendFloat(out, record.throttle);

    out += ",\"brake\":";
    AppendFloat(out, record.brake);

    out += ",\"clutch\":";
    if (record.clutch.has_value()) {
        AppendFloat(out, *record.clutch);
    } else {
        out += "null";
    }

    out += ",\"pressedButtons\":";
    AppendJsonArrayOfInts(out, record.pressedButtons);

    out += ",\"povs\":";
    AppendJsonArrayOfStrings(out, record.povs);

    out += ",\"pollStatus\":";
    AppendJsonString(out, record.pollStatus);

    out += ",\"profileId\":";
    AppendJsonString(out, record.profileId);

    out += ",\"profileOrigin\":";
    AppendJsonString(out, record.profileOrigin);

    out += ",\"readinessState\":";
    AppendJsonString(out, record.readinessState);

    out += "}";
    return out;
}

} // namespace rvwheel::tools::probe
