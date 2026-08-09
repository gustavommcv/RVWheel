# Installing RVWheel (players)

This guide is for playing *RV There Yet?* with a racing wheel. If you want to
build RVWheel from source or add support for a new device, see
[DEVELOPMENT.md](DEVELOPMENT.md) or [ADDING_A_WHEEL.md](ADDING_A_WHEEL.md)
instead.

> [!IMPORTANT]
> RVWheel is a development preview. It automates mod install/enable, bridge
> startup, and launching the game — it does **not** yet install UE4SS for
> you, and it currently ships one verified device profile (Logitech G923).
> Other DirectInput wheels may work but are unverified; see
> [Current status](../README.md#current-status).

## Requirements

- Windows 10/11 x64.
- *RV There Yet?* owned and installed via Steam.
- Your wheel's manufacturer software installed if the wheel needs it to be
  recognized by Windows at all (e.g. Logitech G HUB). RVWheel talks to the
  device through DirectInput; it does not require the manufacturer software
  to stay running, only that Windows sees the device.
- UE4SS installed into the game's `Binaries\Win64` folder (see below). RVWheel
  does not install or redistribute UE4SS itself.

## Step 1 — Install UE4SS

RVWheel does not bundle UE4SS: it is a separate, actively developed project
with its own license and update cycle, and redistributing a third-party
loader would blur support and licensing boundaries better left separate.

1. Download the loader version validated in
   [docs/game-integration/UE4SS_FIRST_TEST.md](game-integration/UE4SS_FIRST_TEST.md)
   (or a newer `experimental-latest` release) from the official
   [UE4SS releases](https://github.com/UE4SS-RE/RE-UE4SS/releases).
2. Extract it into the folder that contains `Ride-Win64-Shipping.exe` (typically
   `...\steamapps\common\Ride\Ride\Binaries\Win64\`). After extraction you
   should have `dwmapi.dll` and a `ue4ss\` folder next to the game executable.

## Step 2 — Get the RVWheel package

Download the latest RVWheel release ZIP (see
[RELEASING.md](RELEASING.md) for what it contains) and extract it anywhere
convenient, e.g. `Documents\RVWheel\`. You should see:

```text
rvwheel_launcher.exe
rvwheel_device_probe.exe
RVWheel\               (the UE4SS mod)
configs\default_profiles\
README.md
LICENSE
```

Do not extract it inside the game's own folder — the launcher copies what it
needs into the game directory itself.

## Step 3 — Connect your wheel before starting

Plug in and power on your wheel, pedals, and shifter before running the
launcher. RVWheel's readiness policy needs to see the device from a
consistent power-up state; hot-plugging after the bridge has already started
is not currently a validated path.

## Step 4 — Run the launcher

Double-click `rvwheel_launcher.exe`. It will, in order:

1. Locate the Steam library containing *RV There Yet?*.
2. Verify UE4SS is present (fails with a clear message if not — see Step 1).
3. Copy/update the `RVWheel` mod into `ue4ss\Mods\` and enable it in
   `mods.txt`.
4. Start the input bridge in the background (no console window).
5. Launch the game through Steam, or reuse it if it is already running.

If the game or the bridge is already running, the launcher reuses them
instead of starting a second copy.

## Step 5 — Verify it works

1. Once in-game and driving, move the wheel: steering should respond
   immediately.
2. If your device profile requires activation (the G923 default profile
   does), move any axis once before input is applied — this is intentional,
   see [ADDING_A_WHEEL.md](ADDING_A_WHEEL.md#why-activation-gating-exists).
3. If you have a Logitech H-pattern shifter, gear changes require the clutch
   pedal pressed past 50%.

If steering/pedals do nothing, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
before assuming hardware is unsupported.

## Step 6 — Logs, updating, and uninstalling

- Bridge logs: `%LOCALAPPDATA%\RVWheel\logs\bridge.log`.
- Your saved/calibrated profiles: `%LOCALAPPDATA%\RVWheel\profiles\`
  (untouched by uninstall/update).
- **Update**: download a newer RVWheel package and run its launcher; it
  overwrites the mod files and `mods.txt` entry idempotently.
- **Uninstall**: close the game, delete `ue4ss\Mods\RVWheel` from the game's
  `Binaries\Win64` folder, and remove the `RVWheel : 1` line from
  `ue4ss\Mods\mods.txt`. To remove UE4SS entirely as well, delete `dwmapi.dll`
  and the `ue4ss` folder (this disables every UE4SS mod, not just RVWheel).
  Your Steam game files are otherwise untouched by either mod or launcher.
