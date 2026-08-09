# RVWheel Architecture

This document describes how RVWheel turns racing-wheel hardware into vehicle
input inside *RV There Yet?*, and why the current design looks the way it
does. It complements the top-level [README](../README.md); start there for
status and quick commands.

## Flow diagram

```text
Physical wheel/pedals/shifter (USB, DirectInput report)
        |
        v
DirectInputDeviceEnumerator / DirectInputDevice   (src/Devices/DirectInput)
   - non-exclusive, background cooperative level
   - raw axis + button + POV polling
        |
        v
IWheelDevice / DeviceManager                      (src/Core, "DAL")
   - backend-agnostic device discovery and polling
   - AxisNormalizer, DeviceReadinessTracker (state machine)
        |
        v
DeviceProfile (JSON, matched by VID/PID)          (src/Profiles, configs/default_profiles)
   - axis direction/center policy, activation threshold
   - ProfileRepository merges built-in + user profiles; ProfileResolver picks one
        |
        v
rvwheel_device_probe --bridge                     (tools/device_probe)
   - polls DeviceManager at a fixed rate
   - BridgeStateFormatter serializes a WheelState into the RVW2 text protocol
   - writes atomically to %LOCALAPPDATA%\RVWheel\runtime\bridge-state.txt
        |
        v
RVW2 protocol (versioned, sequence-guarded text frame)
        |
        v
mods/RVWheel/Scripts/main.lua (UE4SS, Lua)
   - reads/validates the frame (staleness, torn-read, non-finite guards)
   - maps buttons to gears via a VID:PID H-pattern table
   - calls AVS_Vehicle setters (SetSteeringInput/SetThrottleInput/...)
        |
        v
Game vehicle (AVS_Vehicle_C, RGGearBox) -> rendered driving input
```

`rvwheel_launcher.exe` (tools/launcher) sits outside this runtime chain: it
finds the Steam install, installs/updates the `RVWheel` mod, starts the
bridge as a supervised child process, and launches/attaches to the game. It
does not touch axis data.

## Layers and their responsibilities

### Device Abstraction Layer (`src/Core`, `src/Devices`)

- `IWheelDevice` / `ICalibratableWheelDevice` — the only interface consumers
  depend on. `ICalibratableWheelDevice` is a separate, segregated interface
  (Interface Segregation) so that ordinary polling code never needs to know
  about raw-axis discovery, which only the calibration wizard uses.
