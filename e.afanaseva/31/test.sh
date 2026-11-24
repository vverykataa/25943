#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

cd "$(dirname "$0")"

server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
}

trap cleanup EXIT

task="$(basename "$PWD")"
socket="/tmp/task${task}_$(id -u).sock"

if [[ -e "$socket" ]]; then
    echo "Socket already exists: $socket"
    exit 1
fi

./server &
server_pid=$!

for i in 1 2 3 4 5; do
    if [[ -S "$socket" ]]; then
        break
    fi
    sleep 1
done

if [[ ! -S "$socket" ]]; then
    echo "Server failed to start."
    exit 1
fi

(
    perl -e '
        $| = 1;
        for (1..2000) {
            print "a";
            select(undef, undef, undef, 0.001);
        }
        print "\n";
    ' | ./client
) &
client1_pid=$!

(
    perl -e '
        $| = 1;
        for (1..2000) {
            print "b";
            select(undef, undef, undef, 0.001);
        }
        print "\n";
    ' | ./client
) &
client2_pid=$!

wait "$client1_pid"
wait "$client2_pid"

echo
echo "Both clients finished."
