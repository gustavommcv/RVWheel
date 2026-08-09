# RVWheel

[![CI](https://github.com/gustavommcv/RVWheel/actions/workflows/ci.yml/badge.svg)](https://github.com/gustavommcv/RVWheel/actions/workflows/ci.yml)

RVWheel is an open-source project to add racing-wheel support to *RV There Yet?* through UE4SS, without modifying the game executable.

> [!IMPORTANT]
> RVWheel is currently a development preview. The one-click launcher can install
> and enable the RVWheel script, start its bridge, and open the game, but UE4SS
> itself must still be installed in the game directory first.

## Current status

Working and validated:

- Windows x64 and C++20 build with MSVC;
- DirectInput device enumeration and hot-plug foundation;
- normalized steering, pedal, button, and POV snapshots;
- versioned JSON device profiles with exact VID/PID matching;
- guided calibration with continuous 60 Hz acquisition and stable-window sampling for unknown devices;
- standalone `rvwheel_device_probe` for listing, monitoring, capturing, calibrating, and hosting the live bridge;
- native `rvwheel_launcher` for one-click mod sync, bridge supervision, and Steam game startup;
- 186 unit tests passing in Release at the latest local validation;
- Logitech G923 (`046D:C266`) detected on real hardware with 25 buttons, one POV, three pedal axes, steering, and reported FFB capability.
- playable UE4SS integration validated with steering, throttle, brake, clutch,
  Logitech H-pattern gears 1–5, neutral, and reverse.
- force feedback infrastructure (safety controller, mixer, profile-configured
  spring/damper source, `--ffb-simulate` diagnostic) implemented and unit
  tested, exercised end-to-end against real G923 capability detection in
  simulation mode only — **no force has ever been applied to real
  hardware**; see [docs/FORCE_FEEDBACK.md](docs/FORCE_FEEDBACK.md).

Still required before this is a polished installable mod:

- collect verified profiles for additional Logitech, Moza, Thrustmaster, Fanatec, and generic DirectInput devices;
- move game/button mappings into user-facing profile controls;
- package UE4SS and the launcher into a polished end-user installer;
- validate multiplayer behavior;
- wire vehicle telemetry from Lua into the force feedback engine, then run the
  [gated hardware test procedure](docs/FORCE_FEEDBACK_HARDWARE_TEST.md) before
  ever enabling force feedback for real.

See [the G923 hardware baseline](docs/hardware/G923_DIRECTINPUT_CAPTURE.md) for measured behavior and unresolved findings.

## Documentation

- [docs/INSTALL.md](docs/INSTALL.md) — install and play, if you are not building from source.
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — Windows dev environment setup, build, and test.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the layers fit together, the RVW2 protocol, and design decisions.
- [docs/ADDING_A_WHEEL.md](docs/ADDING_A_WHEEL.md) — capture, calibrate, and ship a profile for a new device.
- [docs/FORCE_FEEDBACK.md](docs/FORCE_FEEDBACK.md) — force feedback architecture, safety model, and current status.
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — common problems and what they mean.
- [docs/RELEASING.md](docs/RELEASING.md) — building the distributable package.

## Architecture

```text
UE4SS game integration and fail-safe bridge
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
tools/launcher/            Native one-click Windows launcher
mods/RVWheel/              Playable UE4SS Lua bridge
mods/RVWheelDiscovery/     Runtime object/reflection diagnostics
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
- `rvwheel_device_probe.exe` — standalone developer diagnostic tool and bridge host;
- `rvwheel_launcher.exe` — native one-click game launcher.

To build the player-facing distribution ZIP (launcher, probe, mod, default
profiles, README, license — no dev artifacts), run `cpack -C Release -G ZIP`
from `build/` after a Release build. See [docs/RELEASING.md](docs/RELEASING.md).

## One-click launcher

Build `rvwheel_launcher` in Release, then double-click:

```text
build\tools\launcher\Release\rvwheel_launcher.exe
```

The Release output is self-contained for the RVWheel components: it includes the
bridge host, the UE4SS `RVWheel` mod, and the default device profiles. The launcher:

1. locates the Steam library containing *RV There Yet?*;
2. verifies that UE4SS is installed;
3. copies and enables the current RVWheel mod;
4. starts the bridge without a console window;
5. opens the game through Steam and keeps the bridge alive until the game exits.

It safely reuses a game or bridge that is already running. Bridge diagnostics are
written to `%LOCALAPPDATA%\RVWheel\logs\bridge.log`. Launcher failures are shown in
a Windows message box instead of silently failing.

## Device probe

After a Release build:

```powershell
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --help
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --profiles
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --list
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --calibrate
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --monitor --duration 30 --rate 60
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --capture wheel-capture.jsonl --duration 30 --rate 60
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --bridge --rate 60
.\build\tools\device_probe\Release\rvwheel_device_probe.exe --ffb-simulate --duration 10
```

The probe never applies force feedback: `--ffb-simulate` computes and prints
force feedback commands using an in-process recording sink, never the real
device (see [docs/FORCE_FEEDBACK.md](docs/FORCE_FEEDBACK.md)). Hardware captures (`*.jsonl`) are local diagnostic artifacts and are ignored by Git; derived, reviewed findings belong under `docs/hardware/`.

For the manually validated UE4SS installation and current limitations, see
[the first in-game test](docs/game-integration/UE4SS_FIRST_TEST.md) and the
[bridge README](mods/RVWheel/README.md).

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
