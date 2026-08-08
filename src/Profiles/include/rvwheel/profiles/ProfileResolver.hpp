#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rvwheel/dal/AxisSource.hpp"
#include "rvwheel/dal/WheelTypes.hpp"
#include "rvwheel/profiles/DeviceProfile.hpp"

namespace rvwheel::profiles {

enum class ProfileOrigin : std::uint8_t {
    BuiltInProfile,
    UserProfile,
    // Generic X/Y/Rz/Slider0 heuristic applied because nothing more
    // specific matched; not a verified profile for this exact hardware.
    ProvisionalFallback,
    // Nothing at all could be resolved (no profile matched and the
    // generic fallback found no axis it recognizes).
    Unconfigured,
    // More than one profile tied at the same highest match priority.
    // Never resolved by picking one arbitrarily.
    AmbiguousMatch,
    // A profile exactly matches this device's backend+VID+PID but
    // references an axis source the device does not actually have.
    // Never silently downgraded to the generic fallback -- an exact
    // match that is wrong is a profile bug, not "unknown device".
    InvalidExactMatch,
};

struct ProfileResolution {
    ProfileOrigin origin = ProfileOrigin::Unconfigured;
    std::optional<DeviceProfile> profile; // Set only for BuiltInProfile/UserProfile.
    rvwheel::dal::WheelInputLayout layout;
    rvwheel::dal::DeviceReadinessPolicy readiness;
    std::string reason; // Human-readable rationale, for --list verbose and diagnostics.
};

// Backend-agnostic matching: never touches HWND/COM/DirectInput types,
// only rvwheel::dal::DeviceInfo (identity+capabilities) and a device's
// discovered raw axis sources. See DeviceProfile.hpp/ProfileLoader for how
// profiles get here and WheelInputLayout for what comes out.
class ProfileResolver {
public:
    ProfileResolver() = delete;

    // `profiles`: candidates, e.g. from ProfileRepository::MergedProfiles().
    // `device`: identity to match against (backend, vendorId, productId).
    // `knownAxes`: this specific device's actually-discovered raw axis
    //   sources (e.g. from ICalibratableWheelDevice::EnumerateRawAxes()),
    //   used to reject a matched profile whose axes don't actually exist
    //   on this device rather than trusting the JSON blindly.
    [[nodiscard]] static ProfileResolution Resolve(const std::vector<ProfileWithOrigin>& profiles,
                                                    const rvwheel::dal::DeviceInfo& device,
                                                    const std::vector<rvwheel::dal::AxisSource>& knownAxes);

    // The one and only generic axis-role guess in this project: X/Y/Rz/
    // Slider0 -> steering/throttle/brake/clutch, direction Normal, only
    // for sources actually present in `knownAxes`. Used solely as the
    // ProvisionalFallback path in Resolve(); never applied without being
    // reported as provisional to the caller.
    [[nodiscard]] static rvwheel::dal::WheelInputLayout GenericFallbackLayout(
        const std::vector<rvwheel::dal::AxisSource>& knownAxes);
};

} // namespace rvwheel::profiles
