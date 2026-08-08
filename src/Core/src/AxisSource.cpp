#include "rvwheel/dal/AxisSource.hpp"

namespace rvwheel::dal {

std::string_view ToString(AxisSource source) noexcept {
    switch (source) {
        case AxisSource::Unknown:
            return "Unknown";
        case AxisSource::X:
            return "X";
        case AxisSource::Y:
            return "Y";
        case AxisSource::Z:
            return "Z";
        case AxisSource::RotationX:
            return "Rx";
        case AxisSource::RotationY:
            return "Ry";
        case AxisSource::RotationZ:
            return "Rz";
        case AxisSource::Slider0:
            return "Slider0";
        case AxisSource::Slider1:
            return "Slider1";
    }
    return "Unknown";
}

std::optional<AxisSource> AxisSourceFromString(std::string_view text) noexcept {
    for (const AxisSource source : kAllAxisSources) {
        if (ToString(source) == text) {
            return source;
        }
    }
    return std::nullopt;
}

} // namespace rvwheel::dal
