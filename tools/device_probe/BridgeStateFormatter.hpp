#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace rvwheel::tools::probe {

// Compact, versioned text record shared with the UE4SS Lua bridge. The
// sequence is written at both ends so a reader can reject a torn/partial
// frame without ever applying mixed-axis input.
struct BridgeStateRecord {
    std::uint64_t sequence = 0;
    bool connected = false;
    bool valid = false;
    float steering = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    float clutch = 0.0f;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    // DirectInput supports up to 128 buttons. Four fixed-width words keep the
    // transport compact and make lossless parsing straightforward in Lua.
    std::array<std::uint32_t, 4> buttonWords{};
};

class BridgeStateFormatter {
public:
    BridgeStateFormatter() = delete;

    [[nodiscard]] static std::string Format(const BridgeStateRecord& record);
};

} // namespace rvwheel::tools::probe
