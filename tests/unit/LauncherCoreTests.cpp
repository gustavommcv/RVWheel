#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "LauncherCore.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

using rvwheel::tools::launcher::BridgeExecutableCandidates;
using rvwheel::tools::launcher::BuildBridgeCommandLine;
using rvwheel::tools::launcher::EnableUe4ssMod;
using rvwheel::tools::launcher::LauncherOptions;
using rvwheel::tools::launcher::ParseLauncherArgs;
using rvwheel::tools::launcher::ParseSteamLibraryRoots;

TEST_CASE("Launcher parses and deduplicates Steam library roots", "[Launcher][Steam]") {
    const std::wstring vdf = LR"VDF(
"libraryfolders"
{
    "0" { "path" "C:\\Program Files (x86)\\Steam" }
    "1" { "path" "D:\\SteamLibrary" }
    "2" { "path" "D:\\SteamLibrary" }
}
)VDF";

    const auto roots = ParseSteamLibraryRoots(vdf, LR"(C:\Program Files (x86)\Steam)");
    REQUIRE(roots.size() == 2);
    REQUIRE(roots[0] == std::filesystem::path(LR"(C:\Program Files (x86)\Steam)"));
    REQUIRE(roots[1] == std::filesystem::path(LR"(D:\SteamLibrary)"));
}

TEST_CASE("Launcher enables RVWheel before UE4SS built-in marker", "[Launcher][UE4SS]") {
    const std::string input = "ConsoleEnablerMod : 1\r\n; Built-in keybinds, do not move up!\r\nKeybinds : 1\r\n";
    REQUIRE(EnableUe4ssMod(input, "RVWheel") ==
            "ConsoleEnablerMod : 1\r\nRVWheel : 1\r\n; Built-in keybinds, do not move up!\r\nKeybinds : 1\r\n");
}

TEST_CASE("Launcher replaces disabled and duplicate RVWheel entries idempotently", "[Launcher][UE4SS]") {
    const std::string input = "RVWheel : 0\nOther : 1\nRVWheel:1\n";
    const std::string enabled = EnableUe4ssMod(input, "RVWheel");
    REQUIRE(enabled == "RVWheel : 1\nOther : 1\n");
    REQUIRE(EnableUe4ssMod(enabled, "RVWheel") == enabled);
}

TEST_CASE("Launcher resolves packaged and multi-config bridge layouts", "[Launcher][Paths]") {
    const auto candidates = BridgeExecutableCandidates(
        LR"(C:\repo\build\tools\launcher\Release\rvwheel_launcher.exe)");
    REQUIRE(candidates.size() == 2);
    REQUIRE(candidates[0] == std::filesystem::path(
                                 LR"(C:\repo\build\tools\launcher\Release\rvwheel_device_probe.exe)"));
    REQUIRE(candidates[1] == std::filesystem::path(
                                 LR"(C:\repo\build\tools\device_probe\Release\rvwheel_device_probe.exe)"));
}

namespace {
// Round-trips a full CreateProcessW-style command line back into argv via
// the real Win32 parser, so a test asserts against exactly what the
// spawned bridge's own wmain(argc, argv) would receive -- not a
// hand-rolled reimplementation of the same quoting rules being checked
// against itself.
std::vector<std::wstring> SplitCommandLine(const std::wstring& commandLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(commandLine.c_str(), &argc);
    REQUIRE(argv != nullptr);
    std::vector<std::wstring> result(argv, argv + argc);
    LocalFree(argv);
    return result;
}
} // namespace

TEST_CASE("BuildBridgeCommandLine: default (no opt-ins) never includes --enable-force-feedback or "
          "--profiles-dir -- the ordinary player invocation",
          "[Launcher][FFB]") {
    const LauncherOptions options; // Default: everything off/absent.
    const auto argv = SplitCommandLine(BuildBridgeCommandLine(LR"(C:\repo\rvwheel_device_probe.exe)", options, 60, 4242));

    REQUIRE(std::find(argv.begin(), argv.end(), L"--enable-force-feedback") == argv.end());
    REQUIRE(std::find(argv.begin(), argv.end(), L"--profiles-dir") == argv.end());
    REQUIRE(std::find(argv.begin(), argv.end(), L"--bridge") != argv.end());
    REQUIRE(std::find(argv.begin(), argv.end(), L"--rate") != argv.end());
    REQUIRE(std::find(argv.begin(), argv.end(), L"--parent-pid") != argv.end());
}

