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
};

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
        c.heading = static_cast<uint8_t>(id % 16);
        cars_.push_back(c);
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
            c.x += c.vx;
            c.y += c.vy;
        }
        ++tick_;
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
        }
        return h;
    }

private:
    uint32_t tick_ = 0;
    std::vector<Car> cars_;
};

// Seeded bot driver so the server has something to simulate before real
// client input exists. Only the server uses it; replays read logged inputs.
class BotDriver {
public:
    explicit BotDriver(uint64_t seed) : state_(seed ? seed : 0x9e3779b97f4a7c15ULL) {}

    Input next() {
        uint64_t r = rand();
        Input in;
        in.steer = static_cast<int8_t>(static_cast<int>(r % 5 == 0) - static_cast<int>(r % 7 == 0));
        in.throttle = static_cast<uint8_t>(128 + (r >> 8) % 128);
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