- `DirectInputDevice` is the sole backend for the default build. It opens
  devices with `DISCL_NONEXCLUSIVE | DISCL_BACKGROUND`, which Microsoft's own
  cooperative-level model documents as the combination that lets an
  application read a device without blocking other applications (Logitech
  G HUB, the game itself) from doing the same — see
  [Cooperative Levels, Microsoft Learn](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416848(v=vs.85))
  and
  [IDirectInputDevice8::SetCooperativeLevel](http://doc.51windows.net/Directx9_SDK/input/ref/ifaces/idirectinputdevice9/setcooperativelevel.htm).
  Force feedback (which needs exclusive access) is intentionally out of
  scope for the default build.
- `LogitechDevice` / `ILogitechSdk` exist behind `RVWHEEL_ENABLE_LOGITECH_SDK`
  (default `OFF`) so the proprietary Logitech SDK is never a build
  requirement — DirectInput alone already exposes every axis/button/POV this
  project needs.
- `DeviceReadinessPolicy` / `DeviceReadinessTracker` model warmup as an
  explicit state machine (`Unconfigured -> WarmingUp -> Stabilizing ->
  Ready`/`TimedOut`, plus `AwaitingActivation` when a profile requires proof
  of physical movement). This exists because real hardware needs it: the
  G923 reports a stable-looking placeholder value for about two seconds after
  power-up (see [docs/hardware/G923_DIRECTINPUT_CAPTURE.md](hardware/G923_DIRECTINPUT_CAPTURE.md)),
  which a naive "first sample wins" calibration would have accepted as real
  input.

### Profiles (`src/Profiles`, `configs/default_profiles`)

- `DeviceProfile` is a plain JSON-validated data structure: axis bindings,
  direction, center policy, readiness tuning. It contains no I/O and no
  Win32 dependency, so profile-matching logic is fully unit-testable without
  hardware.
- `ProfileRepository` loads a built-in directory (shipped next to the
  executable, read-only) and a user directory
  (`%LOCALAPPDATA%\RVWheel\profiles`, writable, e.g. by `--calibrate`).
  `ProfileResolver` matches by VID/PID and reports *why* it picked a profile
  (`BuiltInProfile`, `UserProfile`, `ProvisionalFallback`, `Unconfigured`,
  `AmbiguousMatch`, `InvalidExactMatch`) instead of silently guessing, which
  is what makes `--list`/`--calibrate` diagnostics trustworthy.
- New hardware is added by shipping a new JSON profile, not by writing new
  C++. See [docs/ADDING_A_WHEEL.md](ADDING_A_WHEEL.md).

### Bridge (`tools/device_probe`, mode `--bridge`)

- A small polling loop: `DeviceManager` -> `AxisNormalizer`/readiness ->
  `BridgeStateFormatter::Format` -> atomic write to a fixed
  `%LOCALAPPDATA%` path. `--parent-pid` lets the launcher supervise it
  without IPC: the bridge simply exits once the parent PID is gone.
- The bridge is a separate process from both the launcher and the game
  because DirectInput polling must run continuously and independently of the
  game's own frame loop, and because a native, statically-typed process is a
  far more natural place to touch DirectInput than Lua is.

### Protocol (`RVW2`)

A single text line, sequence-numbered at both ends:

```text
RVW2 <seq> <connected> <valid> <steering> <throttle> <brake> <clutch> <VID_hex> <PID_hex> <w0> <w1> <w2> <w3> <seq>
```

- Written and read with `std::locale::classic()` / a fixed Lua pattern, so a
  machine's regional decimal separator can never corrupt the frame.
- The leading and trailing `<seq>` let the Lua reader detect a torn read (the
  bridge rewrote the file mid-parse) without any file locking: if the two
  numbers disagree, the frame is discarded and the previous good state (or
  neutral, once stale) is used instead.
- `RVW2` is a version tag by construction: a future incompatible layout ships
  as `RVW3`, and an old Lua mod reading a `RVW3` line simply fails its fixed
  pattern match and falls back to stale/neutral handling — no explicit
  negotiation needed because the transport is a static file, not a live
  connection.
- Four 32-bit hex words cover DirectInput's full 128-button address space
  generically; device-specific meaning (e.g. which button is 3rd gear) is
  resolved entirely in Lua via a `VID:PID` table, so adding a new shifter
  never touches the native bridge or the protocol.

### Game integration (`mods/RVWheel`, UE4SS)

- `main.lua` hooks `AVS_Vehicle_C:TickInputs` and re-installs the hook on
  `PlayerController:ClientRestart` (possession changes, e.g. entering the
  vehicle), since UE4SS hooks are per-UFunction-instance and the vehicle
  Blueprint is not guaranteed to exist at mod-load time.
- Every bridge frame is treated as untrusted external input: non-finite
  values are dropped, stale frames (no sequence change for 2 seconds) fall
  back to the game's native input path, and shifting requires clutch >= 0.5.
  This means a crashed or stalled bridge degrades to "wheel stops working,
  game still playable with keyboard/gamepad" rather than a stuck or runaway
  vehicle.
- Gear changes drive the selector RPC, `SetManualGear`, and the game's own
  `GearBox` state together, following the same sequencing the established
  open-source [Gear Hotkeys](https://github.com/bitterbutt/RVThereYet-GearHotkeys)
  mod uses — an early attempt that called only `SetManualGear` changed the
  physics transmission but left the HUD/selector inconsistent.

### Launcher (`tools/launcher`)

- `LauncherCore.hpp/.cpp` is deliberately pure (VDF parsing, `mods.txt`
  rewriting, bridge-candidate path resolution) and takes strings/paths in,
  strings/paths out — no Win32, no filesystem access — so the exact parsing
  and idempotency logic is unit-tested without a real Steam install.
  `LauncherApp.cpp` is the thin Win32 shell around it (registry reads,
  process/mutex/file APIs).
- Steam discovery reads `HKCU\Software\Valve\Steam\SteamPath` and then parses
  `steamapps/libraryfolders.vdf` for additional library roots. This mirrors
  the approach used by community Steam-library tooling — VDF is a
  Valve-specific key-value format with no official public grammar, so every
  independent implementation (including this one) hand-parses the same
  `"path"` keys; see the format writeups in
  [SteamShutdown's VDF format notes](https://deepwiki.com/akorb/SteamShutdown/4.1.2-vdf-format-specification)
  and the [iw4x launcher's Steam detection](https://deepwiki.com/iw4x/launcher/6.1-steam-library-detection-and-vdfacf-parsing).
- Mod install/update is idempotent and atomic: files are copied, then
  `mods.txt` is rewritten to a temp file and swapped in with
  `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`, so
  a crash mid-write cannot leave a truncated `mods.txt` behind.
- A named mutex (`Local\RVWheelLauncher`) prevents two launcher instances
  from racing to install the mod or start a second bridge.

## Design decisions considered

| Area | Alternatives considered | Decision | Trade-off |
|---|---|---|---|
| Native/game IPC | Named pipe, shared memory, TCP loopback | Atomically-replaced text file under `%LOCALAPPDATA%` | Simplest to implement correctly from Lua (plain `io.open`), trivially inspectable/debuggable by hand, no listener lifecycle to manage; costs one file-write per tick and depends on the OS file cache rather than a true IPC primitive. A future revision could move to a shared-memory ring buffer if per-tick file I/O ever measurably matters. |
| DirectInput access mode | Exclusive (`DISCL_EXCLUSIVE`) for force feedback | Non-exclusive, background (`DISCL_NONEXCLUSIVE \| DISCL_BACKGROUND`) | Lets G HUB and the game read the same device concurrently; defers force feedback (which needs exclusive access) to a separate, explicitly unvalidated milestone. |
| Vendor SDK dependency | Require Logitech SDK for all builds | `RVWHEEL_ENABLE_LOGITECH_SDK` optional, default `OFF`; DirectInput covers all currently supported hardware | Keeps CI and default developer builds free of a proprietary dependency; anyone building with vendor-specific features must opt in explicitly. |
| Device-specific logic placement | VID/PID branches inside the DAL/backend | JSON device profiles (axis/readiness) + a Lua VID:PID table (H-pattern gears) | Adding a wheel is a data change (profile JSON) plus, for non-standard controls like an H-pattern shifter, a small Lua table entry — never a DAL/backend recompile. |
| Mod packaging | UE4SS-specific packaging tooling | Plain directory copy + `mods.txt` line management, matching the manual install flow UE4SS itself documents | Matches what a player would do by hand, so the launcher's install path and the documented manual-install path in [docs/game-integration/UE4SS_FIRST_TEST.md](game-integration/UE4SS_FIRST_TEST.md) never diverge. |
| Distribution format | CPack MSI/NSIS installer | Plain ZIP of launcher + probe + mod + profiles (see [docs/RELEASING.md](RELEASING.md)) | No installer UI/registry footprint to maintain; matches the "extract and run" expectation already set for UE4SS itself. CMake's [CPack module](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Packaging%20With%20CPack.html) remains a documented option if a future release wants component-selectable installers. |

Sources consulted while writing this document: UE4SS's own Lua API reference
([docs.ue4ss.com](https://docs.ue4ss.com/lua-api.html) and
[RegisterHook](https://docs.ue4ss.com/dev/lua-api/global-functions/registerhook.html)),
Microsoft's DirectInput cooperative-level documentation, community VDF format
write-ups, and CMake's CPack documentation — cited inline above next to the
decision each source informed. No UE4SS, DirectInput, or Steam API was
assumed or invented; every function name mentioned in this document
(`SetSteeringInput`, `RegisterHook`, `SetCooperativeLevel`, etc.) is either
directly called by code in this repository or documented at the links above.

## Testability

Every layer above the actual Win32/DirectInput calls is designed to run
without hardware, a Steam install, or UE4SS:

- DAL: `AxisNormalizer`, `DeviceReadinessPolicy/Tracker`, `AxisSource` are
  pure functions/state machines over supplied samples.
- Profiles: `ProfileLoader`/`ProfileRepository`/`ProfileResolver` operate on
  in-memory JSON and temp directories.
- Calibration: `CalibrationWizard` and `StableRawAxisSampler` take
  caller-supplied timestamps and snapshots — no sleeps, no real polling.
- Launcher: `LauncherCore` functions take strings/paths and return
  strings/paths.
- Protocol: `BridgeStateFormatter` is a pure formatter; the Lua parser's
  pattern can be exercised with literal `RVW2 ...` strings.

This is why the test suite ([docs/DEVELOPMENT.md](DEVELOPMENT.md)) can cover
device-absent, profile-absent, Steam-absent, and UE4SS-absent behavior
deterministically in CI, with no physical wheel attached.

## What is not yet automatic

- UE4SS itself is not installed or redistributed by RVWheel; the player
  installs it separately (see [docs/INSTALL.md](INSTALL.md)) because
  redistributing a third-party loader alongside this project would blur
  license and support boundaries that are cleaner left separate.
- Force feedback's safety/effect/mixer infrastructure is implemented and
  unit-tested, but no telemetry source feeds it yet and no force has ever
  been applied to real hardware — see [docs/FORCE_FEEDBACK.md](FORCE_FEEDBACK.md).
- Only the Logitech G923 (VID `046D` PID `C266`) plus its attached H-pattern
  shifter has a verified profile and Lua gear mapping today.
