#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace rvwheel::dal {

// Opaque, comparable identifier for a physical device as seen by the DAL.
//
// A DeviceId is derived by each backend/enumerator from the most stable
// identity data the underlying API exposes (e.g. a DirectInput instance GUID,
// or a Logitech SDK index combined with VID/PID when available). It is
// stable across DeviceManager refreshes on the same machine for the same
// physical device *as long as the underlying API keeps reporting the same
// identity data*. It is NOT guaranteed stable across reboots, driver
// reinstalls, or different USB ports for backends whose native identity is
// tied to enumeration order (see LogitechDevice for a documented exception).
class DeviceId {
public:
    constexpr DeviceId() noexcept = default;

    [[nodiscard]] static constexpr DeviceId FromValue(std::uint64_t value) noexcept {
        return DeviceId{value, true};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept { return valid_; }
    [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return value_; }

    [[nodiscard]] friend constexpr bool operator==(const DeviceId& lhs, const DeviceId& rhs) noexcept {
        return lhs.valid_ == rhs.valid_ && (!lhs.valid_ || lhs.value_ == rhs.value_);
    }

    [[nodiscard]] friend constexpr bool operator!=(const DeviceId& lhs, const DeviceId& rhs) noexcept {
        return !(lhs == rhs);
    }

    [[nodiscard]] friend constexpr bool operator<(const DeviceId& lhs, const DeviceId& rhs) noexcept {
        if (lhs.valid_ != rhs.valid_) {
            return !lhs.valid_;
        }
        return lhs.value_ < rhs.value_;
    }

private:
    constexpr DeviceId(std::uint64_t value, bool valid) noexcept : value_(value), valid_(valid) {}

    std::uint64_t value_ = 0;
    bool valid_ = false;
};

// FNV-1a over raw bytes. Used by backends to derive a DeviceId from whatever
// stable identity bytes their underlying API exposes (GUID bytes, VID/PID,
// backend tag, etc). Not cryptographic; only needs to be a decent, fast,
// deterministic mixer for identity data within a single machine/session.
[[nodiscard]] constexpr std::uint64_t Fnv1aHash(const unsigned char* data, std::size_t size) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t prime = 0x100000001b3ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(data[i]);
        hash *= prime;
    }
    return hash;
}

} // namespace rvwheel::dal

namespace std {

template <>
struct hash<rvwheel::dal::DeviceId> {
    std::size_t operator()(const rvwheel::dal::DeviceId& id) const noexcept {
        return static_cast<std::size_t>(id.Value()) ^ (id.IsValid() ? 0 : ~std::size_t{0});
    }
};

} // namespace std
