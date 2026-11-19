// Simulation checks that need no network: bots lap the track, cars never
// leave it, and the same inputs always give the same hashes.
#include "../src/common/sim.hpp"

#include <cstdio>
#include <map>

using namespace racer;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

static bool on_track(const Car& c) {
    const bool in_outer = c.x >= -Track::outer_x && c.x <= Track::outer_x &&
                          c.y >= -Track::outer_y && c.y <= Track::outer_y;
    const bool in_inner = c.x > -Track::inner_x && c.x < Track::inner_x &&
                          c.y > -Track::inner_y && c.y < Track::inner_y;
    return in_outer && !in_inner;
}

static uint64_t run(uint32_t ticks, World& w) {
    std::map<uint32_t, BotDriver> bots;
    for (const auto& c : w.cars()) bots.emplace(c.id, BotDriver(1000 + c.id));
    uint64_t h = 0;
    for (uint32_t t = 0; t < ticks; ++t) {
        std::map<uint32_t, Input> in;
        for (const auto& c : w.cars()) in[c.id] = bots.at(c.id).next(c);
        w.step(in);
        for (const auto& c : w.cars())
            CHECK(on_track(c), "car %u off track at tick %u", c.id, w.tick());
        h = w.hash();
    }
    return h;
}

int main() {
    World a, b;
    for (uint32_t id = 1; id <= 4; ++id) { a.join(id); b.join(id); }
    for (const auto& c : a.cars()) CHECK(on_track(c), "car %u starts off track", c.id);

    const uint64_t ha = run(3000, a), hb = run(3000, b);
    CHECK(ha == hb, "same inputs gave different hashes");

    for (const auto& c : a.cars()) {
        std::printf("car %u: %u laps\n", c.id, c.lap);
        CHECK(c.lap >= 3, "car %u only did %u laps in 3000 ticks", c.id, c.lap);
    }

    // Driving straight backwards over the line must not count as a lap.
    World r;
    r.join(1);
    std::map<uint32_t, Input> back{{1, Input{0, 0}}};
    for (int i = 0; i < 8; ++i) { back[1].steer = 1; r.step(back); }  // turn round
    back[1] = Input{0, 255};
    for (int i = 0; i < 400; ++i) r.step(back);
    CHECK(r.cars()[0].lap == 0, "reverse driving counted %u laps", r.cars()[0].lap);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("PASS: sim_test\n");
    return 0;
}
