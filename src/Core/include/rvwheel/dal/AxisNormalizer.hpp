#pragma once

#include <cstdint>

#include "rvwheel/dal/Status.hpp"
#include "rvwheel/dal/WheelInputLayout.hpp"

namespace rvwheel::dal {

// Calibration for a steering-style axis: the raw value the underlying API
// reports at each of the three logical extremes. rawAtMin and rawAtMax are
// named after the *output* they map to (-1 and +1 respectively), not after
// their numeric magnitude, so a physically-inverted axis (where the raw
// value at steering = -1 is numerically greater than the value at
// steering = +1) is handled automatically by the same formula, with no
// separate "inverted" flag needed.
struct AxisCalibration {
    std::int32_t rawAtMin = 0;     // Raw value corresponding to normalized -1.
    std::int32_t rawAtCenter = 0;  // Raw value corresponding to normalized 0.
    std::int32_t rawAtMax = 0;     // Raw value corresponding to normalized +1.
};

// Calibration for a pedal-style axis: the raw value at each of the two
// logical extremes. rawAtReleased may be numerically greater than
// rawAtPressed (inverted pedal wiring); the formula handles both cases
// identically.
struct PedalCalibration {
    std::int32_t rawAtReleased = 0; // Raw value corresponding to normalized 0.
    std::int32_t rawAtPressed = 0;  // Raw value corresponding to normalized 1.
};

struct NormalizeResult {
    float value = 0.0f;
    Status status = Status::Ok();
};

// Pure, hardware-independent axis normalization. No deadzone, curve,
// inversion-flag, or smoothing logic lives here on purpose — those are
// Layer 3 (Input Mapper/Profiles) concerns. This component only answers
// "given this raw sample and this calibration, what is the normalized
// value", deterministically and without ever producing NaN/Inf.
class AxisNormalizer {
public:
    AxisNormalizer() = delete;

    // Maps [rawAtMin, rawAtCenter] -> [-1, 0] and [rawAtCenter, rawAtMax] ->
    // [0, 1], supporting asymmetric ranges (|rawAtCenter - rawAtMin| may
    // differ from |rawAtMax - rawAtCenter|). The result is always clamped to
    // [-1, 1]. If the calibration is degenerate on either side (rawAtMin ==
    // rawAtCenter or rawAtCenter == rawAtMax), the whole calibration is
    // treated as invalid and the result is InvalidArgument with a
    // deterministic value of 0.0f, regardless of which side the raw sample
    // would otherwise fall on.
    [[nodiscard]] static NormalizeResult NormalizeSteering(std::int32_t raw,
                                                            const AxisCalibration& calibration) noexcept;

    // Maps [rawAtReleased, rawAtPressed] -> [0, 1], clamped, including when
    // the axis is physically inverted (rawAtPressed < rawAtReleased). If the
    // calibration is degenerate (rawAtReleased == rawAtPressed), the result
    // is InvalidArgument with a deterministic value of 0.0f.
    [[nodiscard]] static NormalizeResult NormalizePedal(std::int32_t raw,
                                                         const PedalCalibration& calibration) noexcept;

    // Turns a runtime-queried raw range plus a profile's AxisDirection
    // into calibration a backend can hand straight to NormalizeSteering/
    // NormalizePedal. This is the ONLY place "direction" is combined with
    // an actual raw range in the whole project -- backends call this
    // rather than re-deriving the released/pressed (or min/max) swap
    // themselves, so the swap logic is tested once, directly, instead of
    // duplicated and re-verified per backend.
    [[nodiscard]] static PedalCalibration ResolvePedalCalibration(std::int32_t rawMin, std::int32_t rawMax,
                                                                    AxisDirection direction) noexcept {
        return (direction == AxisDirection::Normal) ? PedalCalibration{rawMin, rawMax} : PedalCalibration{rawMax, rawMin};
    }

    [[nodiscard]] static AxisCalibration ResolveSteeringCalibration(std::int32_t rawMin, std::int32_t rawMax,
                                                                      AxisDirection direction) noexcept {
        const auto center = static_cast<std::int32_t>((static_cast<std::int64_t>(rawMin) + rawMax) / 2);
        return (direction == AxisDirection::Normal) ? AxisCalibration{rawMin, center, rawMax}
                                                      : AxisCalibration{rawMax, center, rawMin};
    }
};

} // namespace rvwheel::dal
