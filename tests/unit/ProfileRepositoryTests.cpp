#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "rvwheel/profiles/ProfileRepository.hpp"

using rvwheel::dal::DeviceBackend;
using rvwheel::profiles::DeviceProfile;
using rvwheel::profiles::ProfileRepository;

namespace {

DeviceProfile MakeProfile(std::string id, DeviceBackend backend = DeviceBackend::DirectInput) {
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = std::move(id);
    profile.match.backend = backend;
    return profile;
}

} // namespace

TEST_CASE("ProfileRepository::MergeProfiles: a user profile overrides a built-in with the same profileId",
          "[ProfileRepository][Precedence]") {
    std::vector<DeviceProfile> builtIn{MakeProfile("shared-id")};
    builtIn[0].displayName = "built-in version";

    std::vector<DeviceProfile> user{MakeProfile("shared-id")};
    user[0].displayName = "user override version";

    const auto merged = ProfileRepository::MergeProfiles(builtIn, user);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].isUserProfile);
    REQUIRE(merged[0].profile.displayName == "user override version");
}

TEST_CASE("ProfileRepository::MergeProfiles: an unmatched user profile is appended, not dropped",
          "[ProfileRepository][Precedence]") {
    const std::vector<DeviceProfile> builtIn{MakeProfile("builtin-only")};
    const std::vector<DeviceProfile> user{MakeProfile("user-only")};

    const auto merged = ProfileRepository::MergeProfiles(builtIn, user);
    REQUIRE(merged.size() == 2);
    REQUIRE(merged[0].profile.profileId == "builtin-only");
    REQUIRE_FALSE(merged[0].isUserProfile);
    REQUIRE(merged[1].profile.profileId == "user-only");
    REQUIRE(merged[1].isUserProfile);
}

TEST_CASE("ProfileRepository::MergeProfiles: built-in profiles with no override stay built-in", "[ProfileRepository][Precedence]") {
    const std::vector<DeviceProfile> builtIn{MakeProfile("a"), MakeProfile("b")};
    const std::vector<DeviceProfile> user{};

    const auto merged = ProfileRepository::MergeProfiles(builtIn, user);
    REQUIRE(merged.size() == 2);
    for (const auto& entry : merged) {
        REQUIRE_FALSE(entry.isUserProfile);
    }
}

TEST_CASE("ProfileRepository::MergeProfiles: empty inputs produce an empty result", "[ProfileRepository][Precedence]") {
    const auto merged = ProfileRepository::MergeProfiles({}, {});
    REQUIRE(merged.empty());
}

TEST_CASE("ProfileRepository: loads *.json files from real directories (a temp dir, never %LOCALAPPDATA%)",
          "[ProfileRepository][Filesystem]") {
    // Deliberately NOT %LOCALAPPDATA%: a fresh, uniquely-named temp
    // directory this test owns and cleans up, per the "never touch the
    // real user profiles location in tests" requirement.
    const auto base = std::filesystem::temp_directory_path() / "rvwheel_profile_repo_test";
    const auto builtInDir = base / "builtin";
    const auto userDir = base / "user";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(builtInDir);
    std::filesystem::create_directories(userDir);

    {
        std::ofstream file(builtInDir / "device-a.json");
        file << R"({"schemaVersion":1,"profileId":"device-a","match":{"backend":"DirectInput"}})";
    }
    {
        std::ofstream file(userDir / "device-a-override.json");
        file << R"({"schemaVersion":1,"profileId":"device-a","displayName":"overridden","match":{"backend":"DirectInput"}})";
    }
    {
        std::ofstream file(builtInDir / "broken.json");
        file << "{ not valid json";
    }

    const ProfileRepository repo(builtInDir, userDir);

    REQUIRE(repo.BuiltInProfiles().size() == 1);
    REQUIRE(repo.UserProfiles().size() == 1);
    REQUIRE_FALSE(repo.Issues().empty()); // The broken.json file must be recorded, not silently dropped.

    const auto merged = repo.MergedProfiles();
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].isUserProfile);
    REQUIRE(merged[0].profile.displayName == "overridden");

    std::filesystem::remove_all(base);
}

TEST_CASE("ProfileRepository: a missing user directory is not an error", "[ProfileRepository][Filesystem]") {
    const auto base = std::filesystem::temp_directory_path() / "rvwheel_profile_repo_missing_test";
    std::filesystem::remove_all(base);

    const ProfileRepository repo(base / "builtin_missing", base / "user_missing");
    REQUIRE(repo.BuiltInProfiles().empty());
    REQUIRE(repo.UserProfiles().empty());
    REQUIRE(repo.Issues().empty());
}
