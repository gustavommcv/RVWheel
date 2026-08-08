#include "rvwheel/profiles/ProfileLoader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "rvwheel/dal/AxisSource.hpp"

namespace rvwheel::profiles {

namespace {

namespace dal = rvwheel::dal;

using dal::AxisBinding;
using dal::AxisCenterPolicy;
using dal::AxisDirection;
using dal::AxisSource;

constexpr int kSupportedSchemaVersion = 1;
constexpr long long kMaxReadinessMilliseconds = 60000; // 60s: a generous, documented sanity bound, not a real device's expected value.

[[nodiscard]] std::optional<std::uint16_t> ParseHexUint16(std::string_view text) noexcept {
    std::string_view digits = text;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits = digits.substr(2);
    }
    if (digits.empty() || digits.size() > 4) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (char ch : digits) {
        unsigned digit;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<unsigned>(ch - 'a') + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<unsigned>(ch - 'A') + 10;
        } else {
            return std::nullopt;
        }
        value = value * 16 + digit;
    }
    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] std::string FormatHexUint16(std::uint16_t value) {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";
    std::string out = "0x";
    out.push_back(kHexDigits[(value >> 12) & 0xF]);
    out.push_back(kHexDigits[(value >> 8) & 0xF]);
    out.push_back(kHexDigits[(value >> 4) & 0xF]);
    out.push_back(kHexDigits[value & 0xF]);
    return out;
}

// Accumulates ProfileParseError entries while walking the document, so one
// parse pass can report every problem found rather than stopping at the
// first one.
class Validator {
public:
    explicit Validator(std::vector<ProfileParseError>& errors) : errors_(errors) {}

    void Fail(const std::string& path, const std::string& message) { errors_.push_back({path, message}); }

    [[nodiscard]] bool HasErrors() const noexcept { return !errors_.empty(); }

private:
    std::vector<ProfileParseError>& errors_;
};

void ParseAxis(const nlohmann::json& root, const char* roleName, std::optional<AxisBinding>& outBinding,
               std::vector<AxisSource>& usedSources, Validator& validator) {
    if (!root.contains("axes") || !root["axes"].contains(roleName)) {
        return; // Role simply absent from this profile; that's valid.
    }
    const std::string path = std::string("axes.") + roleName;
    const auto& axisNode = root["axes"][roleName];
    if (!axisNode.is_object()) {
        validator.Fail(path, "must be an object");
        return;
    }

    if (!axisNode.contains("source") || !axisNode["source"].is_string()) {
        validator.Fail(path + ".source", "must be a string axis token (e.g. \"X\", \"Rz\", \"Slider0\")");
        return;
    }
    const std::string sourceStr = axisNode["source"].get<std::string>();
    const auto source = dal::AxisSourceFromString(sourceStr);
    if (!source) {
        validator.Fail(path + ".source", "unknown axis token \"" + sourceStr + "\"");
        return;
    }
    if (std::find(usedSources.begin(), usedSources.end(), *source) != usedSources.end()) {
        validator.Fail(path + ".source", "source \"" + sourceStr + "\" is already used by another role in this profile");
        return;
    }

    AxisDirection direction = AxisDirection::Normal;
    if (axisNode.contains("direction")) {
        if (!axisNode["direction"].is_string()) {
            validator.Fail(path + ".direction", "must be a string (\"normal\" or \"inverted\"), not a boolean or number");
            return;
        }
        const std::string dirStr = axisNode["direction"].get<std::string>();
        if (dirStr == "normal") {
            direction = AxisDirection::Normal;
        } else if (dirStr == "inverted") {
            direction = AxisDirection::Inverted;
        } else {
            validator.Fail(path + ".direction", "must be \"normal\" or \"inverted\", got \"" + dirStr + "\"");
            return;
        }
    }

    std::optional<AxisCenterPolicy> center;
    if (axisNode.contains("center")) {
        if (!axisNode["center"].is_string()) {
            validator.Fail(path + ".center", "must be a string");
            return;
        }
        const std::string centerStr = axisNode["center"].get<std::string>();
        if (centerStr == "rangeMidpoint") {
            center = AxisCenterPolicy::RangeMidpoint;
        } else {
            validator.Fail(path + ".center", "unknown center policy \"" + centerStr + "\"");
            return;
        }
    }

    usedSources.push_back(*source);
    outBinding = AxisBinding{*source, direction, center};
}

