#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rvwheel/dal/DeviceId.hpp"
#include "rvwheel/dal/ReadinessState.hpp"
#include "rvwheel/dal/Status.hpp"
#include "rvwheel/dal/WheelTypes.hpp"
#include "rvwheel/profiles/ProfileResolver.hpp"

namespace rvwheel::tools::probe {

[[nodiscard]] std::string FormatDeviceIdHex(const rvwheel::dal::DeviceId& id);
[[nodiscard]] std::string FormatBackend(rvwheel::dal::DeviceBackend backend);
[[nodiscard]] std::string FormatVendorProductId(const std::optional<std::uint16_t>& vendorId,
                                                 const std::optional<std::uint16_t>& productId);
[[nodiscard]] std::string FormatStatusCode(rvwheel::dal::StatusCode code);
[[nodiscard]] std::string FormatReadinessState(rvwheel::dal::ReadinessState state);
[[nodiscard]] std::string FormatProfileOrigin(rvwheel::profiles::ProfileOrigin origin);

// Returns the 0-based indices of every currently pressed button, but never
// looks past `buttonCount` (the device's reported capability) even though
// `buttons` physically has room for kMaxButtons bits. This is the one
// place that enforces "never read past reported capability" for buttons;
// both the console renderer and the JSONL writer go through it.
[[nodiscard]] std::vector<int> PressedButtonIndices(const rvwheel::dal::ButtonMask& buttons, std::uint16_t buttonCount);

[[nodiscard]] std::string FormatPovDirection(rvwheel::dal::PovDirection direction);

// Same capability-bounding rule as PressedButtonIndices, for POVs.
[[nodiscard]] std::vector<std::string> FormatActivePovs(
    const std::array<rvwheel::dal::PovDirection, rvwheel::dal::kMaxPovCount>& povs, std::uint8_t povCount);

} // namespace rvwheel::tools::probe
