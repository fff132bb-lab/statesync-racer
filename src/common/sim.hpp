// Deterministic simulation shared by the server and the replay tool.
//
// All state is integer (positions in micrometres, velocities in um per tick),
// so the same inputs always produce bit-identical state on any machine.
// That is what makes per-tick hashes and replays meaningful.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace racer {

// One driver input for one car for one tick.
struct Input {
    int8_t steer = 0;      // -1 left, 0 straight, +1 right
    uint8_t throttle = 0;  // 0..255
};

struct Car {
    uint32_t id = 0;
    int64_t x = 0, y = 0;    // um
    int64_t vx = 0, vy = 0;  // um per tick
    uint8_t heading = 0;     // 0..15, index into kDir
    uint16_t lap = 0;        // completed laps
    bool checkpoint = false; // passed the far side of the track this lap
};

// The track: a rectangular ring. Cars stay inside the outer wall and outside
// the inner block, and race counter-clockwise. The start/finish line is x = 0
// on the bottom straight; the checkpoint is x = 0 on the top straight, so a
// lap only counts after going all the way round.
struct Track {
    static constexpr int64_t kM = 1'000'000;  // um per metre
    static constexpr int64_t outer_x = 100 * kM, outer_y = 60 * kM;
    static constexpr int64_t inner_x = 70 * kM, inner_y = 30 * kM;
};

// Grid slot for a car: two lanes behind the start line on the bottom straight.
inline void place_on_grid(Car& c) {
    const uint32_t slot = (c.id - 1) % 16;
    c.x = -(5 + 6 * int64_t(slot / 2)) * Track::kM;
    c.y = (slot % 2 ? -50 : -40) * Track::kM;
    c.vx = c.vy = 0;
    c.heading = 0;  // facing +x, the racing direction on the bottom straight
}

// Unit direction vectors scaled by 1000, 16 headings (22.5 degrees apart).
inline constexpr int32_t kDir[16][2] = {
    {1000, 0},     {924, 383},    {707, 707},    {383, 924},
    {0, 1000},     {-383, 924},   {-707, 707},   {-924, 383},
    {-1000, 0},    {-924, -383},  {-707, -707},  {-383, -924},
    {0, -1000},    {383, -924},   {707, -707},   {924, -383},
};

class World {
public:
    uint32_t tick() const { return tick_; }
    const std::vector<Car>& cars() const { return cars_; }

    void join(uint32_t id) {
        Car c;
        c.id = id;
        place_on_grid(c);
        cars_.push_back(c);
    }

    const Car* find(uint32_t id) const {
        for (const auto& c : cars_) if (c.id == id) return &c;
        return nullptr;
    }

    void leave(uint32_t id) {
        cars_.erase(std::remove_if(cars_.begin(), cars_.end(),
                                   [id](const Car& c) { return c.id == id; }),
                    cars_.end());
    }

    // Advances one tick. Cars without an entry in `inputs` coast.
    void step(const std::map<uint32_t, Input>& inputs) {
        for (auto& c : cars_) {
            Input in;
            if (auto it = inputs.find(c.id); it != inputs.end()) in = it->second;
            c.heading = static_cast<uint8_t>((c.heading + 16 + in.steer) % 16);
            // Acceleration up to ~32 mm/tick^2 along the heading.
            c.vx += int64_t{kDir[c.heading][0]} * in.throttle / 8;
            c.vy += int64_t{kDir[c.heading][1]} * in.throttle / 8;
            // Drag: lose 1/32 of velocity each tick.
            c.vx -= c.vx / 32;
            c.vy -= c.vy / 32;
            const int64_t old_x = c.x;
            c.x += c.vx;
            c.y += c.vy;
            collide(c);
            count_laps(c, old_x);
        }
        ++tick_;
    }

    // Highest lap count among cars, and the lowest id holding it (0 if none).
    uint32_t leader(uint16_t* laps = nullptr) const {
        uint32_t id = 0;
        uint16_t best = 0;
        for (const auto& c : cars_)
            if (c.lap > best || (c.lap == best && best > 0 && c.id < id)) { best = c.lap; id = c.id; }
        if (laps) *laps = best;
        return id;
    }

