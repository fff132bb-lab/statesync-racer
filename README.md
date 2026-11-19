# statesync-racer

A small multiplayer racing game built around **server-authoritative state sync**.

## Idea

- **Server:** C++, designed to run as several distributed nodes. Each race shard runs its own fixed-tick authoritative loop and streams state updates to clients.
- **Real-time first:** tight tick-time budget so races feel responsive.
- **Deterministic replays:** every race is logged as a seed plus player inputs, with a state hash per tick, so any race can be replayed headless.
- **Automation:** overnight headless simulations are meant to help tune matchmaking, team balance, and car stats, and to catch netcode desyncs before release. Fixes come in as reviewed PRs.

## Status

Early skeleton. Nothing playable yet.

## Build

```sh
g++ -std=c++17 -O2 src/main.cpp -o racer_server
./racer_server
```

## License

MIT
