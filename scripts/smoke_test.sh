#!/usr/bin/env bash
# End-to-end check:
#   1. server + three clients (one driving its own car): all receive snapshots,
#      and the driving client's input reaches the simulation
#   2. the recorded race log replays with every hash matching
#   3. a log with one tampered input is reported as a desync at that tick
set -euo pipefail
BIN=${1:-build}
PORT=${PORT:-7799}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$BIN/racer_server" "$PORT" 50 --seed 7 --record "$TMP/race.log" --max-ticks 100 > "$TMP/server.log" 2>&1 &
SERVER=$!
sleep 0.5

"$BIN/racer_client" 127.0.0.1 "$PORT" 5 > "$TMP/client1.log" &
"$BIN/racer_client" 127.0.0.1 "$PORT" 10 --steer 1 --throttle 255 > "$TMP/driver.log" &
"$BIN/racer_client" 127.0.0.1 "$PORT" 5 > "$TMP/client2.log"
wait $SERVER

for f in "$TMP/client1.log" "$TMP/client2.log"; do
  n=$(grep -c '^tick ' "$f")
  [ "$n" -eq 5 ] || { echo "FAIL: $f got $n snapshots"; exit 1; }
done
n=$(grep -c '^tick ' "$TMP/driver.log")
[ "$n" -eq 10 ] || { echo "FAIL: driver got $n snapshots"; exit 1; }
echo "PASS: all clients received their snapshots"

# The driver sends "INPUT 1 255" after each snapshot; the server must apply
# it on later ticks (bots almost never produce exactly that pair repeatedly).
n=$(awk '$1=="I" && $4==1 && $5==255' "$TMP/race.log" | wc -l)
[ "$n" -ge 5 ] || { echo "FAIL: driver input applied on only $n ticks"; exit 1; }
echo "PASS: driver input applied on $n ticks"

# Hash a client saw must equal the hash the server logged for that tick.
read -r tick hash < <(awk '/^tick /{print $2, $4; exit}' "$TMP/client2.log" | tr -d :)
grep -q "^H $tick $hash\$" "$TMP/race.log" || { echo "FAIL: client hash for tick $tick not in log"; exit 1; }
echo "PASS: client hash matches server log at tick $tick"

"$BIN/racer_replay" "$TMP/race.log" || { echo "FAIL: clean replay"; exit 1; }
echo "PASS: clean replay"

# Bump the throttle of the first input at the last recorded input tick.
t=$(awk '$1=="I"{t=$2} END{print t}' "$TMP/race.log")
awk -v t="$t" '!d && $1=="I" && $2==t {$5=($5+1)%256; d=1} {print}' "$TMP/race.log" > "$TMP/bad.log"
set +e
out=$("$BIN/racer_replay" "$TMP/bad.log"); rc=$?
set -e
echo "$out"
[ "$rc" -eq 1 ] && grep -q "DESYNC at tick $t:" <<<"$out" || { echo "FAIL: tampered log not caught at tick $t"; exit 1; }
echo "PASS: tampered input caught at tick $t"
