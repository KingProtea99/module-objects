# Basic objects

Three small pieces most action games need: **trigger volumes** (who is inside this box or sphere), **path platforms** (where a moving platform is at a given tick) and **item spawn points** (is the item there, when does it come back).

- Version 0.1.0, engine 0.9, no required modules (no physics)
- State: `objects::Trigger`, `objects::ItemSpawn`; static data: `objects::Path`
- Simulation: `sim/Objects.h`

## Adding

```
tools\module.ps1 add -Project <your project> objects
```

Put the state in your simulation state, usually one per scene object read at match start:

```cpp
// source/sim/World.h
#include "objects/sim/Objects.h"

MEALKIT_STRUCT(WorldState) {
    ...
    MEALKIT_FIELD(objects::Trigger, goal, {}, "Goal zone");
    MEALKIT_FIELD(objects::ItemSpawn, healthPack, {}, "Health pack");
};
```

## Usage

**Triggers.** Pass the positions of what can enter, each with a slot index below 64 (a player slot or a pool slot). The result says who came in and who left; walk it with `forEachBit` so events come out in slot order.

```cpp
const objects::TriggerChange c = objects::update(state.goal, slots, positions, count);
objects::forEachBit(c.entered, [&](u32 slot) { scoreGoal(slot); });
```

A slot you stop passing in (a pawn that died) is reported as exited.

**Path platforms.** Fill a `Path` from a scene object (up to 8 points, a whole number of ticks per leg, `Loop`, `PingPong` or `Once`). Each tick, move your kinematic body to `positionAt(path, tick)`; `stepDelta` gives the movement over one tick for its velocity. Convert seconds with `mealkit::Ticks` when reading the object.

**Item spawn points.** `tryTake(spawn, tick, respawnTicks)` returns true once and starts the wait; `available` tells the view whether to draw the item.

## How it works

- Triggers test points against an axis-aligned box (edges inside) or a sphere (`halfSize.x` is the radius) with squared distances, so there is no `sqrt`. Membership is one `u64` bit mask, which is the only state and goes into the world hash.
- A path position is computed from the tick alone: `tick - startTick` wraps by the total ticks (twice that for ping-pong, walking the forward path backwards), then interpolates on the leg. Nothing is accumulated, so a late joiner or a rewind gets the same position.
- Item spawns are a `TickTimer` (one tick number) plus a count.
- For exact body shapes rather than points, use the physics module's queries in your game instead of triggers.

## Version history

- 0.1.0: first version