void ParseReadiness(const nlohmann::json& root, dal::DeviceReadinessPolicy& outPolicy, Validator& validator) {
    if (!root.contains("readiness")) {
        outPolicy = dal::DeviceReadinessPolicy::ConservativeDefault();
        return;
    }
    const auto& node = root["readiness"];
    if (!node.is_object()) {
        validator.Fail("readiness", "must be an object");
        return;
    }

    const auto parseMillis = [&](const char* field, std::chrono::milliseconds& out) {
        const std::string path = std::string("readiness.") + field;
        if (!node.contains(field) || !node[field].is_number_integer()) {
            validator.Fail(path, "must be present and a non-negative integer number of milliseconds");
            return;
        }
        const auto value = node[field].get<long long>();
        if (value < 0 || value > kMaxReadinessMilliseconds) {
            validator.Fail(path, "must be between 0 and " + std::to_string(kMaxReadinessMilliseconds) + " milliseconds");
            return;
        }
        out = std::chrono::milliseconds{value};
    };

    parseMillis("minimumWarmupMilliseconds", outPolicy.minimumWarmup);
    parseMillis("stableSampleMilliseconds", outPolicy.stableSample);
    parseMillis("maximumWaitMilliseconds", outPolicy.maximumWait);

    if (node.contains("stabilityTolerance")) {
        if (!node["stabilityTolerance"].is_number()) {
            validator.Fail("readiness.stabilityTolerance", "must be a number");
        } else {
            const double tol = node["stabilityTolerance"].get<double>();
            if (tol < 0.0 || tol > 1.0) {
                validator.Fail("readiness.stabilityTolerance", "must be between 0.0 and 1.0");
            } else {
                outPolicy.stabilityTolerance = static_cast<float>(tol);
            }
        }
    }
}

} // namespace

ProfileParseResult ProfileLoader::ParseFromString(std::string_view jsonText) {
    ProfileParseResult result;
    Validator validator(result.errors);

    const nlohmann::json root = nlohmann::json::parse(jsonText, nullptr, false);
    if (root.is_discarded()) {
        validator.Fail("", "malformed JSON: the document could not be parsed");
        return result;
    }
    if (!root.is_object()) {
        validator.Fail("", "profile document must be a JSON object");
        return result;
    }

    try {
        DeviceProfile profile;

        if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_integer()) {
            validator.Fail("schemaVersion", "must be present and an integer");
        } else {
            profile.schemaVersion = root["schemaVersion"].get<int>();
            if (profile.schemaVersion != kSupportedSchemaVersion) {
                validator.Fail("schemaVersion", "unsupported schema version " + std::to_string(profile.schemaVersion) +
                                                     "; this build supports version " + std::to_string(kSupportedSchemaVersion));
            }
        }

        if (!root.contains("profileId") || !root["profileId"].is_string() || root["profileId"].get<std::string>().empty()) {
            validator.Fail("profileId", "must be a non-empty string");
        } else {
            profile.profileId = root["profileId"].get<std::string>();
        }

        if (root.contains("displayName")) {
            if (!root["displayName"].is_string()) {
                validator.Fail("displayName", "must be a string");
            } else {
                profile.displayName = root["displayName"].get<std::string>();
            }
        }

        if (!root.contains("match") || !root["match"].is_object()) {
            validator.Fail("match", "must be an object");
        } else {
            const auto& match = root["match"];
            if (!match.contains("backend") || !match["backend"].is_string()) {
                validator.Fail("match.backend", "must be a string (\"DirectInput\" or \"Logitech\")");
            } else {
                const std::string backendStr = match["backend"].get<std::string>();
                if (backendStr == "DirectInput") {
                    profile.match.backend = dal::DeviceBackend::DirectInput;
                } else if (backendStr == "Logitech") {
                    profile.match.backend = dal::DeviceBackend::Logitech;
                } else {
                    validator.Fail("match.backend", "unknown backend \"" + backendStr + "\"");
                }
            }

            const bool hasVendor = match.contains("vendorId");
            const bool hasProduct = match.contains("productId");
            if (hasVendor != hasProduct) {
                validator.Fail("match", "vendorId and productId must both be present (exact-match profile) or both be "
                                         "absent (generic profile for this backend)");
            } else if (hasVendor && hasProduct) {
                if (!match["vendorId"].is_string()) {
                    validator.Fail("match.vendorId", "must be a hex string like \"0x046D\"");
                } else if (const auto vid = ParseHexUint16(match["vendorId"].get<std::string>())) {
                    profile.match.vendorId = vid;
                } else {
                    validator.Fail("match.vendorId", "must be 1-4 hex digits, optionally prefixed with \"0x\"");
                }

                if (!match["productId"].is_string()) {
                    validator.Fail("match.productId", "must be a hex string like \"0xC266\"");
                } else if (const auto pid = ParseHexUint16(match["productId"].get<std::string>())) {
                    profile.match.productId = pid;
                } else {
                    validator.Fail("match.productId", "must be 1-4 hex digits, optionally prefixed with \"0x\"");
                }
            }
        }

        std::vector<AxisSource> usedSources;
        ParseAxis(root, "steering", profile.layout.steering, usedSources, validator);
        ParseAxis(root, "throttle", profile.layout.throttle, usedSources, validator);
        ParseAxis(root, "brake", profile.layout.brake, usedSources, validator);
        ParseAxis(root, "clutch", profile.layout.clutch, usedSources, validator);

        ParseReadiness(root, profile.readiness, validator);

        if (root.contains("sanityChecks")) {
            const auto& sanity = root["sanityChecks"];
            if (!sanity.is_object()) {
                validator.Fail("sanityChecks", "must be an object");
            } else {
                if (sanity.contains("expectedButtonCount")) {
                    if (!sanity["expectedButtonCount"].is_number_integer() || sanity["expectedButtonCount"].get<long long>() < 0) {
                        validator.Fail("sanityChecks.expectedButtonCount", "must be a non-negative integer");
                    } else {
                        profile.expectedButtonCount = static_cast<std::uint16_t>(sanity["expectedButtonCount"].get<long long>());
                    }
                }
                if (sanity.contains("expectedPovCount")) {
                    if (!sanity["expectedPovCount"].is_number_integer() || sanity["expectedPovCount"].get<long long>() < 0) {
                        validator.Fail("sanityChecks.expectedPovCount", "must be a non-negative integer");
                    } else {
                        profile.expectedPovCount = static_cast<std::uint8_t>(sanity["expectedPovCount"].get<long long>());
                    }
                }
            }
        }

        if (!validator.HasErrors()) {
            result.profile = std::move(profile);
        }
    } catch (const nlohmann::json::exception& ex) {
        validator.Fail("", std::string("unexpected JSON structure: ") + ex.what());
        result.profile.reset();
    }

    return result;
}

