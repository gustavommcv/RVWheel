# RVWheelDiscovery

Temporary UE4SS Lua mod used to discover the actual player and vehicle classes in
*RV There Yet?* without guessing game internals.

The mod is intentionally read-only: it logs object names and does not change game
state or inject wheel input. After entering a drivable vehicle:

- press **F8** to capture the current world, player controller, pawn, and known
  Unreal vehicle instances;
- press **F9** to enumerate input-related functions and properties on the actual
  vehicle class and its parent classes.

Install this directory as:

```text
<game>\Ride\Binaries\Win64\ue4ss\Mods\RVWheelDiscovery
```

Then add this line to the UE4SS `Mods/mods.txt` file:

```text
RVWheelDiscovery : 1
```
