// Minimal client: connects to the server and prints every snapshot it gets.
//
// Usage: racer_client [host] [port] [max_ticks]
//   max_ticks = 0 (default) means run until the server closes.

#include "../common/protocol.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    const char* port = argc > 2 ? argv[2] : "7777";
    const long max_ticks = argc > 3 ? std::atol(argv[3]) : 0;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, port, &hints, &res) != 0) {
        std::fprintf(stderr, "cannot resolve %s:%s\n", host, port);
        return 1;
    }
    int fd = -1;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd >= 0 && ::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) {
        std::fprintf(stderr, "cannot connect to %s:%s\n", host, port);
        return 1;
    }
    std::printf("connected to %s:%s\n", host, port);

    std::string pending;
    char buf[4096];
    long received = 0;
    while (max_ticks == 0 || received < max_ticks) {
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        pending.append(buf, static_cast<size_t>(n));
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            racer::Snapshot s;
            if (!racer::decode(line, s)) {
                std::fprintf(stderr, "bad snapshot: %s\n", line.c_str());
                continue;
            }
            std::printf("tick %u: %zu car(s)", s.tick, s.cars.size());
            for (const auto& c : s.cars)
                std::printf("  [car %u pos=(%.2f, %.2f) vel=(%.2f, %.2f)]", c.id,
                            c.x, c.y, c.vx, c.vy);
            std::printf("\n");
            std::fflush(stdout);
            if (max_ticks != 0 && ++received >= max_ticks) break;
        }
    }
    ::close(fd);
    return 0;
}
