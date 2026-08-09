# Development setup (Windows)

This is the copy-pasteable version of the [README](../README.md#build-and-test)
build instructions, with the one-time environment setup spelled out. It
targets a machine that has nothing installed yet.

## 1. Install prerequisites

All commands below are PowerShell, run as a normal user (no admin rights
needed except for the Build Tools installer itself).

**Visual Studio 2022 Build Tools** (compiler + Windows SDK, no full IDE):

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override `
  "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

**Git**, **CMake** (3.21+):

```powershell
winget install --id Git.Git
winget install --id Kitware.CMake
```

Restart your terminal after these so `PATH` picks up the new tools.

**vcpkg** (manifest mode; RVWheel's `vcpkg.json`/`vcpkg-configuration.json`
pin exact dependency versions, so any recent vcpkg checkout works):

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```

Open a new terminal so `$env:VCPKG_ROOT` is set.

## 2. Clone and configure

```powershell
git clone https://github.com/gustavommcv/RVWheel.git
cd RVWheel

$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake -S . -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF `
  -DRVWHEEL_BUILD_TESTS=ON `
  -DRVWHEEL_BUILD_TOOLS=ON
```

vcpkg installs `nlohmann-json` and (for the `tests` feature) `catch2`
automatically during this step — no separate `vcpkg install` command needed.

Leave `RVWHEEL_ENABLE_LOGITECH_SDK` at its default `OFF` unless you have a
real Logitech Gaming SDK installation and are specifically working on that
backend; it is not required for DirectInput development or for CI.

## 3. Build and test

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

A clean checkout currently builds with zero warnings under `/W4 /permissive-`
(MSVC) and passes every test in both configurations — no hardware, Steam
install, or UE4SS installation is required for the test suite.

## 4. Running the tools locally

```powershell
# Hardware-independent checks (safe with or without a wheel attached):
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --help
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --profiles
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --list

# With a wheel attached:
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --calibrate
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --monitor --duration 30 --rate 60
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --bridge --rate 60

# Full flow against a real Steam install of RV There Yet? with UE4SS already
# installed (see docs/INSTALL.md Step 1):
.\build\tools\launcher\Release\rvwheel_launcher.exe
```

`--bridge --enable-force-feedback` applies real force to the wheel and is
**not** a routine development command -- it requires a profile with a
physically-validated `forceFeedback.enabled: true` block, exclusive
DirectInput access, and the gated, explicitly-authorized procedure in
[docs/FORCE_FEEDBACK_HARDWARE_TEST.md](FORCE_FEEDBACK_HARDWARE_TEST.md). Do
not add it to a routine dev loop or a script; `--ffb-simulate` (always
safe, never touches the real device) is the right tool for iterating on
force feedback logic during development.

The Release build output under `build\tools\launcher\Release\` is already
laid out like a distribution package (launcher, bridge, mod, default
profiles side by side) — see [RELEASING.md](RELEASING.md) for turning that
into a shippable ZIP.

## 5. Where things live

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full picture. As a map while
reading code:

```text
src/Core/                  DAL contracts, normalization, readiness, manager
src/Devices/DirectInput/   DirectInput 8 backend and raw-axis discovery
src/Devices/Logitech/      Logitech abstraction; proprietary adapter is incomplete
src/Profiles/              JSON profile loading, repository, and resolution
tools/device_probe/        Standalone hardware probe, calibration, bridge host
tools/launcher/            Native one-click Windows launcher
mods/RVWheel/              Playable UE4SS Lua bridge
mods/RVWheelDiscovery/     Runtime object/reflection diagnostics (F8/F9), no game-state changes
configs/default_profiles/  Verified built-in device profiles
tests/unit/                Hardware-independent Catch2 tests
docs/                      Architecture, hardware evidence, and process docs
```

## 6. Before sending a change

- Keep layer boundaries intact: the DAL/profile libraries must stay free of
  UE4SS/Lua/Win32-launcher concerns; see [ARCHITECTURE.md](ARCHITECTURE.md).
- Add or update tests for any behavior change — most of the codebase is
  designed to be testable without hardware (pure functions/state machines
  over supplied samples), so "I can't test this without a wheel" is rarely
  actually true.
- Never claim hardware or in-game validation you did not personally perform;
  state clearly which parts of a change are unit-tested only versus
  hardware-tested.
- Run the full Debug + Release build and test cycle above before submitting.
