// Replays a race log recorded by `racer_server --record` and checks that the
// re-simulated state hash matches the recorded one on every tick.
//
// Usage: racer_replay <race.log>
// Exit code: 0 all hashes match, 1 desync found, 2 bad input.

#include "../common/racelog.hpp"

#include <cinttypes>
#include <cstdio>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: racer_replay <race.log>\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const racer::ReplayResult r = racer::replay(in);
    if (!r.error.empty()) {
        std::fprintf(stderr, "invalid log: %s\n", r.error.c_str());
        return 2;
    }
    if (!r.ok) {
        std::printf("DESYNC at tick %u: expected %016" PRIx64 ", got %016" PRIx64
                    " (%u ticks checked)\n",
                    r.first_bad_tick, r.expected, r.actual, r.ticks_checked);
        return 1;
    }
    std::printf("OK: %u ticks replayed, all hashes match\n", r.ticks_checked);
    return 0;
}