ProfileParseResult ProfileLoader::ParseFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ProfileParseResult result;
        result.errors.push_back({path.string(), "could not open file for reading"});
        return result;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return ParseFromString(contents.str());
}

std::string ProfileLoader::Serialize(const DeviceProfile& profile) {
    nlohmann::json root;
    root["schemaVersion"] = profile.schemaVersion;
    root["profileId"] = profile.profileId;
    root["displayName"] = profile.displayName;

    nlohmann::json match;
    match["backend"] = (profile.match.backend == dal::DeviceBackend::DirectInput) ? "DirectInput" : "Logitech";
    if (profile.match.vendorId && profile.match.productId) {
        match["vendorId"] = FormatHexUint16(*profile.match.vendorId);
        match["productId"] = FormatHexUint16(*profile.match.productId);
    }
    root["match"] = match;

    nlohmann::json axes = nlohmann::json::object();
    const auto serializeAxis = [&](const char* role, const std::optional<AxisBinding>& binding) {
        if (!binding) {
            return;
        }
        nlohmann::json node;
        node["source"] = std::string(dal::ToString(binding->source));
        node["direction"] = (binding->direction == AxisDirection::Inverted) ? "inverted" : "normal";
        if (binding->center) {
            node["center"] = "rangeMidpoint";
        }
        axes[role] = node;
    };
    serializeAxis("steering", profile.layout.steering);
    serializeAxis("throttle", profile.layout.throttle);
    serializeAxis("brake", profile.layout.brake);
    serializeAxis("clutch", profile.layout.clutch);
    root["axes"] = axes;

    nlohmann::json readiness;
    readiness["minimumWarmupMilliseconds"] = profile.readiness.minimumWarmup.count();
    readiness["stableSampleMilliseconds"] = profile.readiness.stableSample.count();
    readiness["maximumWaitMilliseconds"] = profile.readiness.maximumWait.count();
    readiness["stabilityTolerance"] = profile.readiness.stabilityTolerance;
    root["readiness"] = readiness;

    if (profile.expectedButtonCount || profile.expectedPovCount) {
        nlohmann::json sanity;
        if (profile.expectedButtonCount) {
            sanity["expectedButtonCount"] = *profile.expectedButtonCount;
        }
        if (profile.expectedPovCount) {
            sanity["expectedPovCount"] = static_cast<int>(*profile.expectedPovCount);
        }
        root["sanityChecks"] = sanity;
    }

    return root.dump(2);
}

} // namespace rvwheel::profiles
