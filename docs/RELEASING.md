# Releasing (building the distribution package)

This produces the ZIP a player downloads and follows
[INSTALL.md](INSTALL.md) with: `rvwheel_launcher.exe`,
`rvwheel_device_probe.exe`, the `RVWheel` mod, the default device profiles,
and the top-level README/license. It intentionally excludes `.lib`/`.pdb`
files, source code, caches, and Visual Studio project files.

## How the package is assembled

Packaging is CMake's built-in [CPack](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Packaging%20With%20CPack.html),
configured at the bottom of the root `CMakeLists.txt`. Its contents are
controlled entirely by the `install()` rules in
`tools/launcher/CMakeLists.txt`:

```cmake
install(TARGETS rvwheel_launcher rvwheel_device_probe RUNTIME DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/mods/RVWheel DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/configs/default_profiles DESTINATION configs)
install(FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION .)
```

`install(TARGETS ... RUNTIME)` only ever stages the executables themselves
(never `.lib`/`.pdb`/import libraries), and nothing installs `src/` or
`tests/` at all — so there is no exclude-list to maintain; anything not
explicitly `install()`-ed simply cannot end up in the package. CPack stages
this install into its own temporary directory (`build/_CPack_Packages/...`,
recreated on every run and never touching anything outside `build/`) before
zipping it, so packaging is reproducible from a clean `build/` without any
hand-written staging/cleanup script.

## Producing a release ZIP

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF `
  -DRVWHEEL_BUILD_TOOLS=ON

cmake --build build --config Release

cd build
cpack -C Release -G ZIP
```

This produces `build/RVWheel-<version>-win64.zip` (version taken from the
root `CMakeLists.txt`'s `project(RVWheel VERSION ...)`). `cpack` is bundled
with the CMake install used to configure the project — no extra tool to
install, and no dependency on the proprietary Logitech SDK, since
`RVWHEEL_ENABLE_LOGITECH_SDK` stays `OFF` for a release build exactly as it
does for development and CI.

`build/` (including the generated `.zip` and CPack's staging directories)
is gitignored, so re-running this never touches version control.

## Verifying a package before publishing

1. **Inspect contents** — confirm the ZIP contains exactly: `LICENSE`,
   `README.md`, `RVWheel/` (the mod, with its own `README.md` and
   `Scripts/main.lua`), `configs/default_profiles/` (JSON profiles plus
   their contribution-guide `README.md`), `rvwheel_device_probe.exe`,
   `rvwheel_launcher.exe`. No `.lib`, `.pdb`, `.exp`, `.ilk`, source `.cpp`/
   `.hpp`, or build system files.
2. **Extract to a clean folder** (not inside the repository or an existing
   game install) and run `rvwheel_launcher.exe` following
   [INSTALL.md](INSTALL.md) end to end against a real Steam install with
   UE4SS already present.
3. **Confirm no duplicate processes**: only one `rvwheel_launcher.exe`, one
   `rvwheel_device_probe.exe`, and one game process should ever be running
   at a time — the launcher's singleton mutex and process-reuse checks are
   what guarantee this; verify it in Task Manager during the test run.
4. Only after a successful end-to-end run should the ZIP be attached to a
   GitHub release.

## What is not included, on purpose

- **UE4SS itself.** It is a separate, independently licensed project; see
  [INSTALL.md](INSTALL.md#step-1--install-ue4ss) for why RVWheel does not
  bundle or redistribute it.
- **`mods/RVWheelDiscovery`.** It is a development/reflection diagnostic
  tool (F8/F9 hotkeys, no game-state changes), not something a player needs.
- **Debug configuration artifacts.** Only a Release build should ever be
  packaged; Debug binaries are slower and carry heavier PDBs with no
  benefit to a player.
