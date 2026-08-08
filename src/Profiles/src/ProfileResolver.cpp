#include "rvwheel/profiles/ProfileResolver.hpp"

#include <algorithm>

namespace rvwheel::profiles {

namespace {

namespace dal = rvwheel::dal;

using dal::AxisBinding;
using dal::AxisCenterPolicy;
using dal::AxisDirection;
using dal::AxisSource;
using dal::DeviceReadinessPolicy;
using dal::WheelInputLayout;

enum class Specificity : int {
    None = 0,
    BackendOnly = 1,
    ExactVendorProduct = 2,
};

[[nodiscard]] Specificity Classify(const ProfileMatchCriteria& match, const dal::DeviceInfo& device) noexcept {
    if (match.backend != device.backend) {
        return Specificity::None;
    }
    if (match.vendorId && match.productId) {
        if (device.vendorId == match.vendorId && device.productId == match.productId) {
            return Specificity::ExactVendorProduct;
        }
        return Specificity::None; // Specified but does not match this device: not a candidate.
    }
    return Specificity::BackendOnly; // Generic profile for this backend.
}

[[nodiscard]] bool HasSource(const std::vector<AxisSource>& knownAxes, AxisSource source) noexcept {
    return std::find(knownAxes.begin(), knownAxes.end(), source) != knownAxes.end();
}

[[nodiscard]] bool LayoutSourcesAllExist(const WheelInputLayout& layout, const std::vector<AxisSource>& knownAxes) noexcept {
    if (layout.steering && !HasSource(knownAxes, layout.steering->source)) return false;
    if (layout.throttle && !HasSource(knownAxes, layout.throttle->source)) return false;
    if (layout.brake && !HasSource(knownAxes, layout.brake->source)) return false;
    if (layout.clutch && !HasSource(knownAxes, layout.clutch->source)) return false;
    return true;
}

} // namespace

WheelInputLayout ProfileResolver::GenericFallbackLayout(const std::vector<AxisSource>& knownAxes) {
    WheelInputLayout layout;
    if (HasSource(knownAxes, AxisSource::X)) {
        layout.steering = AxisBinding{AxisSource::X, AxisDirection::Normal, AxisCenterPolicy::RangeMidpoint};
    }
    if (HasSource(knownAxes, AxisSource::Y)) {
        layout.throttle = AxisBinding{AxisSource::Y, AxisDirection::Normal, std::nullopt};
    }
    if (HasSource(knownAxes, AxisSource::RotationZ)) {
        layout.brake = AxisBinding{AxisSource::RotationZ, AxisDirection::Normal, std::nullopt};
    }
    if (HasSource(knownAxes, AxisSource::Slider0)) {
        layout.clutch = AxisBinding{AxisSource::Slider0, AxisDirection::Normal, std::nullopt};
    }
    return layout;
}

ProfileResolution ProfileResolver::Resolve(const std::vector<ProfileWithOrigin>& profiles, const dal::DeviceInfo& device,
                                            const std::vector<AxisSource>& knownAxes) {
    Specificity best = Specificity::None;
    std::vector<const ProfileWithOrigin*> bestMatches;

    for (const auto& entry : profiles) {
        const Specificity spec = Classify(entry.profile.match, device);
        if (spec == Specificity::None) {
            continue;
        }
        if (static_cast<int>(spec) > static_cast<int>(best)) {
            best = spec;
            bestMatches.clear();
            bestMatches.push_back(&entry);
        } else if (spec == best) {
            bestMatches.push_back(&entry);
        }
    }

    ProfileResolution resolution;

    if (bestMatches.empty()) {
        const WheelInputLayout fallback = GenericFallbackLayout(knownAxes);
        if (fallback.IsEmpty()) {
            resolution.origin = ProfileOrigin::Unconfigured;
            resolution.reason = "No profile matched this device's backend/VID/PID, and no axis this backend's generic "
                                 "fallback recognizes was found on the device.";
        } else {
            resolution.origin = ProfileOrigin::ProvisionalFallback;
            resolution.layout = fallback;
            resolution.readiness = DeviceReadinessPolicy::ConservativeDefault();
            resolution.reason = "No profile matched this device's backend/VID/PID; applying the generic fallback "
                                 "heuristic provisionally.";
        }
        return resolution;
    }

    if (bestMatches.size() > 1) {
        std::string ids;
        for (const auto* entry : bestMatches) {
            if (!ids.empty()) {
                ids += ", ";
            }
            ids += entry->profile.profileId;
        }
        resolution.origin = ProfileOrigin::AmbiguousMatch;
        resolution.reason = "Multiple profiles tie at the highest match priority: " + ids + ". Remove or rename one.";
        return resolution;
    }

    const ProfileWithOrigin& matched = *bestMatches.front();
    if (!LayoutSourcesAllExist(matched.profile.layout, knownAxes)) {
        if (best == Specificity::ExactVendorProduct) {
            resolution.origin = ProfileOrigin::InvalidExactMatch;
            resolution.reason = "Profile \"" + matched.profile.profileId +
                                 "\" exactly matches this device's VID/PID but references an axis source this device "
                                 "does not have; refusing to fall back silently after an exact match.";
            return resolution;
        }

        // A generic (backend-only) profile not fitting this specific
        // device is not the same severity as an exact-match mismatch:
        // fall through to the generic heuristic instead of failing hard.
        const WheelInputLayout fallback = GenericFallbackLayout(knownAxes);
        if (fallback.IsEmpty()) {
            resolution.origin = ProfileOrigin::Unconfigured;
            resolution.reason = "The matching generic profile \"" + matched.profile.profileId +
                                 "\" references axes this device lacks, and the fallback heuristic found nothing "
                                 "usable either.";
        } else {
            resolution.origin = ProfileOrigin::ProvisionalFallback;
            resolution.layout = fallback;
            resolution.readiness = DeviceReadinessPolicy::ConservativeDefault();
            resolution.reason = "The matching generic profile \"" + matched.profile.profileId +
                                 "\" references axes this device lacks; applying the generic fallback heuristic instead.";
        }
        return resolution;
    }

    resolution.origin = matched.isUserProfile ? ProfileOrigin::UserProfile : ProfileOrigin::BuiltInProfile;
    resolution.profile = matched.profile;
    resolution.layout = matched.profile.layout;
    resolution.readiness = matched.profile.readiness;
    resolution.reason = "Matched profile \"" + matched.profile.profileId + "\" (" +
                         (best == Specificity::ExactVendorProduct ? std::string("exact VID/PID match")
                                                                   : std::string("generic backend match")) +
                         ").";
    return resolution;
}

} // namespace rvwheel::profiles
