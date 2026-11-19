# statesync-racer

A small multiplayer racing game built around **server-authoritative state sync**.

The server owns the simulation. It steps every car at a fixed tick rate and
broadcasts the full world snapshot to all connected clients each tick. Clients
only render what the server says.

## What works today

- `racer_server`: listens on a TCP port, gives each connected client a car,
  steps a dummy simulation (each car drives a circle) at a fixed tick rate,
  and broadcasts position and velocity for every car each tick. Handles
  clients joining and leaving at any time.
- `racer_client`: connects, decodes each snapshot, and prints it.
- `src/common/protocol.hpp`: the snapshot format shared by both sides.

Wire format, one line per tick (text for now so it's easy to inspect with `nc`):

```
STATE <tick> <count> [<id> <x> <y> <vx> <vy>]...
```

## Build

Needs a C++17 compiler on Linux or macOS.

With CMake:

```sh
cmake -B build
cmake --build build
```

Or directly:

```sh
mkdir -p build
c++ -std=c++17 -O2 src/server/main.cpp -o build/racer_server
c++ -std=c++17 -O2 src/client/main.cpp -o build/racer_client
```

## Run

In one terminal, start the server (port 7777, 20 ticks per second):

```sh
./build/racer_server 7777 20
```

In one or more other terminals, start clients:

```sh
./build/racer_client 127.0.0.1 7777
```

Each client prints lines like:

```
tick 12: 2 car(s)  [car 1 pos=(13.51, 21.04) vel=(-10.52, 6.75)]  [car 2 pos=(-3.87, 29.75) vel=(-14.88, -1.93)]
```

Pass a third argument to stop after N snapshots, for example
`./build/racer_client 127.0.0.1 7777 10`.

Stop the server with Ctrl+C.

## Smoke test

```sh
./scripts/smoke_test.sh build
```

Starts a server, connects two clients, and checks both got 5 snapshots.

## Roadmap

- Client input (steer, throttle, brake) sent to the server
- Deterministic fixed-point physics and per-tick state hashes
- Input logging and headless replay for desync detection
- Binary wire format and UDP transport
- Sharding races across multiple server nodes

## License

MIT
