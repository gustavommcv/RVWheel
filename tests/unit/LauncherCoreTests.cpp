#include <catch2/catch_test_macros.hpp>

#include "LauncherCore.hpp"

using rvwheel::tools::launcher::BridgeExecutableCandidates;
using rvwheel::tools::launcher::EnableUe4ssMod;
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
