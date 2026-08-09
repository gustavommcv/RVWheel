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

Copy [`mods/RVWheelDiscovery`](../../mods/RVWheelDiscovery) into the UE4SS
`Mods` directory and add the following entry to `ue4ss/Mods/mods.txt`:

```text
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

This validates the loader and game-object discovery only. Wheel input injection
is the next milestone; the discovery mod deliberately does not alter game state.

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
- `Ctrl+R`: hot reload UE4SS mods.

## Removal

Close the game, then remove the local `dwmapi.dll` and the `ue4ss` directory from
`Ride/Binaries/Win64`. Steam game files are otherwise untouched.
