// Race log: everything needed to replay a race bit-for-bit.
//
// Text format, one record per line, in the order the server applied them:
//   RACELOG 1 <seed>             header
//   J <tick> <id>                car joined before simulating <tick>
//   L <tick> <id>                car left before simulating <tick>
//   I <tick> <id> <steer> <thr>  input applied while simulating <tick>
//   H <tick> <hash>              state hash after simulating <tick>
#pragma once

#include "sim.hpp"

#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace racer {

class RaceLogWriter {
public:
    bool open(const std::string& path, uint64_t seed) {
        out_.open(path);
        if (!out_) return false;
        out_ << "RACELOG 1 " << seed << '\n';
        return true;
    }
    bool is_open() const { return out_.is_open(); }
    void join(uint32_t tick, uint32_t id) { if (is_open()) out_ << "J " << tick << ' ' << id << '\n'; }
    void leave(uint32_t tick, uint32_t id) { if (is_open()) out_ << "L " << tick << ' ' << id << '\n'; }
    void input(uint32_t tick, uint32_t id, const Input& in) {
        if (is_open())
            out_ << "I " << tick << ' ' << id << ' ' << int(in.steer) << ' ' << int(in.throttle) << '\n';
    }
    void hash(uint32_t tick, uint64_t h) {
        if (!is_open()) return;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%016" PRIx64, h);
        out_ << "H " << tick << ' ' << buf << '\n';
        out_.flush();
    }

private:
    std::ofstream out_;
};

struct ReplayResult {
    bool ok = false;
    uint32_t ticks_checked = 0;
    uint32_t first_bad_tick = 0;
    uint64_t expected = 0, actual = 0;
    std::string error;
};

// Re-simulates a race log and checks every recorded hash.
inline ReplayResult replay(std::istream& in) {
    ReplayResult r;
    std::string line, tag;
    if (!std::getline(in, line)) { r.error = "empty log"; return r; }
    {
        std::istringstream h(line);
        int version = 0;
        uint64_t seed = 0;
        if (!(h >> tag >> version >> seed) || tag != "RACELOG" || version != 1) {
            r.error = "bad header";
            return r;
        }
    }
    World world;
    std::map<uint32_t, Input> inputs;
    size_t lineno = 1;
    while (std::getline(in, line)) {
        ++lineno;
        if (line.empty()) continue;
        std::istringstream s(line);
        uint32_t tick = 0;
        if (!(s >> tag >> tick)) { r.error = "bad line " + std::to_string(lineno); return r; }
        if (tick != world.tick()) { r.error = "out-of-order tick at line " + std::to_string(lineno); return r; }
        if (tag == "J" || tag == "L") {
            uint32_t id = 0;
            s >> id;
            if (tag == "J") world.join(id); else world.leave(id);
        } else if (tag == "I") {
            uint32_t id = 0;
            int steer = 0, thr = 0;
            s >> id >> steer >> thr;
            inputs[id] = Input{static_cast<int8_t>(steer), static_cast<uint8_t>(thr)};
        } else if (tag == "H") {
            std::string hex;
            s >> hex;
            uint64_t expected = std::stoull(hex, nullptr, 16);
            world.step(inputs);
            inputs.clear();
            uint64_t actual = world.hash();
            ++r.ticks_checked;
            if (actual != expected) {
                r.first_bad_tick = tick;
                r.expected = expected;
                r.actual = actual;
                return r;
            }
        } else {
            r.error = "unknown record at line " + std::to_string(lineno);
            return r;
        }
    }
    r.ok = true;
    return r;
}

} // namespace racer
