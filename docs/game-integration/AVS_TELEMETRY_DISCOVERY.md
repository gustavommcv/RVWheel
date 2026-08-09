# AVS vehicle telemetry discovery (RVWheelDiscovery F10/F11)

This records one real, authorized in-game capture session using
`RVWheelDiscovery`'s F10 (targeted AVS schema reflection) and F11
(lightweight `AActor` telemetry snapshot) — see
[mods/RVWheelDiscovery/README.md](../../mods/RVWheelDiscovery/README.md)
for what each key does and its safety constraints. This document records
**only what was observed**: measured values and log output. Where the
underlying cause of something is not independently confirmed, this
document says so explicitly rather than guessing.

## Session setup

- Launcher run with no arguments (`rvwheel_launcher.exe`, no
  `--enable-force-feedback`) — the bridge was input-only for this entire
  session; no force feedback was armed at any point.
- `RVWheelDiscovery` installed and enabled in `ue4ss/Mods/mods.txt`
  alongside the existing `RVWheel` mod.
- Vehicle: `BP_Vehicle_Winnebago_01_C`, map `SnowLevel`.
- F10 was pressed 6 times and F11 was pressed 6 times over the session
  (stopped, driving in a straight line, a sharp/closed curve, and a
  gentle curve).
- Every F10 run produced identical structural results: 145 pawn-class
  members matched, 4 component objects followed, 200 component members
  logged (the component budget's own cap, `F10_MAX_COMPONENT_MEMBERS`).
- No `[RVWheelDiscovery]`-prefixed crash/error line occurred at any point
  other than the one `GetActorRotation` outcome recorded below, which the
  mod logged as designed (as "unavailable") without crashing.
- **Known, unrelated observation, cause not investigated**: the bridge
  log for this session recorded 3629 "failed to publish bridge frame"
  warnings out of 81368 polls. This is the bridge's own
  `WriteBridgeStateAtomically` input-state write, unrelated to force
  feedback (none was armed this session) and unrelated to
  `RVWheelDiscovery`. No cause has been established for these failures;
  this document does not attribute them to the UE4SS console overlay,
  system load, or anything else.

## F11 — lightweight `AActor` telemetry snapshot

Four functions were called each time F11 was pressed:
`GetVelocity()`, `GetActorForwardVector()`, `GetActorRightVector()`,
`GetActorRotation()`. All four calls are independently `pcall`-protected
in `capture_actor_telemetry_snapshot` (`mods/RVWheelDiscovery/Scripts/main.lua`).

### `GetActorRotation()`

Every one of the 6 presses produced the same logged outcome:

```
F11 GetActorRotation: unavailable (...RVWheelDiscovery\Scripts\main.lua:425: attempt to call a TrivialObject value (field '?'))
```

This is the complete observed symptom. No cause has been independently
confirmed for why this specific call behaves this way in this game/UE4SS
combination — this document does not claim it is a general UE4SS/Lua
binding limitation, a property-vs-function name collision, or anything
else. What is confirmed is that the call did not return a usable
rotation value, and that the surrounding `pcall` correctly caught this so
the rest of the snapshot (velocity-derived values) still ran.

### `GetVelocity()` / `GetActorForwardVector()` / `GetActorRightVector()`

All three succeeded on all 6 presses. Logged values (velocity in
Unreal's native cm/s; `speed` also converted to km/h; `forwardSpeed`/
`lateralSpeed` are the dot products of velocity against the actor's own
forward/right vectors, computed in `main.lua`):

| # | Condition (as driven) | velocity X/Y/Z (cm/s) | speed | forwardSpeed | lateralSpeed |
|---|---|---|---|---|---|
| 1 | Stopped | 0.475 / 1.821 / -2.371 | 3.027 cm/s (0.109 km/h) | 1.302 cm/s | 2.428 cm/s |
| 2 | Stopped | 0.022 / -0.508 / -0.511 | 0.721 cm/s (0.026 km/h) | -0.192 cm/s | -0.128 cm/s |
| 3 | Stopped | -1.267 / -4.942 / 1.704 | 5.378 cm/s (0.194 km/h) | -3.379 cm/s | -4.136 cm/s |
| 4 | Driving in a straight line | 292.822 / 370.402 / 0.368 | 472.168 cm/s (16.998 km/h) | 472.167 cm/s | 0.119 cm/s |
| 5 | Sharp/closed curve | -32.619 / 380.806 / -4.310 | 382.224 cm/s (13.760 km/h) | 369.446 cm/s | 81.855 cm/s |
| 6 | Gentle curve | 55.415 / 46.599 / 0.683 | 72.407 cm/s (2.607 km/h) | 38.270 cm/s | -61.461 cm/s |

Observed pattern in these six samples: while stopped, `forwardSpeed` and
`lateralSpeed` are both small (consistent with near-zero total speed).
Driving in a straight line, `forwardSpeed` accounts for nearly all of
`speed` and `lateralSpeed` is near zero. In both curve samples,
`lateralSpeed` is a substantial fraction of (sample 5) or larger than
(sample 6) `forwardSpeed`. This is the complete observation; this
document does not generalize beyond these six samples (e.g. it does not
claim a specific curve radius/speed relationship).

**Confirmed reachable, no reflection required**: speed and a
forward/lateral velocity decomposition, via `GetVelocity()`,
`GetActorForwardVector()`, and `GetActorRightVector()` on the possessed
vehicle pawn, treated as a plain `AActor`.

**Not confirmed**: yaw rate. `GetActorRotation()` did not produce a
usable value in this session (see above); no other yaw-rate-specific
call was attempted in this pass.

## F10 — targeted AVS schema (one reflection pass)

Class chain walked from the possessed pawn (identical across all 6 runs):

```
[0] BlueprintGeneratedClass /Game/Ride/Vehicle/Blueprints/BP_Vehicle_Winnebago_01.BP_Vehicle_Winnebago_01_C
[1] BlueprintGeneratedClass /Game/Ride/Vehicle/Blueprints/BP_VehicleBase.BP_VehicleBase_C
[2] BlueprintGeneratedClass /VehicleSystemPlugin/AVS_Vehicle.AVS_Vehicle_C
[3] Class /Script/VehicleSystemPlugin.VehicleSystemBase
[4] Class /Script/Engine.Pawn
[5] Class /Script/Engine.Actor
[6] Class /Script/CoreUObject.Object
```

Properties logged whose name matched the F10 keyword list
(`speed, veloc, angular, yaw, lateral, forward, rpm, slip, suspension,
compression, load, contact, surface, impact, collision, movement,
component, chassis, wheel`), on the vehicle-specific classes only
(generic `Actor`/`Object`-level matches, e.g. `bReplicateMovement`,
`InputComponent`, omitted here for brevity — they are in the raw
`UE4SS.log` capture):

- **`BP_Vehicle_Winnebago_01_C`**: `Wheel_FR`/`Wheel_FL`/`Wheel_RR`/`Wheel_RL`
  (`ObjectProperty`, each followed one level as a component -- see
  below), `GameTimeSinceImpact`, `OffsetGameTimeOnImpact`,
  `ImpactForceThreshold` (`DoubleProperty`), `LoadedDestroyedParts`
  (`ArrayProperty`).
- **`BP_VehicleBase_C`**: `PossibleWheelMeshes`, `AppliedWheelMeshes`,
  `CommonSurfaceEffects` (`ArrayProperty`), `OnAirSpeedUpdated`
  (`MulticastInlineDelegateProperty`).
- **`AVS_Vehicle_C`**: `Wheels` (`ArrayProperty`), `RPM`, `dRPM`
  (`DoubleProperty`), `NumDrivingWheels` (`IntProperty`), `SpeedUnits`
  (`ByteProperty`), `SpeedUnit` (`StructProperty`), `AirSpeed`,
  `TargetSpeed`, `Slip`, `IdleRPM`, `IdleMaxRPM`,
  `VehicleMaxAngularVelocity` (`DoubleProperty`), `WheelReprojection`,
  `WheelReprojectionCamber`, `ContactModificationDirty`,
  `SuspensionDebug` (`BoolProperty`), `VehicleSpeedFromWheelRotation`
  (`DoubleProperty`).
- **`VehicleSystemBase`**: `VehicleWheels`, `ContactModMeshes`
  (`ArrayProperty`), `ReplicateMovement` (`BoolProperty`),
  `RestVelocityThreshold`, `SteeringSpeed`, `SteeringRecenterSpeed`
  (`FloatProperty`).
- **`Actor`** (inherited, so present on the vehicle too): `ReplicatedMovement`
  (`StructProperty`) — the standard `AActor` replication-movement struct.
  Its fields were not individually reflected or read in this pass.

None of these properties were read (no value was fetched for any of
them in this pass) and no discovered function was invoked — F10 only
ever logs name/type/owner, per its own design.

### Component follow-up (one level into the 4 wheel objects)

All four `Wheel_FR`/`Wheel_FL`/`Wheel_RR`/`Wheel_RL` properties resolved
to distinct, valid objects of the same class,
`/VehicleSystemPlugin/VehicleAssets/Components/Vehicle_Wheel.Vehicle_Wheel_C`.
Deduplicated property names found on that class (identical across all 4
wheel instances):

`SurfaceEffects` (Array), `HaveInitializedFromLoadYet` (Bool),
`IsWheelInAir` (Bool), `LastSurfaceHit` (Byte),
`CurrentAudioSurfaceIndex` (Double), `LastSuspensionImpactInGameTime`
(Double), `RotationSpeed` (Double), `SkidEffectSpeed` (Double),
`WheelTorque` (Double), `SkidEffectSpeedBySurface` (Map),
`AppliedWheelPhysMat` (Object), `WheelController` (Object, not followed
further -- one level was the limit), `ServerLoadScrewTimer` (Struct),
`WheelTag` (Struct).

None of these were read either. In particular: **`WheelTorque` and
`RotationSpeed` were found and named only** -- this document does not
characterize either one as usable for yaw rate or any other derived
quantity; that would require actually reading them (not done in this
pass) and reasoning about what they represent, which has not been done.

Several `Vehicle_Wheel_C` functions were also logged (e.g.
`GetWheelStatus`, `Update_WheelSuspension_Buffer`, `ApplyWheelModifiers`)
-- names only, never invoked.

## Summary of what this session confirms and does not confirm

| Question | Status |
|---|---|
| Speed reachable? | **Confirmed** -- `GetVelocity()` on the possessed pawn, no reflection. |
| Lateral velocity reachable? | **Confirmed** -- dot product of `GetVelocity()` against `GetActorRightVector()`; six samples across stopped/straight/two curves produced physically consistent numbers. |
| Yaw rate reachable? | **Not confirmed.** `GetActorRotation()` did not return a usable value in this session (exact log line above). `ReplicatedMovement` and per-wheel fields are named candidates only, not read. |
| Per-wheel load / suspension compression reachable? | **Not confirmed.** `Vehicle_Wheel_C`'s own properties were named but not read; `WheelController` (one further level) was not explored. |
| Does any of this require reflection at runtime? | **No**, for the confirmed speed/lateral path -- it uses only `GetVelocity`/`GetActorForwardVector`/`GetActorRightVector`, the same three functions the continuous telemetry transport uses. |

## Raw data

The full `[RVWheelDiscovery]`-prefixed log lines for this session are in
the game's `ue4ss/UE4SS.log` at the time of capture; this document is a
curated summary, not a replacement for that raw log.
