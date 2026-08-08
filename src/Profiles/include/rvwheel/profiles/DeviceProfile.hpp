#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "rvwheel/dal/DeviceReadinessPolicy.hpp"
#include "rvwheel/dal/WheelInputLayout.hpp"
#include "rvwheel/dal/WheelTypes.hpp"

namespace rvwheel::profiles {

struct ProfileMatchCriteria {
    rvwheel::dal::DeviceBackend backend = rvwheel::dal::DeviceBackend::DirectInput;
    // Always both present or both absent (enforced by ProfileLoader): a
    // profile either targets an exact vendor/product pair or is generic
    // for its backend. There is no partial/asymmetric match.
    std::optional<std::uint16_t> vendorId;
    std::optional<std::uint16_t> productId;
};

// A fully parsed, schema-validated device profile -- see ProfileLoader for
// the JSON shape and validation rules. Matching-time source-existence
// validation (does this device actually have the axes this profile
// references) happens separately in ProfileResolver, since that requires a
// real device's discovered axes, not just the JSON document.
struct DeviceProfile {
    int schemaVersion = 1;
    std::string profileId;
    std::string displayName;
    ProfileMatchCriteria match;
    rvwheel::dal::WheelInputLayout layout;
    rvwheel::dal::DeviceReadinessPolicy readiness;

    // Optional sanity-check hints. Never used to reject or weaken a match
    // -- only to flag a mismatch worth telling the user about (e.g. "this
    // profile expects 25 buttons but the device reports 20").
    std::optional<std::uint16_t> expectedButtonCount;
    std::optional<std::uint8_t> expectedPovCount;
};

// A profile paired with where it came from, as produced by
// ProfileRepository::MergeProfiles(). isUserProfile distinguishes a user
// override (which wins ties on profileId against a built-in) from a
// built-in profile shipped with RVWheel.
struct ProfileWithOrigin {
    DeviceProfile profile;
    bool isUserProfile = false;
};

} // namespace rvwheel::profiles
