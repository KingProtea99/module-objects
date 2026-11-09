// Objects module tests: trigger enter/exit in slot order, path positions as a pure function of the tick, item respawn
#include "objects/sim/Objects.h"

#include "mealkit/core/Hash.h"

#include <cstdio>

static int g_failed = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            std::printf("%s(%d): FAIL %s\n", __FILE__, __LINE__, #x);                                                  \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using mealkit::Vec3;
using mealkit::u32;

static bool near(Vec3 a, Vec3 b) { return mealkit::lengthSq(a - b) < 1e-8f; }

int main() {
    // Trigger: box at origin, half size 1
    objects::Trigger box;
    const u32 slots[] = {5, 2, 40};
    Vec3 pts[] = {{0, 0, 0}, {3, 0, 0}, {0.5f, 1.0f, -1.0f}}; // edges count as inside
    objects::TriggerChange c = objects::update(box, slots, pts, 3);
    CHECK(c.entered == ((1ull << 5) | (1ull << 40)) && c.exited == 0);
    u32 order[3]{}, n = 0;
    objects::forEachBit(c.entered, [&](u32 s) { order[n++] = s; });
    CHECK(n == 2 && order[0] == 5 && order[1] == 40);
    pts[1] = {0, 0, 0};
    c = objects::update(box, slots, pts, 2); // slot 40 not passed in: gone
    CHECK(c.entered == (1ull << 2) && c.exited == (1ull << 40));
    CHECK(objects::isInside(box, 2) && !objects::isInside(box, 40) && !objects::isInside(box, 99));
    c = objects::update(box, slots, pts, 2);
    CHECK(c.entered == 0 && c.exited == 0);

    objects::Trigger ball;
    ball.shape = objects::Shape::Sphere;
    ball.halfSize = {2, 0, 0};
    CHECK(objects::contains(ball, {0, 2, 0}) && !objects::contains(ball, {1.5f, 1.5f, 0}));

    // Path: ping-pong between two points, 10 ticks a leg, starting at tick 100
    objects::Path p;
    p.mode = objects::PathMode::PingPong;
    p.startTick = 100;
    p.pointCount = 2;
    p.points[0] = {0, 0, 0};
    p.points[1] = {10, 0, 0};
    p.legTicks[0] = 10;
    CHECK(near(objects::positionAt(p, 50), {0, 0, 0}));
    CHECK(near(objects::positionAt(p, 105), {5, 0, 0}));
    CHECK(near(objects::positionAt(p, 110), {10, 0, 0}));
    CHECK(near(objects::positionAt(p, 113), {7, 0, 0}));
    CHECK(near(objects::positionAt(p, 120), {0, 0, 0}));
    CHECK(near(objects::positionAt(p, 100 + 20 * 1000 + 4), {4, 0, 0})); // no drift over time
    CHECK(near(objects::stepDelta(p, 101), {1, 0, 0}));

    // Loop over three points returns to the first
    p.mode = objects::PathMode::Loop;
    p.pointCount = 3;
    p.points[2] = {10, 0, 10};
    p.legTicks[1] = 5;
    p.legTicks[2] = 5;
    CHECK(near(objects::positionAt(p, 112), {10, 0, 4}));
    CHECK(near(objects::positionAt(p, 117), {6, 0, 6}));
    CHECK(near(objects::positionAt(p, 120), {0, 0, 0}));

    p.mode = objects::PathMode::Once;
    CHECK(near(objects::positionAt(p, 500), {10, 0, 10}));

    objects::Path empty;
    CHECK(near(objects::positionAt(empty, 7), {0, 0, 0}));

    // Item spawn: taken at 10, back at 70
    objects::ItemSpawn s;
    CHECK(objects::tryTake(s, 10, 60));
    CHECK(!objects::available(s, 69) && !objects::tryTake(s, 69, 60));
    CHECK(objects::tryTake(s, 70, 60) && s.taken == 2);

    // State is declared as fields and goes into the hash
    objects::Trigger a, b;
    b.inside = 1;
    mealkit::Hasher ha, hb;
    mealkit::reflect::hashStruct(mealkit::reflect::typeOf<objects::Trigger>(), &a, ha);
    mealkit::reflect::hashStruct(mealkit::reflect::typeOf<objects::Trigger>(), &b, hb);
    CHECK(ha.value != hb.value);

    std::printf(g_failed ? "objects tests: %d failed\n" : "objects tests: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
