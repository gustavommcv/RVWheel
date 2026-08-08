#include "ProbeFormatting.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace rvwheel::tools::probe {

using rvwheel::dal::DeviceBackend;
using rvwheel::dal::DeviceId;
using rvwheel::dal::PovDirection;
using rvwheel::dal::StatusCode;

std::string FormatDeviceIdHex(const DeviceId& id) {
    if (!id.IsValid()) {
        return "0x0000000000000000(invalid)";
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << id.Value();
    return oss.str();
}

std::string FormatBackend(DeviceBackend backend) {
    switch (backend) {
        case DeviceBackend::DirectInput:
            return "DirectInput";
        case DeviceBackend::Logitech:
            return "Logitech";
    }
    return "Unknown";
}

std::string FormatVendorProductId(const std::optional<std::uint16_t>& vendorId, const std::optional<std::uint16_t>& productId) {
    std::ostringstream oss;
    oss << "VID=";
    if (vendorId) {
        oss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << *vendorId;
    } else {
        oss << "unknown";
    }
    oss << std::dec << " PID=";
    if (productId) {
        oss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << *productId;
    } else {
        oss << "unknown";
    }
    return oss.str();
}

std::string FormatReadinessState(rvwheel::dal::ReadinessState state) {
    using rvwheel::dal::ReadinessState;
    switch (state) {
        case ReadinessState::Unconfigured:
            return "Unconfigured";
        case ReadinessState::WarmingUp:
            return "WarmingUp";
        case ReadinessState::Stabilizing:
            return "Stabilizing";
        case ReadinessState::Ready:
            return "Ready";
        case ReadinessState::TimedOut:
            return "TimedOut";
    }
    return "Unknown";
}

std::string FormatProfileOrigin(rvwheel::profiles::ProfileOrigin origin) {
    using rvwheel::profiles::ProfileOrigin;
    switch (origin) {
        case ProfileOrigin::BuiltInProfile:
            return "BuiltInProfile";
        case ProfileOrigin::UserProfile:
            return "UserProfile";
        case ProfileOrigin::ProvisionalFallback:
            return "ProvisionalFallback";
        case ProfileOrigin::Unconfigured:
            return "Unconfigured";
        case ProfileOrigin::AmbiguousMatch:
            return "AmbiguousMatch";
        case ProfileOrigin::InvalidExactMatch:
            return "InvalidExactMatch";
    }
    return "Unknown";
}

std::string FormatStatusCode(StatusCode code) {
    switch (code) {
        case StatusCode::Success:
            return "Ok";
        case StatusCode::NotConnected:
            return "NotConnected";
        case StatusCode::NotSupported:
            return "NotSupported";
        case StatusCode::InvalidArgument:
            return "InvalidArgument";
        case StatusCode::BackendError:
            return "BackendError";
    }
    return "Unknown";
}

std::vector<int> PressedButtonIndices(const rvwheel::dal::ButtonMask& buttons, std::uint16_t buttonCount) {
    std::vector<int> indices;
    const std::size_t limit = std::min<std::size_t>(buttonCount, buttons.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (buttons[i]) {
            indices.push_back(static_cast<int>(i));
        }
    }
    return indices;
}

std::string FormatPovDirection(PovDirection direction) {
    switch (direction) {
        case PovDirection::Centered:
            return "Centered";
        case PovDirection::North:
            return "North";
        case PovDirection::NorthEast:
            return "NorthEast";
        case PovDirection::East:
            return "East";
        case PovDirection::SouthEast:
            return "SouthEast";
        case PovDirection::South:
            return "South";
        case PovDirection::SouthWest:
            return "SouthWest";
        case PovDirection::West:
            return "West";
        case PovDirection::NorthWest:
            return "NorthWest";
    }
    return "Unknown";
}

std::vector<std::string> FormatActivePovs(const std::array<PovDirection, rvwheel::dal::kMaxPovCount>& povs, std::uint8_t povCount) {
    std::vector<std::string> result;
    const std::size_t limit = std::min<std::size_t>(povCount, povs.size());
    for (std::size_t i = 0; i < limit; ++i) {
        result.push_back(FormatPovDirection(povs[i]));
    }
    return result;
}

} // namespace rvwheel::tools::probe