    // FNV-1a 64 over the tick number and every car's full state, in a fixed
    // byte order, so the hash is stable across compilers and platforms.
    uint64_t hash() const {
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&h](uint64_t v, int bytes) {
            for (int i = 0; i < bytes; ++i) {
                h ^= (v >> (8 * i)) & 0xff;
                h *= 1099511628211ULL;
            }
        };
        mix(tick_, 4);
        mix(cars_.size(), 4);
        for (const auto& c : cars_) {
            mix(c.id, 4);
            mix(static_cast<uint64_t>(c.x), 8);
            mix(static_cast<uint64_t>(c.y), 8);
            mix(static_cast<uint64_t>(c.vx), 8);
            mix(static_cast<uint64_t>(c.vy), 8);
            mix(c.heading, 1);
            mix(c.lap, 2);
            mix(c.checkpoint, 1);
        }
        return h;
    }

private:
    // Walls stop the car on the wall and bounce half its speed back.
    static void collide(Car& c) {
        if (c.x > Track::outer_x) { c.x = Track::outer_x; c.vx = -c.vx / 2; }
        if (c.x < -Track::outer_x) { c.x = -Track::outer_x; c.vx = -c.vx / 2; }
        if (c.y > Track::outer_y) { c.y = Track::outer_y; c.vy = -c.vy / 2; }
        if (c.y < -Track::outer_y) { c.y = -Track::outer_y; c.vy = -c.vy / 2; }
        if (c.x > -Track::inner_x && c.x < Track::inner_x &&
            c.y > -Track::inner_y && c.y < Track::inner_y) {
            // Push out through the nearest face of the inner block.
            const int64_t dl = c.x + Track::inner_x, dr = Track::inner_x - c.x;
            const int64_t db = c.y + Track::inner_y, dt = Track::inner_y - c.y;
            const int64_t m = std::min(std::min(dl, dr), std::min(db, dt));
            if (m == dl) { c.x = -Track::inner_x; c.vx = -c.vx / 2; }
            else if (m == dr) { c.x = Track::inner_x; c.vx = -c.vx / 2; }
            else if (m == db) { c.y = -Track::inner_y; c.vy = -c.vy / 2; }
            else { c.y = Track::inner_y; c.vy = -c.vy / 2; }
        }
    }

    static void count_laps(Car& c, int64_t old_x) {
        if (c.y > 0 && old_x >= 0 && c.x < 0) c.checkpoint = true;  // top, heading -x
        if (c.y < 0 && old_x < 0 && c.x >= 0 && c.checkpoint) {      // bottom, heading +x
            ++c.lap;
            c.checkpoint = false;
        }
    }

    uint32_t tick_ = 0;
    std::vector<Car> cars_;
};

// Seeded bot driver for cars whose client sends no input. It steers toward
// the racing direction for the part of the track it is on, with some random
// throttle so bots spread out. Only the server uses it; replays read the
// logged inputs, so changing it never breaks old race logs.
class BotDriver {
public:
    explicit BotDriver(uint64_t seed) : state_(seed ? seed : 0x9e3779b97f4a7c15ULL) {}

    Input next(const Car& c) {
        const int64_t m = Track::kM;
        int target;
        if (c.y < -Track::inner_y && c.x < 85 * m) target = 0;       // bottom: +x
        else if (c.x > Track::inner_x && c.y < 45 * m) target = 4;   // right: +y
        else if (c.y > Track::inner_y && c.x > -85 * m) target = 8;  // top: -x
        else if (c.x < -Track::inner_x && c.y > -45 * m) target = 12; // left: -y
        else target = 0;
        const int diff = (target - c.heading + 16) % 16;
        Input in;
        in.steer = static_cast<int8_t>(diff == 0 ? 0 : diff < 8 ? 1 : -1);
        const uint64_t r = rand();
        in.throttle = static_cast<uint8_t>(diff == 0 ? 180 + r % 70 : 90 + r % 40);
        return in;
    }

private:
    uint64_t rand() {  // xorshift64
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }
    uint64_t state_;
};

} // namespace racer
