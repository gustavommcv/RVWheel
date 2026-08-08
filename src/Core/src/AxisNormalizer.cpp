#include "rvwheel/dal/AxisNormalizer.hpp"

#include <algorithm>

namespace rvwheel::dal {

namespace {

[[nodiscard]] float ClampUnit(float value, float lo, float hi) noexcept {
    return std::clamp(value, lo, hi);
}

} // namespace

NormalizeResult AxisNormalizer::NormalizeSteering(std::int32_t raw, const AxisCalibration& calibration) noexcept {
    // A calibration that is degenerate on EITHER side is treated as invalid
    // as a whole, not just for samples that would fall on the degenerate
    // side: a zero-width segment almost always indicates a bad upstream
    // query (e.g. a driver returning a nonsensical DIPROPRANGE), and
    // reporting that deterministically here is more useful than silently
    // trusting whichever side happens to still be well-formed.
    const bool minDegenerate = (calibration.rawAtCenter == calibration.rawAtMin);
    const bool maxDegenerate = (calibration.rawAtCenter == calibration.rawAtMax);
    if (minDegenerate || maxDegenerate) {
        return NormalizeResult{0.0f, Status::InvalidArgument("Steering calibration has a zero-width range on at least one side")};
    }

    if (raw == calibration.rawAtCenter) {
        return NormalizeResult{0.0f, Status::Ok()};
    }

    // Decide which side of center the raw sample falls on without dividing.
    // "min side" means raw sits between rawAtMin and rawAtCenter; this
    // holds for both increasing (rawAtMin < rawAtCenter) and decreasing
    // (rawAtMin > rawAtCenter) orientations because both the sample and
    // rawAtMin are compared against rawAtCenter the same way.
    const bool rawBelowCenter = raw < calibration.rawAtCenter;
    const bool minBelowCenter = calibration.rawAtMin < calibration.rawAtCenter;
    const bool useMinSide = (rawBelowCenter == minBelowCenter);

    // Subtractions are done in double, not int32_t: raw and the calibration
    // extremes can each independently approach INT32_MIN/INT32_MAX (e.g. a
    // driver reporting a signed full-range axis alongside a caller passing
    // an out-of-range sample), and int32_t - int32_t between values far
    // apart in opposite directions can overflow, which is undefined
    // behavior. double exactly represents every std::int32_t value, so the
    // subtraction itself can never overflow here.
    const double rawD = static_cast<double>(raw);
    const double centerD = static_cast<double>(calibration.rawAtCenter);
    double value;
    if (useMinSide) {
        value = (rawD - centerD) / (centerD - static_cast<double>(calibration.rawAtMin));
    } else {
        value = (rawD - centerD) / (static_cast<double>(calibration.rawAtMax) - centerD);
    }

    return NormalizeResult{ClampUnit(static_cast<float>(value), -1.0f, 1.0f), Status::Ok()};
}

NormalizeResult AxisNormalizer::NormalizePedal(std::int32_t raw, const PedalCalibration& calibration) noexcept {
    if (calibration.rawAtPressed == calibration.rawAtReleased) {
        return NormalizeResult{0.0f, Status::InvalidArgument("Pedal calibration has a zero-width range")};
    }

    // See NormalizeSteering for why this is done in double rather than
    // int32_t: raw and the calibration extremes can be far enough apart in
    // opposite directions to overflow a plain int32_t subtraction.
    const double denom = static_cast<double>(calibration.rawAtPressed) - static_cast<double>(calibration.rawAtReleased);
    const double value = (static_cast<double>(raw) - static_cast<double>(calibration.rawAtReleased)) / denom;

    return NormalizeResult{ClampUnit(static_cast<float>(value), 0.0f, 1.0f), Status::Ok()};
}

} // namespace rvwheel::dal
