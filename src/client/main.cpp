// Minimal client: connects to the server and prints every snapshot it gets.
//
// Usage: racer_client [host] [port] [max_ticks] [--steer S] [--throttle T]
//   max_ticks = 0 (default) means run until the server closes.
//   --steer / --throttle drive the car: the client sends that input after
//   every snapshot. Without them the server's bot drives the car.

#include "../common/protocol.hpp"

#include <arpa/inet.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    const char* port = "7777";
    long max_ticks = 0;
    int steer = 0, throttle = 0;
    bool drive = false;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--steer" && i + 1 < argc) { steer = std::atoi(argv[++i]); drive = true; }
        else if (arg == "--throttle" && i + 1 < argc) { throttle = std::atoi(argv[++i]); drive = true; }
        else if (positional == 0) { host = argv[i]; ++positional; }
        else if (positional == 1) { port = argv[i]; ++positional; }
        else if (positional == 2) { max_ticks = std::atol(argv[i]); ++positional; }
        else { std::fprintf(stderr, "unknown argument: %s\n", argv[i]); return 1; }
    }
    int check_s = 0, check_t = 0;
    if (drive && !racer::decode_input(racer::encode_input(steer, throttle), check_s, check_t)) {
        std::fprintf(stderr, "steer must be -1..1 and throttle 0..255\n");
        return 1;
    }
    const std::string input_line = racer::encode_input(steer, throttle);

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
            uint32_t my_id = 0;
            if (racer::decode_welcome(line, my_id)) {
                std::printf("you are car %u\n", my_id);
                continue;
            }
            racer::Snapshot s;
            if (!racer::decode(line, s)) {
                std::fprintf(stderr, "bad snapshot: %s\n", line.c_str());
                continue;
            }
            std::printf("tick %u hash %016" PRIx64 ": %zu car(s)", s.tick, s.hash, s.cars.size());
            for (const auto& c : s.cars)
                std::printf("  [car %u pos=(%.2f, %.2f) vel=(%.2f, %.2f)]", c.id,
                            c.x, c.y, c.vx, c.vy);
            std::printf("\n");
            std::fflush(stdout);
            if (drive && ::send(fd, input_line.data(), input_line.size(), MSG_NOSIGNAL) < 0) break;
            if (max_ticks != 0 && ++received >= max_ticks) break;
        }
    }
    ::close(fd);
    return 0;
}
