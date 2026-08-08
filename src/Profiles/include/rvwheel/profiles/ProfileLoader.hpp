#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rvwheel/profiles/DeviceProfile.hpp"

namespace rvwheel::profiles {

struct ProfileParseError {
    std::string path; // JSON-pointer-ish field path, e.g. "axes.throttle.source"; empty for document-level errors.
    std::string message;
};

struct ProfileParseResult {
    std::optional<DeviceProfile> profile; // Set only when errors is empty.
    std::vector<ProfileParseError> errors;

    [[nodiscard]] bool IsOk() const noexcept { return profile.has_value(); }
};

// JSON parsing and schema validation for device profiles (see
// configs/default_profiles/README.md for the schema and an authored
// example). Uses nlohmann::json internally; this is the ONLY place in the
// project that does, by design -- the DAL never depends on a JSON
// library, and neither does anything outside this loader/serializer pair.
//
// Validation is strict and never falls back silently: a malformed
// document, an unsupported schemaVersion, an unknown backend/axis
// token/direction string, a duplicate axis source, or an out-of-range
// readiness time all produce a ProfileParseError with the specific field
// path, rather than a partially-populated DeviceProfile.
class ProfileLoader {
public:
    ProfileLoader() = delete;

    [[nodiscard]] static ProfileParseResult ParseFromString(std::string_view jsonText);
    [[nodiscard]] static ProfileParseResult ParseFromFile(const std::filesystem::path& path);

    // Round-trips a DeviceProfile back to pretty-printed JSON matching the
    // same schema ParseFromString accepts. Used by the calibration wizard
    // to write a generated profile, and by tests to check round-tripping.
    [[nodiscard]] static std::string Serialize(const DeviceProfile& profile);
};

} // namespace rvwheel::profiles
