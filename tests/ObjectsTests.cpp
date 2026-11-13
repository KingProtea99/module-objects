// Objects module tests: trigger enter/exit in slot order, path positions as a pure function of the tick, timers, rule rows, item respawn
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

    // Timer: on 3 ticks of every 10, from tick 5
    objects::Timer t;
    t.period.value = 10;
    t.onTicks.value = 3;
    t.startTick.value = 5;
    CHECK(!objects::timerOn(t, 4) && objects::timerOn(t, 5) && objects::timerOn(t, 7) && !objects::timerOn(t, 8));
    CHECK(objects::timerOn(t, 15) && !objects::timerOn(t, 18));
    CHECK(objects::timerEdge(t, 5) == objects::TimerEdge::Started && objects::timerEdge(t, 6) == objects::TimerEdge::None);
    CHECK(objects::timerEdge(t, 8) == objects::TimerEdge::Ended && objects::timerEdge(t, 15) == objects::TimerEdge::Started);
    objects::Timer once;
    once.onTicks.value = 4; // period 0: on for ticks 0..3 only
    CHECK(objects::timerEdge(once, 0) == objects::TimerEdge::Started && objects::timerEdge(once, 4) == objects::TimerEdge::Ended);
    CHECK(!objects::timerOn(once, 40));

    // Rule rows: a game row type with the four fields
    {
        enum class Fx : mealkit::u8 { Speed, Push };
        struct Row {
            objects::RuleWhen when = objects::RuleWhen::Inside;
            Fx effect = Fx::Speed;
            mealkit::f32 amount = 0.5f;
            mealkit::Ticks duration{};
        };
        const auto speed = static_cast<mealkit::u8>(Fx::Speed);
        objects::Effects fx[3];
        objects::Trigger zone;
        const u32 zs[] = {0, 2};
        Vec3 zp[] = {{0, 0, 0}, {5, 0, 0}};

        // Inside: slot 0 slowed while inside, back to 1 after leaving; slot 2 never inside
        Row inside;
        objects::runZoneRule(inside, 0, objects::update(zone, zs, zp, 2), fx, 3, 100);
        CHECK(objects::product(fx[0], speed, 100) == 0.5f && objects::product(fx[2], speed, 100) == 1.0f);
        CHECK(objects::product(fx[0], speed, 100000) == 0.5f); // until it leaves
        zp[0] = {9, 0, 0};
        objects::runZoneRule(inside, 0, objects::update(zone, zs, zp, 2), fx, 3, 101);
        CHECK(objects::product(fx[0], speed, 101) == 1.0f && !objects::has(fx[0], speed, 101));

        // Enter with a duration: lasts 30 ticks after coming in, even after leaving; coming in again refreshes, doesn't stack
        Row enter;
        enter.when = objects::RuleWhen::Enter;
        enter.duration.value = 30;
        zp[0] = {0, 0, 0};
        objects::runZoneRule(enter, 1, objects::update(zone, zs, zp, 2), fx, 3, 200);
        zp[0] = {9, 0, 0};
        objects::runZoneRule(enter, 1, objects::update(zone, zs, zp, 2), fx, 3, 201);
        CHECK(objects::product(fx[0], speed, 229) == 0.5f && objects::product(fx[0], speed, 230) == 1.0f);
        zp[0] = {0, 0, 0};
        objects::runZoneRule(enter, 1, objects::update(zone, zs, zp, 2), fx, 3, 220);
        CHECK(objects::product(fx[0], speed, 240) == 0.5f && objects::product(fx[0], speed, 250) == 1.0f);
        objects::expire(fx[0], 250);
        CHECK(fx[0].list[0].rule == objects::kNoRule);

        // Exit: slot 0 leaves and gets the effect for 10 ticks
        Row exit = enter;
        exit.when = objects::RuleWhen::Exit;
        exit.duration.value = 10;
        zp[0] = {9, 0, 0};
        objects::runZoneRule(exit, 2, objects::update(zone, zs, zp, 2), fx, 3, 300);
        CHECK(objects::has(fx[0], speed, 305) && !objects::has(fx[0], speed, 310));

        // Two rules multiply; a fifth effect replaces the one ending first
        objects::Effects many;
        objects::start(many, 10, speed, 0.5f, 100);
        objects::start(many, 11, speed, 0.8f, 50);
        CHECK(objects::product(many, speed, 0) == 0.5f * 0.8f);
        CHECK(objects::sum(many, speed, 0) == 0.5f + 0.8f);
        objects::start(many, 12, 1, 2.0f, 200);
        objects::start(many, 13, 1, 2.0f, 300);
        objects::start(many, 14, 1, 3.0f, 400);
        CHECK(objects::product(many, speed, 0) == 0.5f); // rule 11 (ends at 50) went
        CHECK(objects::sum(many, 1, 0) == 7.0f);

        // Timer rows: everyone in the mask while the timer is on (duration 0), nothing for zone rows
        Row timed;
        timed.when = objects::RuleWhen::Timer;
        objects::Effects all[3];
        objects::runTimerRule(timed, 5, objects::TimerEdge::Started, 0b101, all, 3, 0);
        CHECK(objects::has(all[0], speed, 1) && !objects::has(all[1], speed, 1) && objects::has(all[2], speed, 1));
        objects::runTimerRule(timed, 5, objects::TimerEdge::Ended, 0b111, all, 3, 9);
        CHECK(!objects::has(all[0], speed, 9) && !objects::has(all[2], speed, 9));
        objects::runTimerRule(inside, 0, objects::TimerEdge::Started, 0b111, all, 3, 10);
        CHECK(!objects::has(all[1], speed, 10));

        // Effects are state fields: they go into the hash
        objects::Effects e1, e2;
        objects::start(e2, 0, speed, 0.5f, 10);
        mealkit::Hasher h1, h2;
        mealkit::reflect::hashStruct(mealkit::reflect::typeOf<objects::Effects>(), &e1, h1);
        mealkit::reflect::hashStruct(mealkit::reflect::typeOf<objects::Effects>(), &e2, h2);
        CHECK(h1.value != h2.value);
    }

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
