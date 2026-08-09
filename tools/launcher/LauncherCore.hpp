#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rvwheel::tools::launcher {

// Single source of truth for the bridge executable's file name, shared by
// BridgeExecutableCandidates below and by LauncherApp's process lookup so
// the two never drift apart.
inline constexpr wchar_t kBridgeExecutableFileName[] = L"rvwheel_device_probe.exe";

// Parses every "path" entry in Steam's libraryfolders.vdf. The default
// Steam root is always returned first, even when the VDF is missing it.
[[nodiscard]] std::vector<std::filesystem::path> ParseSteamLibraryRoots(
    std::wstring_view vdfText,
    const std::filesystem::path& defaultSteamRoot);

// Enables a UE4SS Lua mod without duplicating its entry. Existing comments
// and unrelated load-order lines are preserved.
[[nodiscard]] std::string EnableUe4ssMod(std::string_view existingText, std::string_view modName);

// Packaged builds put the bridge beside the launcher. Multi-config CMake
// builds keep each tool in a sibling <tool>/<config> directory; include both.
[[nodiscard]] std::vector<std::filesystem::path> BridgeExecutableCandidates(
    const std::filesystem::path& launcherExecutable);

} // namespace rvwheel::tools::launcher
