#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rvwheel/profiles/DeviceProfile.hpp"

namespace rvwheel::profiles {

struct ProfileLoadIssue {
    std::filesystem::path path; // Empty for issues not tied to one file (e.g. a duplicate profileId across files).
    std::string message;
};

// Loads built-in and user profile directories (each *.json file parsed
// independently via ProfileLoader) and exposes them for matching. Paths
// are constructor parameters -- this class never reads %LOCALAPPDATA% or
// an executable-relative path itself; see
// rvwheel::tools::probe::ResolveBuiltInProfilesDirectory and
// ResolveUserProfilesDirectory in the probe for how production code
// decides what to pass in. Tests construct this directly with a temp
// directory (or an empty/nonexistent one, which is not an error).
//
// A malformed file, or a duplicate profileId within the same directory,
// is recorded in Issues() and skipped -- it never aborts loading the rest
// of the directory, and it never silently disappears without a trace.
class ProfileRepository {
public:
    ProfileRepository(std::filesystem::path builtInDir, std::filesystem::path userDir);

    [[nodiscard]] const std::vector<DeviceProfile>& BuiltInProfiles() const noexcept { return builtIn_; }
    [[nodiscard]] const std::vector<DeviceProfile>& UserProfiles() const noexcept { return user_; }
    [[nodiscard]] const std::vector<ProfileLoadIssue>& Issues() const noexcept { return issues_; }

    [[nodiscard]] const std::filesystem::path& BuiltInDirectory() const noexcept { return builtInDir_; }
    [[nodiscard]] const std::filesystem::path& UserDirectory() const noexcept { return userDir_; }

    [[nodiscard]] std::vector<ProfileWithOrigin> MergedProfiles() const { return MergeProfiles(builtIn_, user_); }

    // Pure merge logic, exposed statically so precedence itself is
    // testable with synthetic in-memory profiles and zero filesystem
    // access. A user profile whose profileId matches a built-in's
    // replaces it in place (same position in the result); an unmatched
    // user profile is appended.
    [[nodiscard]] static std::vector<ProfileWithOrigin> MergeProfiles(const std::vector<DeviceProfile>& builtIn,
                                                                       const std::vector<DeviceProfile>& user);

private:
    void LoadDirectory(const std::filesystem::path& dir, std::vector<DeviceProfile>& out);

    std::filesystem::path builtInDir_;
    std::filesystem::path userDir_;
    std::vector<DeviceProfile> builtIn_;
    std::vector<DeviceProfile> user_;
    std::vector<ProfileLoadIssue> issues_;
};

} // namespace rvwheel::profiles
