#pragma once

#include <vector>

#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::ffb {

// Combines the independent contributions of one or more
// IForceFeedbackSource instances into a single command. Deliberately has
// no notion of "safety limits" -- ForceFeedbackSafetyController is the only
// component allowed to make a value smaller/slower for safety reasons; this
// class only defines how multiple *legitimate* requests combine.
//
// Combination rule, chosen so two sources can never accidentally double an
// effect's real-world strength:
//   - constantForce: summed (independent signed torque contributions add).
//   - spring / damper: the strongest single request wins (max), since these
//     map to one hardware condition effect per axis, not one per source.
//   - gain: the most conservative request wins (min).
// All outputs are clamped back into their documented domains before being
// returned, matching WheelTypes.hpp's ForceFeedbackCommand contract.
class ForceFeedbackMixer {
public:
    [[nodiscard]] rvwheel::dal::ForceFeedbackCommand Mix(
        const std::vector<rvwheel::dal::ForceFeedbackCommand>& contributions) const noexcept;
};

} // namespace rvwheel::ffb
