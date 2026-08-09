# RVWheel UE4SS bridge

First playable bridge between the RVWheel DirectInput/profile host and the
Advanced Vehicle System used by *RV There Yet?*.

## Run

1. Install this directory as `ue4ss/Mods/RVWheel` and enable
   `RVWheel : 1` in `ue4ss/Mods/mods.txt`.
2. Start the host from a normal interactive PowerShell:

   ```powershell
   .\rvwheel_device_probe.exe --bridge --rate 60
   ```

3. Move one wheel/pedal axis once if the host reports
   `AwaitingActivation`, then enter the Winnebago.

The Lua side reads `%LOCALAPPDATA%\RVWheel\runtime\bridge-state.txt`. It only
applies input while frames are connected, Ready, internally consistent, and
fresh. On stale/invalid input it sends one neutral frame and returns control to
the game's normal input path.

The `RVW2` transport carries steering, throttle, brake, clutch, VID/PID, and
all 128 DirectInput button bits. The verified `046D:C266` map additionally
supports the Logitech Driving Force H-pattern shifter:

| Gate | DirectInput button | Game action |
|---|---:|---|
| Neutral | none | Neutral |
| 1–5 | 12–16 | Manual gears 1–5 |
| 6 | 17 | Neutral (the RV has no sixth gear) |
| Reverse | 18 | Reverse |

Shifts are accepted only while the normalized clutch is at least `0.5`. The
Lua side follows the game's own two-layer gearbox path: it updates the
Park/Reverse/Neutral/Drive selector, applies the AVS manual gear, and updates
the `RGGearBox` state. Calling `SetManualGear` by itself is insufficient.

This flow was derived from and should credit bitter's open-source
[Gear Hotkeys](https://github.com/bitterbutt/RVThereYet-GearHotkeys) mod.

Validated in a real single-player drive on 2026-08-08: steering, throttle,
brake, clutch-gated gears 1–5, neutral, and reverse worked. Multiplayer,
force feedback, packaging, and automatic host startup remain separate
milestones.
