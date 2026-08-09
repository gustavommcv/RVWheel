# RVWheelDiscovery

Temporary UE4SS Lua mod used to discover the actual player and vehicle classes in
*RV There Yet?* without guessing game internals.

The mod is intentionally read-only and manual: every capture below runs once,
only when its key is pressed, only on the game thread, and never per-tick. It
logs object/class/property names and, for F11, a few read-only numeric
values -- it never writes a property, never calls a function that takes
parameters, and never changes vehicle input or force feedback state. After
entering a drivable vehicle:

- press **F8** to capture the current world, player controller, pawn, and known
  Unreal vehicle instances;
- press **F9** to enumerate input-related functions and properties on the actual
  vehicle class and its parent classes (steering/throttle/gear -- the properties
  the RVWheel bridge already uses);
- press **F10** for a targeted AVS telemetry *schema* discovery: walks the
  possessed vehicle's class and superclasses (bounded depth and result count)
  looking for members whose name suggests speed, velocity, yaw/angular rate,
  suspension, load, collision, or a movement/wheel/chassis component --
  exactly the properties the force-feedback research identified as never
  having been searched for (F9's own keyword list only ever covered input).
  For each match it logs the full name, the reflected property/function type,
  and the owning class -- it never invokes a discovered function. When a
  match is itself an object reference to something named like a component
  (movement, wheel, suspension, chassis), F10 also tries to read that object
  and reflects one additional level into it (also bounded, deduplicated by
  address) -- never assuming `GetComponents()` exists, and never recursing
  further;
- press **F11** for a lightweight *numeric* telemetry snapshot: no reflection
  at all, just four known, read-only `AActor` functions (`GetVelocity`,
  `GetActorForwardVector`, `GetActorRightVector`, `GetActorRotation`), each
  independently guarded so one being unavailable in this game/UE4SS version
  is logged as such and does not stop the rest. When the velocity/forward/
  right vectors are readable, it logs velocity X/Y/Z, speed in cm/s and km/h,
  and the forward/lateral components of velocity (dot products against the
  actor's own forward/right vectors) -- the actual numbers a future force
  feedback telemetry producer would need, without committing to any
  reflection-based access path.

F10 and F11 exist to answer the open question in
[docs/research/FORCE_FEEDBACK_FEASIBILITY.md](../../docs/research/FORCE_FEEDBACK_FEASIBILITY.md)
(§7, question 1) about whether AVS exposes usable telemetry at all --
running them and reading their `[RVWheelDiscovery]` log output is the whole
point; neither one feeds anything back into the bridge, the Lua vehicle
input hook, or force feedback on its own.

Install this directory as:

```text
<game>\Ride\Binaries\Win64\ue4ss\Mods\RVWheelDiscovery
```

Then add this line to the UE4SS `Mods/mods.txt` file:

```text
RVWheelDiscovery : 1
```
