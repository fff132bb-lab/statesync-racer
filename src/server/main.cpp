// Authoritative state-sync server.
//
// Listens on a TCP port, gives every connected client a car, steps the
// deterministic simulation at a fixed tick rate and broadcasts the full
// snapshot (with its state hash) to all clients each tick. Cars are driven by
// seeded bots until client input exists.
//
// Usage: racer_server [port] [tick_hz] [--seed N] [--record FILE] [--max-ticks N]
//   --record FILE   write a race log that racer_replay can verify
//   --max-ticks N   stop after N ticks (0 = run until Ctrl+C)

#include "../common/protocol.hpp"
#include "../common/racelog.hpp"
#include "../common/sim.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <map>
#include <string>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

volatile std::sig_atomic_t g_running = 1;
void on_signal(int) { g_running = 0; }

struct Client {
    int fd;
    uint32_t id;
};

int make_listener(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 ||
        ::listen(fd, 16) < 0) {
        ::close(fd);
        return -1;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

// Sends the whole buffer or reports failure (client is then dropped).
bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    uint16_t port = 7777;
    int tick_hz = 20;
    uint64_t seed = 1;
    std::string record_path;
    uint32_t max_ticks = 0;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--record" && i + 1 < argc) record_path = argv[++i];
        else if (arg == "--max-ticks" && i + 1 < argc) max_ticks = static_cast<uint32_t>(std::atol(argv[++i]));
        else if (positional == 0) { port = static_cast<uint16_t>(std::atoi(argv[i])); ++positional; }
        else if (positional == 1) { tick_hz = std::atoi(argv[i]); ++positional; }
        else { std::fprintf(stderr, "unknown argument: %s\n", argv[i]); return 1; }
    }
    if (tick_hz <= 0) {
        std::fprintf(stderr, "tick_hz must be positive\n");
        return 1;
    }

    racer::RaceLogWriter log;
    if (!record_path.empty() && !log.open(record_path, seed)) {
        std::fprintf(stderr, "cannot open %s\n", record_path.c_str());
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    int listener = make_listener(port);
    if (listener < 0) {
        std::perror("listen");
        return 1;
    }
    std::printf("racer_server listening on :%u at %d Hz\n", port, tick_hz);
    std::fflush(stdout);

    using clock = std::chrono::steady_clock;
    const auto tick_len = std::chrono::nanoseconds(1'000'000'000LL / tick_hz);

    racer::World world;
    std::map<uint32_t, racer::BotDriver> bots;
    std::vector<Client> clients;
    uint32_t next_id = 1;
    auto next_tick = clock::now();

    while (g_running) {
        // Accept any pending connections.
        for (;;) {
            int fd = ::accept(listener, nullptr, nullptr);
            if (fd < 0) break;
            Client c{fd, next_id++};
            clients.push_back(c);
            world.join(c.id);
            log.join(world.tick(), c.id);
            bots.emplace(c.id, racer::BotDriver(seed * 1000003ULL + c.id));
            std::printf("client %u connected (%zu online)\n", c.id, clients.size());
            std::fflush(stdout);
        }

        // Drop clients that closed their side.
        for (auto it = clients.begin(); it != clients.end();) {
            pollfd p{it->fd, POLLIN, 0};
            char buf[256];
            bool gone = false;
            if (::poll(&p, 1, 0) > 0) {
                ssize_t n = ::recv(it->fd, buf, sizeof buf, MSG_DONTWAIT);
                gone = n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
            }
            if (gone) {
                std::printf("client %u disconnected\n", it->id);
                world.leave(it->id);
                log.leave(world.tick(), it->id);
                bots.erase(it->id);
                ::close(it->fd);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        // Simulate and broadcast one tick.
        const uint32_t tick = world.tick();
        std::map<uint32_t, racer::Input> inputs;
        for (auto& [id, bot] : bots) {
            inputs[id] = bot.next();
            log.input(tick, id, inputs[id]);
        }
        world.step(inputs);
        racer::Snapshot snap;
        snap.tick = tick;
        snap.hash = world.hash();
        log.hash(tick, snap.hash);
        for (const auto& c : world.cars()) {
            racer::CarState cs;
            cs.id = c.id;
            cs.x = static_cast<float>(c.x) / 1e6f;
            cs.y = static_cast<float>(c.y) / 1e6f;
            cs.vx = static_cast<float>(c.vx) * static_cast<float>(tick_hz) / 1e6f;
            cs.vy = static_cast<float>(c.vy) * static_cast<float>(tick_hz) / 1e6f;
            snap.cars.push_back(cs);
        }
        const std::string wire = racer::encode(snap);
        for (auto it = clients.begin(); it != clients.end();) {
            if (send_all(it->fd, wire)) {
                ++it;
            } else {
                std::printf("client %u dropped (send failed)\n", it->id);
                world.leave(it->id);
                log.leave(world.tick(), it->id);
                bots.erase(it->id);
                ::close(it->fd);
                it = clients.erase(it);
            }
        }
        std::fflush(stdout);

        if (max_ticks != 0 && world.tick() >= max_ticks) break;
        next_tick += tick_len;
        std::this_thread::sleep_until(next_tick);
    }

    for (auto& c : clients) ::close(c.fd);
    ::close(listener);
    std::printf("racer_server stopped after %u ticks\n", world.tick());
    return 0;
}
