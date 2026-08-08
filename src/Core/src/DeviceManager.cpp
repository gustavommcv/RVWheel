#include "rvwheel/dal/DeviceManager.hpp"

#include <algorithm>
#include <iterator>

namespace rvwheel::dal {

namespace {

struct VidPidKey {
    std::uint16_t vendorId;
    std::uint16_t productId;

    [[nodiscard]] friend bool operator==(const VidPidKey& lhs, const VidPidKey& rhs) noexcept {
        return lhs.vendorId == rhs.vendorId && lhs.productId == rhs.productId;
    }
};

// Logitech is preferred over DirectInput for the same physical device: the
// Logitech SDK exposes richer/vendor-tuned force feedback for its own
// hardware, so when both backends can see the same wheel we keep the
// Logitech-backed instance and drop the DirectInput one.
[[nodiscard]] bool PrefersOver(DeviceBackend incoming, DeviceBackend existing) noexcept {
    return incoming == DeviceBackend::Logitech && existing == DeviceBackend::DirectInput;
}

// Cross-backend deduplication. Only candidates that expose BOTH vendor and
// product IDs can be matched against each other; a candidate missing either
// is kept as-is because matching on display name alone is unreliable (many
// devices share generic names, and localized/driver-specific name strings
// differ between DirectInput and the Logitech SDK for the very same unit).
//
// Known limitation: two distinct physical devices that happen to share the
// same VID/PID (same model, no serial number exposed by either API) cannot
// be told apart and will be incorrectly merged into one. Neither
// DirectInput nor the Logitech SDK reliably expose a per-unit serial number
// for wheels, so this is an inherent limitation of the MVP, not a bug to be
// fixed by better matching logic alone.
[[nodiscard]] std::vector<std::unique_ptr<IWheelDevice>> DeduplicateAcrossBackends(
    std::vector<std::unique_ptr<IWheelDevice>> candidates) {
    std::vector<std::unique_ptr<IWheelDevice>> result;
    std::vector<VidPidKey> resultKeys; // Parallel to `result`; only meaningful where both IDs are known.
    resultKeys.reserve(candidates.size());

    for (auto& candidate : candidates) {
        const DeviceInfo& info = candidate->Info();
        if (!info.vendorId.has_value() || !info.productId.has_value()) {
            result.push_back(std::move(candidate));
            resultKeys.push_back(VidPidKey{0, 0});
            continue;
        }

        const VidPidKey key{*info.vendorId, *info.productId};

        std::size_t matchIndex = result.size();
        for (std::size_t i = 0; i < result.size(); ++i) {
            const DeviceInfo& existingInfo = result[i]->Info();
            const bool existingHasKey = existingInfo.vendorId.has_value() && existingInfo.productId.has_value();
            if (existingHasKey && resultKeys[i] == key) {
                matchIndex = i;
                break;
            }
        }

        if (matchIndex == result.size()) {
            resultKeys.push_back(key);
            result.push_back(std::move(candidate));
            continue;
        }

        if (PrefersOver(info.backend, result[matchIndex]->Info().backend)) {
            result[matchIndex] = std::move(candidate);
        }
        // Otherwise the candidate is dropped (destroyed) in favor of the existing entry.
    }

    return result;
}

} // namespace

DeviceManager::DeviceManager(std::vector<std::unique_ptr<IDeviceEnumerator>> enumerators,
                              std::chrono::milliseconds refreshInterval,
                              Clock clock,
                              DiagnosticSink diagnostics)
    : enumerators_(std::move(enumerators)),
      refreshInterval_(refreshInterval),
      clock_(std::move(clock)),
      diagnostics_(std::move(diagnostics)) {}

void DeviceManager::MergeCandidates(std::vector<std::unique_ptr<IWheelDevice>> candidates) {
    std::vector<std::unique_ptr<IWheelDevice>> deduped = DeduplicateAcrossBackends(std::move(candidates));

    for (auto& candidate : deduped) {
        const DeviceId id = candidate->Info().id;
        const auto existingIt = std::find_if(devices_.begin(), devices_.end(), [&](const std::unique_ptr<IWheelDevice>& d) {
            return d->Info().id == id;
        });

        if (existingIt == devices_.end()) {
            devices_.push_back(std::move(candidate));
        }
        // Else: a device with this identity is already known. Keep the
        // existing instance untouched (preserves object identity/handles
        // across refreshes) and let the new candidate be destroyed.
    }
}

Status DeviceManager::ForceRefresh() {
    std::vector<std::unique_ptr<IWheelDevice>> allCandidates;
    for (auto& enumerator : enumerators_) {
        auto found = enumerator->Enumerate();
        allCandidates.insert(allCandidates.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    }

    MergeCandidates(std::move(allCandidates));

    lastRefresh_ = clock_();
    hasRefreshedOnce_ = true;
    return Status::Ok();
}

Status DeviceManager::RefreshIfDue() {
    if (hasRefreshedOnce_) {
        const auto now = clock_();
        if (now - lastRefresh_ < refreshInterval_) {
            return Status::Ok();
        }
    }
    return ForceRefresh();
}

} // namespace rvwheel::dal
