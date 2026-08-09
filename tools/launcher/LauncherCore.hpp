#pragma once

#include <cstdint>
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

// --enable-force-feedback / --profiles-dir <path>: the launcher's own two
// opt-ins, forwarded to the bridge it spawns. Both default to "off"/absent
// so a launcher invoked with no arguments (the ordinary player path)
// behaves identically to before this feature existed -- see
// ParseLauncherArgs and BuildBridgeCommandLine.
struct LauncherOptions {
    bool enableForceFeedback = false;
    std::filesystem::path profilesDir; // Empty means not given.
};

struct LauncherCliParseResult {
    bool success = false;
    LauncherOptions options;
    std::wstring errorMessage; // Empty when success is true.
};

// Pure command-line parsing: no Win32, no filesystem access, no process
// I/O. `args` excludes argv[0] (the caller tokenizes the raw command line,
// e.g. via CommandLineToArgvW, before calling this). Rejects anything
// other than --enable-force-feedback and --profiles-dir <path> so a typo
// fails loudly instead of being silently ignored -- see RunLauncher(),
// which fails before touching Steam, UE4SS, or the bridge if this fails.
[[nodiscard]] LauncherCliParseResult ParseLauncherArgs(const std::vector<std::wstring>& args);

// Pure construction of the bridge child process's command line: correctly
// quoted/escaped for CreateProcessW, and round-trippable by the bridge's
// own wmain(argc, argv) parsing (which uses the same Windows command-line
// convention). Always includes --bridge/--rate/--parent-pid;
// --enable-force-feedback and --profiles-dir are appended only when
// `options` actually sets them, so LauncherOptions{} (the default)
// produces the exact command line the launcher always used before this
// feature existed.
[[nodiscard]] std::wstring BuildBridgeCommandLine(const std::filesystem::path& bridgeExecutable,
                                                   const LauncherOptions& options, int rateHz,
                                                   std::uint32_t parentProcessId);

} // namespace rvwheel::tools::launcher
