#pragma once

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"
#include "mealkit/sim/TickTimer.h"

// Basic objects module (docs/index.md): trigger volumes, path platforms, item spawn points.
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
