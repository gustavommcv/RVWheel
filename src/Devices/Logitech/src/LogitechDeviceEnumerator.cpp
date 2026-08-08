#include "LogitechDeviceEnumerator.hpp"

#include <cstring>
#include <optional>
#include <utility>

#include "rvwheel/devices/LogitechDevice.hpp"

namespace rvwheel::devices {

namespace {

using rvwheel::dal::DeviceBackend;
using rvwheel::dal::DeviceId;
using rvwheel::dal::Fnv1aHash;
using rvwheel::dal::LogLevel;

// Unlike DirectInput's instance GUID, the Logitech SDK's simple index-based
// API does not expose a persistent per-unit identifier. When VID/PID are
// available we hash those (stable across replug/reorder as long as the
// model doesn't change); otherwise we fall back to hashing the SDK index
// alone, which is only stable while enumeration order does not change
// across refreshes (e.g. no other Logitech device is plugged/unplugged in
// between). This is a documented limitation, not an oversight.
[[nodiscard]] DeviceId MakeLogitechDeviceId(std::size_t sdkIndex, const std::optional<std::uint16_t>& vendorId,
                                             const std::optional<std::uint16_t>& productId) noexcept {
    unsigned char bytes[1 + sizeof(std::uint16_t) * 2 + sizeof(std::size_t)];
    std::size_t offset = 0;
    bytes[offset++] = static_cast<unsigned char>(DeviceBackend::Logitech);

    if (vendorId.has_value() && productId.has_value()) {
        const std::uint16_t vid = *vendorId;
        const std::uint16_t pid = *productId;
        std::memcpy(bytes + offset, &vid, sizeof(vid));
        offset += sizeof(vid);
        std::memcpy(bytes + offset, &pid, sizeof(pid));
        offset += sizeof(pid);
    } else {
        std::memcpy(bytes + offset, &sdkIndex, sizeof(sdkIndex));
        offset += sizeof(sdkIndex);
    }

    return DeviceId::FromValue(Fnv1aHash(bytes, offset));
}

} // namespace

LogitechDeviceEnumerator::LogitechDeviceEnumerator(std::shared_ptr<ILogitechSdk> sdk, rvwheel::dal::DiagnosticSink diagnostics)
    : sdk_(std::move(sdk)), diagnostics_(std::move(diagnostics)) {}

std::vector<std::unique_ptr<rvwheel::dal::IWheelDevice>> LogitechDeviceEnumerator::Enumerate() {
    namespace dal = rvwheel::dal;
    std::vector<std::unique_ptr<dal::IWheelDevice>> result;

    if (!sdk_) {
        return result;
    }

    if (!initialized_) {
        if (initFailedOnce_) {
            return result;
        }
        const dal::Status status = sdk_->Initialize();
        if (!status.IsOk()) {
            initFailedOnce_ = true;
            diagnostics_(LogLevel::Error, "Logitech SDK Initialize() failed: " + status.Message());
            return result;
        }
        initialized_ = true;
    }

    const std::size_t count = sdk_->DeviceCount();
    for (std::size_t i = 0; i < count; ++i) {
        if (!sdk_->IsConnected(i)) {
            continue;
        }

        const auto identity = sdk_->GetIdentity(i);
        const auto capabilities = sdk_->GetCapabilities(i);
        if (!identity || !capabilities) {
            diagnostics_(LogLevel::Warning, "Logitech SDK reported a connected device with no identity/capabilities; skipping");
            continue;
        }

        dal::DeviceInfo info{};
        info.id = MakeLogitechDeviceId(i, identity->vendorId, identity->productId);
        info.name = identity->name;
        info.manufacturer = identity->manufacturer;
        info.backend = dal::DeviceBackend::Logitech;
        info.vendorId = identity->vendorId;
        info.productId = identity->productId;
        info.capabilities = *capabilities;

        result.push_back(std::make_unique<LogitechDevice>(std::move(info), sdk_, i, diagnostics_));
    }

    return result;
}

} // namespace rvwheel::devices
