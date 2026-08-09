# UE4SS first in-game test

Validated on 2026-08-08 against Steam app `3949040` (*RV There Yet?*), local
game build `22864294`.

## Loader selection

The game identifies itself as Unreal Engine 5.6 in `UE4SS.log`. The validated
loader is the official UE4SS `experimental-latest` artifact:

```text
zDEV-UE4SS_v3.0.1-1021-g1c1a1497.zip
SHA-256 497F7106E19C866F38511699FFAECC17AE2A032682066D5ED8029AC61532D517
```

This repository intentionally does not vendor the UE4SS binaries.

## Installation layout

Extract the package into the directory containing `Ride-Win64-Shipping.exe`.
The resulting relevant layout is:

```text
Ride/Binaries/Win64/
|-- Ride-Win64-Shipping.exe
|-- dwmapi.dll
`-- ue4ss/
    |-- UE4SS.dll
    |-- UE4SS-settings.ini
    `-- Mods/
```

Copy [`mods/RVWheel`](../../mods/RVWheel) and
[`mods/RVWheelDiscovery`](../../mods/RVWheelDiscovery) into the UE4SS `Mods`
directory and add the following entries to `ue4ss/Mods/mods.txt`:

```text
RVWheel : 1
RVWheelDiscovery : 1
```

When installing while the game is open, press `Ctrl+R` to hot reload all mods.

## Real test result

The process remained responsive with both the local `dwmapi.dll` proxy and
`ue4ss/UE4SS.dll` loaded. The discovery mod loaded successfully, handled its F8
keybind, and found these runtime objects in `SnowLevel`:

```text
PlayerController:
  BP_FirstPersonPlayerController_C
  /Game/Ride/Character/Blueprints/BP_FirstPersonPlayerController

On foot pawn:
  BP_FirstPersonCharacter_C
  /Game/Ride/Character/Blueprints/BP_FirstPersonCharacter

Driven vehicle pawn:
  BP_Vehicle_Winnebago_01_C
  /Game/Ride/Vehicle/Blueprints/BP_Vehicle_Winnebago_01
```

Possession changes between the character and the Winnebago invoke
`PlayerController.ClientRestart`, which gives RVWheel a reliable lifecycle hook
for activating and deactivating vehicle input.

The discovery mod deliberately does not alter game state. The separate
`RVWheel` mod now supplies the validated input bridge described below.

## Playable bridge result

`rvwheel_device_probe --bridge --rate 60` publishes an atomically replaced,
sequence-guarded `RVW2` frame under
`%LOCALAPPDATA%/RVWheel/runtime/bridge-state.txt`. DirectInput uses
non-exclusive background access so G HUB and the game can coexist with the
host. The Lua consumer rejects torn, stale, disconnected, invalid, or
non-finite frames and returns control to the native input path when the host
stops.

A real single-player drive validated:

- steering from `-1` through `+1`, including the final AVS `Steering` property;
- throttle and brake from `0` through `1`;
- clutch as the safety gate for every H-pattern change;
- Logitech shifter neutral, forward gears 1–5, and reverse;
- safe coexistence of the game, G HUB, UE4SS, and the bridge host.

The Logitech G923 PS/PC shifter reports buttons 12–17 for gates 1–6 and button
18 for reverse. The game exposes five forward gears, so gate 6 intentionally
resolves to neutral. Gear changes must update both the AVS vehicle and the
game-specific `RGGearBox`; the established open-source
[Gear Hotkeys](https://github.com/bitterbutt/RVThereYet-GearHotkeys) mod was
used as the behavioral reference for that integration seam.

The first direct `SetManualGear` experiment demonstrated why this distinction
matters: AVS state changed without keeping the game's selector/HUD layer
consistent. The final implementation instead drives the selector RPC, manual
gear, and gearbox state together and requires at least 50% clutch input.

## Vehicle input API discovered

The F9 reflection completed successfully against the possessed Winnebago. Its
inheritance chain includes the Advanced Vehicle System plugin Blueprint class:

```text
/VehicleSystemPlugin/AVS_Vehicle.AVS_Vehicle_C
```

The runtime exposes direct candidate setters:

```text
SetSteeringInput
SetThrottleInput
SetBrakeInput
SetHandbrakeInput
SetThrottleAndBrakeInput
```

It also exposes `DoubleProperty` values named `SteeringInput`, `ThrottleInput`,
and `BrakeInput`, plus the server RPCs `RPC_Server_Steering`,
`RPC_Server_Throttle`, and `RPC_Server_Brake`. The setter functions are the
preferred first integration seam; the RPCs should remain owned by the game's
existing vehicle logic unless multiplayer testing proves otherwise.

## Controls

- `F8`: capture world, controller, pawn, and vehicle instances.
- `F9`: enumerate input-related functions and properties on the possessed pawn.
- `Ctrl+R`: hot reload UE4SS mods. Use only during development; a full hot
  reload tears down every mod and is significantly heavier than normal play.

## Removal

Close the game, then remove the local `dwmapi.dll` and the `ue4ss` directory from
`Ride/Binaries/Win64`. Steam game files are otherwise untouched.
