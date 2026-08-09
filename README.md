# RVWheel

RVWheel is an open-source project to add racing-wheel support to *RV There Yet?* through UE4SS, without modifying the game executable.

> [!IMPORTANT]
> RVWheel is currently a development preview. The hardware abstraction, DirectInput backend, profile system, calibration foundation, and standalone device probe exist. The UE4SS/game-integration layer does not exist yet, so there is no mod package to install in the game today.

## Current status

Working and validated:

- Windows x64 and C++20 build with MSVC;
- DirectInput device enumeration and hot-plug foundation;
- normalized steering, pedal, button, and POV snapshots;
- versioned JSON device profiles with exact VID/PID matching;
- guided calibration with continuous 60 Hz acquisition and stable-window sampling for unknown devices;
- standalone `rvwheel_device_probe` for listing, monitoring, and capturing hardware input;
- 128 unit tests passing in Debug and Release at the latest local validation;
- Logitech G923 (`046D:C266`) detected on real hardware with 25 buttons, one POV, three pedal axes, steering, and reported FFB capability.

Still required before this is an installable mod:

- collect verified profiles for additional Logitech, Moza, Thrustmaster, Fanatec, and generic DirectInput devices;
- implement the input mapper and user-facing profile controls;
- implement UE4SS/game hooks for vehicle input;
- package and test an installable mod layout;
- validate force feedback safely on real hardware.

See [the G923 hardware baseline](docs/hardware/G923_DIRECTINPUT_CAPTURE.md) for measured behavior and unresolved findings.

## Architecture

```text
Game integration (future UE4SS layer)
                 │
Input mapping and device profiles
                 │
Device Abstraction Layer (IWheelDevice)
                 │
DirectInput / optional vendor backends
```

The DAL does not depend on UE4SS, Unreal Engine, Lua, or JSON. The profile library parses JSON into backend-agnostic layout types consumed by the DAL.

```text
src/Core/                  DAL contracts, normalization, readiness, manager
src/Devices/DirectInput/   DirectInput 8 backend and raw-axis discovery
src/Devices/Logitech/      Logitech abstraction; proprietary adapter is incomplete
src/Profiles/              JSON profile loading, repository, and resolution
tools/device_probe/        Standalone hardware probe and calibration workflow
configs/default_profiles/  Verified built-in device profiles
tests/unit/                Hardware-independent Catch2 tests
docs/                      Architecture prompts and hardware evidence
```

## Requirements

- Windows 10/11 x64;
- Visual Studio 2022 Build Tools with the Desktop C++ workload and Windows SDK;
- CMake 3.21 or newer;
- Git;
- vcpkg with `VCPKG_ROOT` configured.

Dependencies are declared in `vcpkg.json`. Manifest mode installs `nlohmann-json` and the optional test dependency Catch2 through the vcpkg toolchain.

## Build and test

From PowerShell:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake -S . -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF `
  -DRVWHEEL_BUILD_TESTS=ON `
  -DRVWHEEL_BUILD_TOOLS=ON

cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The main build products are:

- `rvwheel_dal.lib` — static device-abstraction library;
- `rvwheel_profiles.lib` — static profile-system library;
- `rvwheel_device_probe.exe` — standalone developer diagnostic tool.

## Device probe

After a Release build:

```powershell
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --help
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --profiles
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --list
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --calibrate
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --monitor --duration 30 --rate 60
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --capture wheel-capture.jsonl --duration 30 --rate 60
```

The probe never applies force feedback. Hardware captures (`*.jsonl`) are local diagnostic artifacts and are ignored by Git; derived, reviewed findings belong under `docs/hardware/`.

## Device profiles

Built-in profiles live in `configs/default_profiles/`. User-generated profiles are intended to live under `%LOCALAPPDATA%\RVWheel\profiles\` and override a built-in profile with the same `profileId`.

Do not add a device profile from guessed mappings or internet VID/PID lists. New profiles should be backed by a real probe capture and documented hardware behavior. See [the profile contribution guide](configs/default_profiles/README.md).

## Logitech SDK status

`RVWHEEL_ENABLE_LOGITECH_SDK` defaults to `OFF`. DirectInput is the functional path currently used for real-hardware validation.

The Logitech Gaming SDK is proprietary and is not downloaded or redistributed by this repository. The vendor adapter remains an explicit skeleton until it can be implemented and tested against genuine SDK headers. Enabling the option today should not be interpreted as validated Logitech-SDK support.

## Contributing

Keep changes focused, preserve the layer boundaries, add tests for behavior, and report hardware validation separately from unit-test results. Never claim that a wheel, force-feedback effect, or game integration was tested unless it was exercised on the relevant hardware/runtime.

## License

RVWheel is licensed under the [MIT License](LICENSE).
