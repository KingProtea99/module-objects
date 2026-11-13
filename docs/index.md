# Basic objects

Small pieces most action games need: **trigger volumes** (who is inside this box or sphere), **path platforms** (where a moving platform is at a given tick), **timers** (is it on at this tick), **rule rows** (a designer combines a condition and an effect in a table, such as "player inside → speed x0.5") and **item spawn points** (is the item there, when does it come back).

- Version 0.2.0, engine 0.9, no required modules (no physics)
- State: `objects::Trigger`, `objects::Effects`, `objects::ItemSpawn`; static data: `objects::Path`, `objects::Timer`
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

**Timers.** A `Timer` is on for `onTicks` at the start of every `period` from `startTick` (`period` 0 = once). `timerOn(timer, tick)` and `timerEdge(timer, tick)` (`Started`, `Ended`, `None`) are pure functions of the tick, so there is no state to keep.

**Item spawn points.** `tryTake(spawn, tick, respawnTicks)` returns true once and starts the wait; `available` tells the view whether to draw the item.

## Rule rows

Programmers decide what can happen; designers decide when and how much. The game declares an effect list and a row type with four fields, the rows go in a table, and scene objects pick a row:

```cpp
// source/sim/Tables.h
MEALKIT_ENUM(RuleEffect, mealkit::u8, Speed);   // the game's effects; add kinds as maps need them
MEALKIT_STRUCT(Rule) {
    MEALKIT_FIELD(objects::RuleWhen, when, objects::RuleWhen::Inside, "When");
    MEALKIT_FIELD(RuleEffect, effect, RuleEffect::Speed, "Effect");
    MEALKIT_FIELD(mealkit::f32, amount, 1.0f, "Amount");
    MEALKIT_FIELD(mealkit::Ticks, duration, {}, "Duration (s, 0 = while it lasts)");
};
MEALKIT_TABLE(Rule, "tables/rules.table", 0);

// source/sim/Objects.h: a zone (area) and a timer that pick a row
MEALKIT_OBJECT(RuleZone) {
    static constexpr mealkit::u32 kObjectFlags = mealkit::reflect::kObjectArea;
    MEALKIT_FIELD(mealkit::RowRef<Rule>, rule, {}, "Rule");
};
MEALKIT_OBJECT(RuleTimer) {                     // copy the three times into an objects::Timer when loading the scene
    MEALKIT_FIELD(mealkit::Ticks, period, {}, "Period (s, 0 = once)");
    MEALKIT_FIELD(mealkit::Ticks, onTime, {}, "On for (s)");
    MEALKIT_FIELD(mealkit::Ticks, start, {}, "First start (s)");
    MEALKIT_FIELD(mealkit::RowRef<Rule>, rule, {}, "Rule");
};
```

Keep one `objects::Effects` per player slot in the state and the zones' `Trigger`s in a fixed array. Once per tick, at one place in `World::step`:

```cpp
for (u32 s = 0; s < kSlots; ++s) objects::expire(state.slots[s].effects, tick);
for (zone : zones) {                       // scene order
    const objects::TriggerChange c = objects::update(zone.trigger, slots, feet, count);
    objects::runZoneRule(rules[zone.rule], zone.rule, c, effects, kSlots, tick);
}
for (timer : timers)
    objects::runTimerRule(rules[timer.rule], timer.rule, objects::timerEdge(timer.timer, tick), playersMask, effects, kSlots, tick);
```

Then apply each kind where it belongs, such as `speed *= objects::product(p.effects, u8(RuleEffect::Speed), tick)`. `sum` adds amounts and `has` tells whether any is active.

| `when` | What happens |
|---|---|
| `Enter` | A slot that comes in gets the effect for `duration` (0 = until it leaves) |
| `Inside` | The effect lasts while the slot is inside |
| `Exit` | A slot that leaves gets the effect for `duration` |
| `Timer` | Used by a timer: everyone passed in gets it when the timer starts, for `duration` (0 = until the timer ends) |

- The same rule doesn't stack: entering again refreshes its amount and end. Different rules of the same kind multiply (`product`) or add (`sum`).
- Each slot holds up to four effects (`kMaxEffects`); a fifth replaces the one ending first.
- Rows are table data, so they are in the content hash, and the rules run at one fixed point in the tick: peers stay in step.

## How it works

- Triggers test points against an axis-aligned box (edges inside) or a sphere (`halfSize.x` is the radius) with squared distances, so there is no `sqrt`. Membership is one `u64` bit mask, which is the only state and goes into the world hash.
- A path position is computed from the tick alone: `tick - startTick` wraps by the total ticks (twice that for ping-pong, walking the forward path backwards), then interpolates on the leg. Nothing is accumulated, so a late joiner or a rewind gets the same position.
- Item spawns are a `TickTimer` (one tick number) plus a count.
- Timers use `tick - startTick` modulo the period, like paths. An effect is a rule index, the effect kind as a byte, an amount and an end tick; `expire` clears ended ones, and the helpers walk slots with `forEachBit` in slot order.
- For exact body shapes rather than points, use the physics module's queries in your game instead of triggers.

## Version history

- 0.2.0: timers, rule rows (`RuleWhen`, `Effects`, `runZoneRule`, `runTimerRule`, `product`/`sum`/`has`)
- 0.1.0: first version
