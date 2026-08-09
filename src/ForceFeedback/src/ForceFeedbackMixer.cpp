#include "rvwheel/ffb/ForceFeedbackMixer.hpp"

#include <algorithm>
#include <cmath>

namespace rvwheel::ffb {

namespace {
using rvwheel::dal::ForceFeedbackCommand;

[[nodiscard]] float SanitizedOrZero(float value) noexcept { return std::isfinite(value) ? value : 0.0f; }
} // namespace

ForceFeedbackCommand ForceFeedbackMixer::Mix(const std::vector<ForceFeedbackCommand>& contributions) const noexcept {
    if (contributions.empty()) {
        return ForceFeedbackCommand{0.0f, 0.0f, 0.0f, 0.0f};
    }

    float constantForceSum = 0.0f;
    float springMax = 0.0f;
    float damperMax = 0.0f;
    float gainMin = 1.0f;

    for (const ForceFeedbackCommand& contribution : contributions) {
        constantForceSum += SanitizedOrZero(contribution.constantForce);
        springMax = std::max(springMax, SanitizedOrZero(contribution.spring));
        damperMax = std::max(damperMax, SanitizedOrZero(contribution.damper));
        gainMin = std::min(gainMin, SanitizedOrZero(contribution.gain));
    }

    ForceFeedbackCommand mixed{};
    mixed.constantForce = std::clamp(constantForceSum, -1.0f, 1.0f);
    mixed.spring = std::clamp(springMax, 0.0f, 1.0f);
    mixed.damper = std::clamp(damperMax, 0.0f, 1.0f);
    mixed.gain = std::clamp(gainMin, 0.0f, 1.0f);
    return mixed;
}

} // namespace rvwheel::ffb
