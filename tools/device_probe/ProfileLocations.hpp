#pragma once

#include <filesystem>

namespace rvwheel::tools::probe {

// Walks up from this executable's own directory (never the current
// working directory) looking for "configs/default_profiles", bounded to a
// handful of levels -- enough for both a build tree
// (build-*/tools/device_probe/[Debug|Release]/exe) and a flat install
// layout, without an unbounded filesystem walk. Returns an empty path if
// not found; callers treat that as "no built-in profiles available", not
// a fatal error.
[[nodiscard]] std::filesystem::path ResolveBuiltInProfilesDirectory();

// %LOCALAPPDATA%\RVWheel\profiles. Returns an empty path if LOCALAPPDATA
// is not set (e.g. certain sandboxed/service contexts); callers must not
// assume this directory exists -- it is created on first save, not read
// eagerly.
[[nodiscard]] std::filesystem::path ResolveUserProfilesDirectory();

// %LOCALAPPDATA%\RVWheel\runtime\bridge-state.txt. The bridge host creates
// the parent directory on startup. Empty if LOCALAPPDATA is unavailable.
[[nodiscard]] std::filesystem::path ResolveBridgeStatePath();

} // namespace rvwheel::tools::probe