TEST_CASE("BuildBridgeCommandLine: the opt-in forwards --enable-force-feedback to the bridge", "[Launcher][FFB]") {
    LauncherOptions options;
    options.enableForceFeedback = true;
    const auto argv = SplitCommandLine(BuildBridgeCommandLine(LR"(C:\repo\rvwheel_device_probe.exe)", options, 60, 4242));

    REQUIRE(std::find(argv.begin(), argv.end(), L"--enable-force-feedback") != argv.end());
}

TEST_CASE("BuildBridgeCommandLine: --profiles-dir with a space-and-Unicode path round-trips exactly",
          "[Launcher][FFB][Unicode]") {
    LauncherOptions options;
    options.profilesDir = LR"(C:\Usuários\gu gam\Perfis Ünïcödé\profiles)";
    const auto argv = SplitCommandLine(BuildBridgeCommandLine(LR"(C:\repo\rvwheel_device_probe.exe)", options, 60, 4242));

    const auto flagIt = std::find(argv.begin(), argv.end(), L"--profiles-dir");
    REQUIRE(flagIt != argv.end());
    REQUIRE(std::next(flagIt) != argv.end());
    REQUIRE(*std::next(flagIt) == options.profilesDir.wstring());
}

TEST_CASE("BuildBridgeCommandLine: a path ending in a bare backslash (e.g. a drive root) escapes "
          "correctly instead of collapsing into the closing quote",
          "[Launcher][FFB]") {
    LauncherOptions options;
    options.profilesDir = LR"(D:\)"; // The classic Windows command-line quoting pitfall.
    const auto argv = SplitCommandLine(BuildBridgeCommandLine(LR"(C:\repo\rvwheel_device_probe.exe)", options, 60, 4242));

    const auto flagIt = std::find(argv.begin(), argv.end(), L"--profiles-dir");
    REQUIRE(flagIt != argv.end());
    REQUIRE(std::next(flagIt) != argv.end());
    REQUIRE(*std::next(flagIt) == L"D:\\");
}

TEST_CASE("BuildBridgeCommandLine: the bridge executable path itself round-trips even with spaces",
          "[Launcher][FFB]") {
    const LauncherOptions options;
    const std::filesystem::path bridgePath = LR"(C:\Program Files\RVWheel\rvwheel_device_probe.exe)";
    const auto argv = SplitCommandLine(BuildBridgeCommandLine(bridgePath, options, 60, 4242));

    REQUIRE_FALSE(argv.empty());
    REQUIRE(argv.front() == bridgePath.wstring());
}

TEST_CASE("ParseLauncherArgs: no arguments (the ordinary player invocation) leaves everything off",
          "[Launcher][FFB]") {
    const auto result = ParseLauncherArgs({});
    REQUIRE(result.success);
    REQUIRE_FALSE(result.options.enableForceFeedback);
    REQUIRE(result.options.profilesDir.empty());
}

TEST_CASE("ParseLauncherArgs: --enable-force-feedback parses and sets the flag", "[Launcher][FFB]") {
    const auto result = ParseLauncherArgs({L"--enable-force-feedback"});
    REQUIRE(result.success);
    REQUIRE(result.options.enableForceFeedback);
}

TEST_CASE("ParseLauncherArgs: --profiles-dir parses and preserves a Unicode/space path exactly",
          "[Launcher][FFB][Unicode]") {
    const std::wstring path = LR"(C:\Usuários\gu gam\Perfis Ünïcödé\profiles)";
    const auto result = ParseLauncherArgs({L"--profiles-dir", path});
    REQUIRE(result.success);
    REQUIRE(result.options.profilesDir == std::filesystem::path(path));
}

TEST_CASE("ParseLauncherArgs: --profiles-dir without a value fails, before touching the game or hardware",
          "[Launcher][FFB][Invalid]") {
    const auto result = ParseLauncherArgs({L"--profiles-dir"});
    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errorMessage.empty());
}

TEST_CASE("ParseLauncherArgs: an unrecognized argument fails, before touching the game or hardware",
          "[Launcher][FFB][Invalid]") {
    const auto result = ParseLauncherArgs({L"--bogus"});
    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errorMessage.empty());
}
