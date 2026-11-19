// Authoritative state-sync server.
//
// Listens on a TCP port, gives every connected client a car, steps the
// simulation at a fixed tick rate and broadcasts the full snapshot to all
// clients each tick.
//
// Usage: racer_server [port] [tick_hz]

#include "../common/protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
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
    racer::CarState car;
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

// Every car drives its own circle around the origin.
void step(racer::CarState& c, uint32_t tick, float dt) {
    const float radius = 20.f + 5.f * static_cast<float>(c.id);
    const float omega = 0.5f;  // rad/s
    const float phase = 0.7f * static_cast<float>(c.id);
    const float t = static_cast<float>(tick) * dt;
    const float a = omega * t + phase;
    c.x = radius * std::cos(a);
    c.y = radius * std::sin(a);
    c.vx = -radius * omega * std::sin(a);
    c.vy = radius * omega * std::cos(a);
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
    const uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 7777;
    const int tick_hz = argc > 2 ? std::atoi(argv[2]) : 20;
    if (tick_hz <= 0) {
        std::fprintf(stderr, "tick_hz must be positive\n");
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
    const float dt = 1.f / static_cast<float>(tick_hz);

    std::vector<Client> clients;
    uint32_t next_id = 1;
    uint32_t tick = 0;
    auto next_tick = clock::now();

    while (g_running) {
        // Accept any pending connections.
        for (;;) {
            int fd = ::accept(listener, nullptr, nullptr);
            if (fd < 0) break;
            Client c{fd, {}};
            c.car.id = next_id++;
            clients.push_back(c);
            std::printf("client %u connected (%zu online)\n", c.car.id, clients.size());
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
                std::printf("client %u disconnected\n", it->car.id);
                ::close(it->fd);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        // Simulate and broadcast one tick.
        racer::Snapshot snap;
        snap.tick = tick;
        for (auto& c : clients) {
            step(c.car, tick, dt);
            snap.cars.push_back(c.car);
        }
        const std::string wire = racer::encode(snap);
        for (auto it = clients.begin(); it != clients.end();) {
            if (send_all(it->fd, wire)) {
                ++it;
            } else {
                std::printf("client %u dropped (send failed)\n", it->car.id);
                ::close(it->fd);
                it = clients.erase(it);
            }
        }
        std::fflush(stdout);

        ++tick;
        next_tick += tick_len;
        std::this_thread::sleep_until(next_tick);
    }

    for (auto& c : clients) ::close(c.fd);
    ::close(listener);
    std::printf("racer_server stopped after %u ticks\n", tick);
    return 0;
}
