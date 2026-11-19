// raylib viewer: connects to racer_server, draws every car from the latest
// snapshot, and drives your own car with the arrow keys or WASD.
//
// Usage: racer_viewer [host] [port] [--ready] [--frames N --screenshot FILE]
//   Enter or Space sends READY in the lobby; --ready sends it on connect.
//   --frames N         quit after N rendered frames
//   --screenshot FILE  save the last frame as a PNG before quitting
//                      (used by CI to prove the renderer works)
//
// Rendering follows your car (or the middle of the pack before the server has
// told you which car is yours) over a 10 m grid, so motion stays visible.

#include "../common/protocol.hpp"
#include "../common/sim.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "raylib.h"

namespace {

int connect_to(const char* host, const char* port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, port, &hints, &res) != 0) return -1;
    int fd = -1;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd >= 0 && ::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd >= 0) ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

Color car_color(uint32_t id) {
    static const Color palette[] = {RED, BLUE, ORANGE, PURPLE, DARKGREEN, MAROON, VIOLET, BROWN};
    return palette[id % (sizeof palette / sizeof palette[0])];
}

constexpr float kPixelsPerMetre = 8.f;

} // namespace

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    const char* port = "7777";
    long max_frames = 0;
    std::string screenshot;
    bool auto_ready = false;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) max_frames = std::atol(argv[++i]);
        else if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (arg == "--ready") auto_ready = true;
        else if (positional == 0) { host = argv[i]; ++positional; }
        else if (positional == 1) { port = argv[i]; ++positional; }
        else { std::fprintf(stderr, "unknown argument: %s\n", argv[i]); return 1; }
    }

    const int fd = connect_to(host, port);
    if (fd < 0) {
        std::fprintf(stderr, "cannot connect to %s:%s\n", host, port);
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(960, 600, "statesync-racer");
    SetTargetFPS(60);

    std::string pending;
    racer::Snapshot snap;
    bool have_snap = false;
    uint32_t my_id = 0;
    std::string last_sent;
    racer::PhaseInfo phase;
    bool sent_ready = false;
    bool connected = true;
    Camera2D cam{};
    cam.offset = {480.f, 300.f};
    cam.zoom = 0.55f;  // the whole 200 m x 120 m track nearly fits

    for (long frame = 0; !WindowShouldClose(); ++frame) {
        // Drain whatever the server sent; keep only the newest snapshot.
        char buf[8192];
        for (;;) {
            ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n > 0) { pending.append(buf, static_cast<size_t>(n)); continue; }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) connected = false;
            break;
        }
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            uint32_t id = 0;
            racer::Snapshot s;
            racer::PhaseInfo ph;
            if (racer::decode_welcome(line, id)) my_id = id;
            else if (racer::decode_phase(line, ph)) phase = ph;
            else if (racer::decode(line, s)) { snap = std::move(s); have_snap = true; }
        }

        if (!sent_ready && connected && (auto_ready || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) {
            const std::string ready = "READY\n";
            sent_ready = ::send(fd, ready.data(), ready.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(ready.size());
        }

        // Send controls only when they change; the server holds the last one.
        const int steer = (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) - (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A));
        const int throttle = (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) ? 255 : 0;
        const std::string line = racer::encode_input(steer, throttle);
        if (connected && line != last_sent && (steer != 0 || throttle != 0 || !last_sent.empty())) {
            if (::send(fd, line.data(), line.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(line.size()))
                last_sent = line;
        }

        // Follow our car, or the middle of the pack.
        float fx = 0.f, fy = 0.f;
        int n = 0;
        for (const auto& c : snap.cars) {
            if (c.id == my_id) { fx = c.x; fy = c.y; n = 1; break; }
            fx += c.x; fy += c.y; ++n;
        }
        if (n > 1) { fx /= static_cast<float>(n); fy /= static_cast<float>(n); }
        cam.target = {fx * kPixelsPerMetre, -fy * kPixelsPerMetre};

        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode2D(cam);
        const float grid = 10.f * kPixelsPerMetre;
        const float gx0 = cam.target.x - 1000.f - std::fmod(cam.target.x - 1000.f, grid);
        const float gy0 = cam.target.y - 700.f - std::fmod(cam.target.y - 700.f, grid);
        for (float x = gx0; x < cam.target.x + 1000.f; x += grid)
            DrawLineV({x, cam.target.y - 700.f}, {x, cam.target.y + 700.f}, LIGHTGRAY);
        for (float y = gy0; y < cam.target.y + 700.f; y += grid)
            DrawLineV({cam.target.x - 1000.f, y}, {cam.target.x + 1000.f, y}, LIGHTGRAY);
        // Track: outer wall, inner block, start/finish line at x = 0.
        const float ox = racer::Track::outer_x / 1e6f * kPixelsPerMetre, oy = racer::Track::outer_y / 1e6f * kPixelsPerMetre;
        const float ix = racer::Track::inner_x / 1e6f * kPixelsPerMetre, iy = racer::Track::inner_y / 1e6f * kPixelsPerMetre;
        DrawRectangleLinesEx({-ox, -oy, 2 * ox, 2 * oy}, 4.f, DARKGRAY);
        DrawRectangleRec({-ix, -iy, 2 * ix, 2 * iy}, Fade(DARKGREEN, 0.35f));
        DrawRectangleLinesEx({-ix, -iy, 2 * ix, 2 * iy}, 4.f, DARKGRAY);
        for (float y = iy; y < oy; y += 16.f) {  // checkered start/finish line
            DrawRectangleV({-4.f, y}, {8.f, 8.f}, BLACK);
            DrawRectangleV({-4.f, y + 8.f}, {8.f, 8.f}, LIGHTGRAY);
        }
        for (const auto& c : snap.cars) {
            const Vector2 p{c.x * kPixelsPerMetre, -c.y * kPixelsPerMetre};
            DrawCircleV(p, 10.f, car_color(c.id));
            // Velocity arrow, 0.5 s ahead.
            DrawLineEx(p, {p.x + c.vx * 0.5f * kPixelsPerMetre, p.y - c.vy * 0.5f * kPixelsPerMetre}, 3.f, DARKGRAY);
            if (c.id == my_id) DrawCircleLinesV(p, 16.f, BLACK);
            DrawText(TextFormat("%u  L%u", c.id, c.lap), static_cast<int>(p.x) + 12, static_cast<int>(p.y) - 22, 18, BLACK);
        }
        EndMode2D();
        DrawText(have_snap ? TextFormat("tick %u  hash %016llx  cars %zu", snap.tick,
                                        static_cast<unsigned long long>(snap.hash), snap.cars.size())
                           : "waiting for server...",
                 12, 12, 20, DARKGRAY);
        DrawText(my_id ? TextFormat("you are car %u  |  arrows / WASD to drive", my_id) : "",
                 12, 38, 18, GRAY);
        const char* status = "";
        switch (phase.phase) {
        case racer::Phase::Lobby:
            status = TextFormat("LOBBY  %u/%u ready%s", phase.a, phase.b, sent_ready ? "" : "  -  press Enter when ready");
            break;
        case racer::Phase::Countdown: status = TextFormat("STARTING IN %u", phase.a); break;
        case racer::Phase::Race: status = TextFormat("RACE  -  first to %u laps", phase.a); break;
        case racer::Phase::Finished: status = TextFormat("FINISHED  -  car %u wins", phase.a); break;
        }
        if (have_snap) DrawText(status, 12, 64, 22, MAROON);
        if (!connected) DrawText("disconnected", 12, 92, 20, RED);
        EndDrawing();

        if (max_frames != 0 && frame + 1 >= max_frames) {
            if (!screenshot.empty()) {
                Image img = LoadImageFromScreen();
                const bool ok = ExportImage(img, screenshot.c_str());
                UnloadImage(img);
                if (!ok) { std::fprintf(stderr, "cannot write %s\n", screenshot.c_str()); CloseWindow(); return 1; }
                std::printf("saved %s at tick %u with %zu car(s)\n", screenshot.c_str(), snap.tick, snap.cars.size());
            }
            break;
        }
    }

    CloseWindow();
    ::close(fd);
    return 0;
}
