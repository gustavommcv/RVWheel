# Troubleshooting

This covers problems reported by the launcher, the bridge, or the in-game
mod, in the order you are likely to hit them. If a symptom is not listed
here, check `%LOCALAPPDATA%\RVWheel\logs\bridge.log` first — the bridge is
the component most likely to have written a specific reason.

## Launcher shows an error message box

The launcher reports failures in a Windows message box instead of failing
silently. Common messages:

- **"UE4SS não foi encontrado na pasta do jogo. Instale o UE4SS validado
  antes de usar o launcher."** — UE4SS is not installed. Follow
  [INSTALL.md Step 1](INSTALL.md#step-1--install-ue4ss). The launcher checks
  for `dwmapi.dll` and `ue4ss\UE4SS.dll` next to
  `Ride-Win64-Shipping.exe`; if either is missing, this fires.
- **"rvwheel_device_probe.exe não foi encontrado ao lado do launcher."** —
  the package was extracted incompletely, or `rvwheel_launcher.exe` was
  copied out of the release folder on its own. Re-extract the full RVWheel
  ZIP and run the launcher from inside it.
- **"Não foi possível copiar o mod RVWheel para a pasta do UE4SS." /
  "Não foi possível habilitar RVWheel em ue4ss/Mods/mods.txt."** — usually a
  permissions problem (the game folder is under a location Windows
  restricts, e.g. `Program Files` without elevated rights) or the game is
  running with files locked. Close the game and retry; if it persists, check
  that your Windows user account has write access to the Steam library.
- Launcher appears to do nothing when double-clicked — a previous instance
  may already be running (RVWheel enforces a single launcher instance via a
  named mutex). Check Task Manager for `rvwheel_launcher.exe` and close it,
  or use its existing window/effect if the game already started.

## Game starts but the wheel does nothing

1. Confirm the bridge is actually running: Task Manager should show
   `rvwheel_device_probe.exe`. If not, check
   `%LOCALAPPDATA%\RVWheel\logs\bridge.log` for why it exited.
2. Confirm your device is recognized at all:
   `rvwheel_device_probe.exe --list` should show it with a plausible button
   count. If it is missing, this is a Windows/driver problem, not an
   RVWheel problem — check Windows' own Game Controllers panel
   (`joy.cpl`).
3. Check whether your profile requires activation
   (see [ADDING_A_WHEEL.md](ADDING_A_WHEEL.md#why-activation-gating-exists)):
   move the wheel or a pedal once after entering the game. Until then,
   `valid` stays `false` and no input is applied — this is intentional, not
   a hang.
4. Confirm you are actually driving the vehicle (possessing the Winnebago),
   not walking around on foot — the input hook only applies to the
   possessed vehicle pawn.
5. If the bridge log shows the frame is being written but the game still
   ignores it, the frame may be going stale: the Lua mod requires a fresh
   sequence number at least once every 2 seconds. A bridge that has stalled
   (e.g. lost the device) will fall back to the game's native input rather
   than freezing the last value.

## Steering/pedals feel inverted or centered wrong

This is a profile problem, not a code bug: `direction`/`center` in the
device's JSON profile were derived from a specific unit's observed behavior,
and a different regional SKU or a driver update can change what "released"
vs. "pressed" raw values look like (this has been observed even on the same
G923 model across sessions — see the addenda in
[G923_DIRECTINPUT_CAPTURE.md](hardware/G923_DIRECTINPUT_CAPTURE.md)). Re-run
`--calibrate` to generate a fresh user profile for your exact unit; it
overrides the built-in one automatically once it shares the same
`profileId`, or use a distinct `profileId` to keep both side by side while
comparing.

## Shifter gears do nothing / wrong gear engages

- Gear changes require the clutch pedal past 50% (`CLUTCH_SHIFT_THRESHOLD`
  in `main.lua`) — this is intentional, matching how a manual-transmission
  wheel setup is expected to be driven.
- If two shifter gates appear pressed simultaneously (a worn or miswired
  shifter), the Lua side treats this as an ambiguous H-pattern state and
  ignores the shift entirely rather than guessing.
- If your shifter model is not the verified Logitech unit, it has no entry
  in `H_PATTERN_SHIFTERS` and gear input from it is not read at all — see
  [ADDING_A_WHEEL.md §5](ADDING_A_WHEEL.md#5-non-standard-controls-h-pattern-shifters-extra-buttons).

## "Ambiguous match" or "Invalid exact match" from `--list`/`--calibrate`

`ProfileResolver` reports these instead of silently picking a profile when
more than one profile claims the same exact VID/PID, or when a profile
matching your device fails its own JSON validation. Check
`%LOCALAPPDATA%\RVWheel\profiles\` for a leftover or duplicate profile file
and remove or fix the one you did not intend to use.

## My wheel doesn't vibrate / has no force feedback

This is expected today, on every wheel, regardless of its capabilities.
Force feedback's infrastructure exists and is unit-tested, but no code path
in the launcher or bridge ever enables it, and no telemetry source feeds it
— see [FORCE_FEEDBACK.md](FORCE_FEEDBACK.md) for the exact status. This is
not a bug to report; it is documented, deliberate, unfinished work.

## No Steam / no game / no UE4SS / no wheel at all

Each of these is a supported, deliberately-non-crashing state:

- No Steam installed, or the game not owned/installed: the launcher reports
  it cannot find the game and exits cleanly with a message box; it does not
  attempt to install Steam or purchase the game.
- No UE4SS installed: see the launcher error above.
- No wheel attached: `rvwheel_device_probe --list` reports zero devices;
  `--bridge` keeps running and simply reports `connected=false` frames,
  which the Lua mod treats the same as a stale/absent bridge (native input
  keeps working).
- No profile matches an attached device: readiness reports `Unconfigured`
  and input is not applied for that device; the rest of the system is
  unaffected.

## Where to look next

- Bridge behavior and log format: `%LOCALAPPDATA%\RVWheel\logs\bridge.log`.
- Architecture and protocol details: [ARCHITECTURE.md](ARCHITECTURE.md).
- Hardware-specific measured quirks: [hardware/](hardware/).
- If none of the above explains it, open an issue with your
  `rvwheel_device_probe --list` output, the relevant section of
  `bridge.log`, and exactly what you did — vague reports ("wheel doesn't
  work") without that context are difficult to act on.
