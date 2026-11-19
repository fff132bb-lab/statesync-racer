#!/usr/bin/env bash
# Starts the server, connects two clients, and checks both received snapshots.
set -euo pipefail
BIN=${1:-build}
PORT=${PORT:-7799}

"$BIN/racer_server" "$PORT" 20 > /tmp/racer_server.log 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
sleep 0.5

"$BIN/racer_client" 127.0.0.1 "$PORT" 5 > /tmp/racer_client1.log &
"$BIN/racer_client" 127.0.0.1 "$PORT" 5 > /tmp/racer_client2.log
wait %2 2>/dev/null || true

for f in /tmp/racer_client1.log /tmp/racer_client2.log; do
  n=$(grep -c '^tick ' "$f")
  if [ "$n" -ne 5 ]; then
    echo "FAIL: $f got $n snapshots"; exit 1
  fi
done
echo "PASS: both clients received 5 snapshots"
