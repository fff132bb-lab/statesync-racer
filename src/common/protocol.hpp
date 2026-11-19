// Wire protocol shared by server and client.
//
// One snapshot per server tick, sent as a single text line:
//   STATE <tick> <hash> <count> [<id> <x> <y> <vx> <vy>]...\n
// <hash> is the server's 16-hex-digit state hash for that tick, so clients
// (and tests) can tell whether they see exactly the server's state.
// Text keeps it easy to debug with `nc`; it can move to a binary
// format later without changing the simulation code.
#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace racer {

struct CarState {
    uint32_t id = 0;
    float x = 0.f, y = 0.f;   // position (m)
    float vx = 0.f, vy = 0.f; // velocity (m/s)
};

struct Snapshot {
    uint32_t tick = 0;
    uint64_t hash = 0;
    std::vector<CarState> cars;
};

inline std::string encode(const Snapshot& s) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "STATE %u %016" PRIx64 " %zu", s.tick, s.hash,
                  s.cars.size());
    std::string out = buf;
    for (const auto& c : s.cars) {
        std::snprintf(buf, sizeof buf, " %u %.3f %.3f %.3f %.3f", c.id, c.x,
                      c.y, c.vx, c.vy);
        out += buf;
    }
    out += '\n';
    return out;
}

// Parses one line (without the trailing newline). Returns false on bad input.
inline bool decode(const std::string& line, Snapshot& s) {
    std::istringstream in(line);
    std::string tag, hex;
    size_t count = 0;
    if (!(in >> tag >> s.tick >> hex >> count) || tag != "STATE") return false;
    s.hash = std::stoull(hex, nullptr, 16);
    s.cars.assign(count, {});
    for (auto& c : s.cars)
        if (!(in >> c.id >> c.x >> c.y >> c.vx >> c.vy)) return false;
    return true;
}

} // namespace racer
