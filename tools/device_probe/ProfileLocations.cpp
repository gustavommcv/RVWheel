#include "ProfileLocations.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rvwheel::tools::probe {

std::filesystem::path ResolveBuiltInProfilesDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }

    std::filesystem::path dir = std::filesystem::path(buffer).parent_path();
    for (int i = 0; i < 8; ++i) {
        const std::filesystem::path candidate = dir / "configs" / "default_profiles";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent.empty() || parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

std::filesystem::path ResolveUserProfilesDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer) / L"RVWheel" / L"profiles";
}

} // namespace rvwheel::tools::probe
