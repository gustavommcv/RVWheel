#include "rvwheel/profiles/ProfileRepository.hpp"

#include <algorithm>
#include <system_error>

#include "rvwheel/profiles/ProfileLoader.hpp"

namespace rvwheel::profiles {

ProfileRepository::ProfileRepository(std::filesystem::path builtInDir, std::filesystem::path userDir)
    : builtInDir_(std::move(builtInDir)), userDir_(std::move(userDir)) {
    LoadDirectory(builtInDir_, builtIn_);
    LoadDirectory(userDir_, user_);
}

void ProfileRepository::LoadDirectory(const std::filesystem::path& dir, std::vector<DeviceProfile>& out) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        return; // A missing profiles directory (especially the user one) is not an error.
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            issues_.push_back({dir, "failed to enumerate directory: " + ec.message()});
            return;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        const ProfileParseResult result = ProfileLoader::ParseFromFile(entry.path());
        if (!result.IsOk()) {
            std::string message = "failed to load profile: ";
            for (const auto& error : result.errors) {
                message += (error.path.empty() ? "" : (error.path + ": ")) + error.message + "; ";
            }
            issues_.push_back({entry.path(), message});
            continue;
        }

        const auto duplicateIt = std::find_if(out.begin(), out.end(), [&](const DeviceProfile& existing) {
            return existing.profileId == result.profile->profileId;
        });
        if (duplicateIt != out.end()) {
            issues_.push_back({entry.path(), "duplicate profileId \"" + result.profile->profileId +
                                                  "\" within the same directory; keeping the first one loaded"});
            continue;
        }

        out.push_back(*result.profile);
    }
}

std::vector<ProfileWithOrigin> ProfileRepository::MergeProfiles(const std::vector<DeviceProfile>& builtIn,
                                                                  const std::vector<DeviceProfile>& user) {
    std::vector<ProfileWithOrigin> result;
    result.reserve(builtIn.size() + user.size());
    for (const auto& profile : builtIn) {
        result.push_back(ProfileWithOrigin{profile, false});
    }
    for (const auto& userProfile : user) {
        const auto it = std::find_if(result.begin(), result.end(), [&](const ProfileWithOrigin& existing) {
            return existing.profile.profileId == userProfile.profileId;
        });
        if (it != result.end()) {
            *it = ProfileWithOrigin{userProfile, true};
        } else {
            result.push_back(ProfileWithOrigin{userProfile, true});
        }
    }
    return result;
}

} // namespace rvwheel::profiles
