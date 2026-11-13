#pragma once

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"
#include "mealkit/sim/TickTimer.h"

// Basic objects module (docs/index.md): trigger volumes, path platforms, timers, rule rows with active effects, item spawn points.
// Functions take only their own state and what the game passes (points, tick numbers); they don't know the game's World or physics.
// Results depend only on the inputs: no sqrt, no per-tick accumulation, events in index order.
namespace objects {

using mealkit::f32;
using mealkit::u32;
using mealkit::u64;
using mealkit::Vec3;

// ---------------------------------------------------------------- Trigger volumes

MEALKIT_ENUM(Shape, mealkit::u8, Box, Sphere);

inline constexpr u32 kMaxTriggerSlots = 64; // the game's slot indices (player slots, pawn pool slots) must be below this

MEALKIT_STRUCT(Trigger) {
    MEALKIT_FIELD(Shape, shape, Shape::Box, "Shape");
    MEALKIT_FIELD(Vec3, center, {}, "Center");
    MEALKIT_FIELD(Vec3, halfSize, (Vec3{1, 1, 1}), "Half size (box) / radius in x (sphere)");
    MEALKIT_FIELD(u64, inside, 0, "Slots inside (bit mask)");
};

inline bool contains(const Trigger& t, Vec3 p) {
    const Vec3 d = p - t.center;
    if (t.shape == Shape::Sphere) return mealkit::lengthSq(d) <= t.halfSize.x * t.halfSize.x;
    const auto in = [](f32 v, f32 h) { return v >= -h && v <= h; };
    return in(d.x, t.halfSize.x) && in(d.y, t.halfSize.y) && in(d.z, t.halfSize.z);
}

struct TriggerChange {
    u64 entered = 0; // bits of slots that came in this update
    u64 exited = 0;  // bits of slots that left (including slots no longer passed in)
};

// One update: points[i] is the position of slot slots[i] (any order). Slots not passed in count as gone.
// Walk the result bits from low to high (forEachBit) so events come out in slot order.
inline TriggerChange update(Trigger& t, const u32* slots, const Vec3* points, u32 count) {
    u64 now = 0;
    for (u32 i = 0; i < count; ++i)
        if (slots[i] < kMaxTriggerSlots && contains(t, points[i])) now |= u64{1} << slots[i];
    TriggerChange c{now & ~t.inside, t.inside & ~now};
    t.inside = now;
    return c;
}

inline bool isInside(const Trigger& t, u32 slot) { return slot < kMaxTriggerSlots && ((t.inside >> slot) & 1u); }

template <class F>
void forEachBit(u64 bits, F&& f) {
    for (u32 i = 0; i < kMaxTriggerSlots; ++i)
        if ((bits >> i) & 1u) f(i);
}

// ---------------------------------------------------------------- Path platforms

inline constexpr u32 kMaxPathPoints = 8;

MEALKIT_ENUM(PathMode, mealkit::u8, Loop, PingPong, Once);

// Waypoints with a whole-tick length for each leg (legTicks[i] = ticks from point i to point i+1; in Loop the last leg returns to point 0).
// Static data, usually read from a scene object at match start. The position is a pure function of the tick.
MEALKIT_STRUCT(Path) {
    MEALKIT_FIELD(PathMode, mode, PathMode::PingPong, "Mode");
    MEALKIT_FIELD(u32, startTick, 0, "Start tick");
    MEALKIT_FIELD(u32, pointCount, 0, "Point count");
    MEALKIT_ARRAY(Vec3, kMaxPathPoints, points, "Points");
    MEALKIT_ARRAY(u32, kMaxPathPoints, legTicks, "Leg lengths (ticks)");
};

inline u32 legCount(const Path& p) {
    if (p.pointCount < 2) return 0;
    return p.mode == PathMode::Loop ? p.pointCount : p.pointCount - 1;
}

inline u32 forwardTicks(const Path& p) {
    u32 total = 0;
    for (u32 i = 0; i < legCount(p); ++i) total += p.legTicks[i];
    return total;
}

// Position at a tick. Before startTick it stays at point 0; Once stops at the last point.
inline Vec3 positionAt(const Path& p, u32 tick) {
    if (p.pointCount == 0) return {};
    const u32 total = forwardTicks(p);
    if (total == 0 || tick <= p.startTick) return p.points[0];
    u32 t = tick - p.startTick;
    if (p.mode == PathMode::Once) {
        if (t >= total) return p.points[p.pointCount - 1];
    } else if (p.mode == PathMode::Loop) {
        t %= total;
    } else {
        t %= total * 2;
        if (t >= total) {
            t = total * 2 - t; // walk the forward path backwards: same positions, mirrored time
        }
    }
    for (u32 i = 0; i < legCount(p); ++i) {
        const u32 len = p.legTicks[i];
        if (t < len || (t == len && i + 1 == legCount(p))) {
            const Vec3 a = p.points[i];
            const Vec3 b = p.points[(i + 1) % p.pointCount];
            return len ? mealkit::lerp(a, b, static_cast<f32>(t) / static_cast<f32>(len)) : b;
        }
        t -= len;
    }
    return p.points[0];
}

// Movement over one tick (for a kinematic body's velocity: divide by the tick length in the game)
inline Vec3 stepDelta(const Path& p, u32 tick) { return positionAt(p, tick + 1) - positionAt(p, tick); }

// ---------------------------------------------------------------- Timers

// Repeating on/off timer: off until startTick, then on for onTicks at the start of every period (period 0 = on once, from startTick).
// Like Path, a pure function of the tick: nothing to step, so rewinds and late joins match.
MEALKIT_STRUCT(Timer) {
    MEALKIT_FIELD(mealkit::Ticks, period, {}, "Period (s, 0 = once)");
    MEALKIT_FIELD(mealkit::Ticks, onTicks, {}, "On for (s)");
    MEALKIT_FIELD(mealkit::Ticks, startTick, {}, "First start (s)");
};

inline bool timerOn(const Timer& t, u32 tick) {
    if (tick < t.startTick.value) return false;
    const u32 since = tick - t.startTick.value;
    const u32 at = t.period.value ? since % t.period.value : since;
    return at < t.onTicks.value;
}

MEALKIT_ENUM(TimerEdge, mealkit::u8, None, Started, Ended);

// Started on the first tick the timer is on, Ended on the first tick it is off again (before tick 0 counts as off)
inline TimerEdge timerEdge(const Timer& t, u32 tick) {
    const bool now = timerOn(t, tick);
    const bool before = tick > 0 && timerOn(t, tick - 1);
    if (now == before) return TimerEdge::None;
    return now ? TimerEdge::Started : TimerEdge::Ended;
}

// ---------------------------------------------------------------- Rule rows and active effects

// A rule row is the game's own MEALKIT_STRUCT in a table (docs/index.md "Rule rows") with these four fields:
//   objects::RuleWhen when; <the game's effect enum> effect; f32 amount; mealkit::Ticks duration;
// Scene objects (a zone with a Trigger, a timer) pick a row with RowRef. The game declares the effect enum and applies each kind
// in sim/, reading active effects with product/sum/has. A duration of 0 means "while it lasts": while inside, or while the timer is on.
MEALKIT_ENUM(RuleWhen, mealkit::u8, Enter, Inside, Exit, Timer);

inline constexpr u32 kMaxEffects = 4;             // active effects per slot; when full, a new one replaces the one ending first
inline constexpr u32 kNoRule = 0xFFFFFFFFu;
inline constexpr u32 kUntilStopped = 0xFFFFFFFFu; // until stop() (inside a zone, a timer row without a duration)

MEALKIT_STRUCT(Effect) {
    MEALKIT_FIELD(u32, rule, kNoRule, "Rule row (index)");
    MEALKIT_FIELD(mealkit::u8, kind, 0, "Effect (game enum value)");
    MEALKIT_FIELD(f32, amount, 1.0f, "Amount");
    MEALKIT_FIELD(u32, until, 0, "Ends at tick (exclusive)");
};

MEALKIT_STRUCT(Effects) {
    MEALKIT_ARRAY(Effect, kMaxEffects, list, "Active effects");
};

inline bool active(const Effect& e, u32 tick) { return e.rule != kNoRule && tick < e.until; }

// Starts (or refreshes) a rule's effect. The same rule doesn't stack: it takes the new amount and end.
inline void start(Effects& fx, u32 rule, mealkit::u8 kind, f32 amount, u32 until) {
    Effect* slot = nullptr;
    for (Effect& e : fx.list)
        if (!slot && e.rule == rule) slot = &e;
    for (Effect& e : fx.list)
        if (!slot && e.rule == kNoRule) slot = &e;
    if (!slot) { // full: replace the one ending first (lowest index on ties)
        slot = &fx.list[0];
        for (Effect& e : fx.list)
            if (e.until < slot->until) slot = &e;
    }
    *slot = {};
    slot->rule = rule;
    slot->kind = kind;
    slot->amount = amount;
    slot->until = until;
}

inline void stop(Effects& fx, u32 rule) {
    for (Effect& e : fx.list)
        if (e.rule == rule) e = {};
}

// Clears effects that ended (call once per tick at a fixed point in World::step)
inline void expire(Effects& fx, u32 tick) {
    for (Effect& e : fx.list)
        if (e.rule != kNoRule && tick >= e.until) e = {};
}

inline void clear(Effects& fx) { fx = {}; }

// Active effects of one kind multiplied (speed x0.5 and x0.8 -> x0.4), or added; 1 and 0 when there are none
inline f32 product(const Effects& fx, mealkit::u8 kind, u32 tick) {
    f32 v = 1.0f;
    for (const Effect& e : fx.list)
        if (e.kind == kind && active(e, tick)) v *= e.amount;
    return v;
}

inline f32 sum(const Effects& fx, mealkit::u8 kind, u32 tick) {
    f32 v = 0.0f;
    for (const Effect& e : fx.list)
        if (e.kind == kind && active(e, tick)) v += e.amount;
    return v;
}

inline bool has(const Effects& fx, mealkit::u8 kind, u32 tick) {
    for (const Effect& e : fx.list)
        if (e.kind == kind && active(e, tick)) return true;
    return false;
}

namespace detail {
template <class Row>
u32 endOf(const Row& row, u32 tick) {
    return row.duration.value ? tick + row.duration.value : kUntilStopped;
}

template <class F>
void forSlots(u64 bits, u32 slotCount, F&& f) {
    forEachBit(bits, [&](u32 s) {
        if (s < slotCount) f(s);
    });
}
} // namespace detail

// Runs a zone's rule row after one trigger update (call right after update(trigger, ...)). perSlot[s] is slot s's effects.
// Enter: slots that came in get the effect for duration (0 = until they leave). Inside: slots that came in get it until they leave.
// Exit: slots that left get it for duration. Timer rows do nothing here (runTimerRule).
template <class Row>
void runZoneRule(const Row& row, u32 rule, const TriggerChange& change, Effects* perSlot, u32 slotCount, u32 tick) {
    const auto kind = static_cast<mealkit::u8>(row.effect);
    const u32 duration = row.duration.value;
    switch (row.when) {
    case RuleWhen::Enter:
    case RuleWhen::Inside: {
        const u32 until = row.when == RuleWhen::Inside ? kUntilStopped : detail::endOf(row, tick);
        detail::forSlots(change.entered, slotCount, [&](u32 s) { start(perSlot[s], rule, kind, row.amount, until); });
        if (until == kUntilStopped) detail::forSlots(change.exited, slotCount, [&](u32 s) { stop(perSlot[s], rule); });
        break;
    }
    case RuleWhen::Exit:
        if (duration)
            detail::forSlots(change.exited, slotCount, [&](u32 s) { start(perSlot[s], rule, kind, row.amount, tick + duration); });
        break;
    case RuleWhen::Timer: break;
    }
}

// Runs a timer's rule row on the slots whose bits are set: when the timer starts they get the effect for duration
// (0 = until the timer ends). Other rows do nothing here.
template <class Row>
void runTimerRule(const Row& row, u32 rule, TimerEdge edge, u64 slots, Effects* perSlot, u32 slotCount, u32 tick) {
    if (row.when != RuleWhen::Timer || edge == TimerEdge::None) return;
    const auto kind = static_cast<mealkit::u8>(row.effect);
    detail::forSlots(slots, slotCount, [&](u32 s) {
        if (edge == TimerEdge::Started) start(perSlot[s], rule, kind, row.amount, detail::endOf(row, tick));
        else if (!row.duration.value) stop(perSlot[s], rule);
    });
}

// ---------------------------------------------------------------- Item spawn points

MEALKIT_STRUCT(ItemSpawn) {
    MEALKIT_FIELD(mealkit::sim::TickTimer, respawn, {}, "Available again at");
    MEALKIT_FIELD(u32, taken, 0, "Times taken");
};

inline bool available(const ItemSpawn& s, u32 tick) { return s.respawn.ready(tick); }

// If the item is there, takes it (starts the respawn wait) and returns true
inline bool tryTake(ItemSpawn& s, u32 tick, u32 respawnTicks) {
    if (!available(s, tick)) return false;
    s.respawn.start(tick, respawnTicks);
    ++s.taken;
    return true;
}

inline void reset(ItemSpawn& s) { s = {}; }

} // namespace objects
