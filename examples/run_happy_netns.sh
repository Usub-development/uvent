#!/usr/bin/env bash

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../build/uvent_example_happy_netns}"
NS=uvent_he

if [[ $EUID -ne 0 ]]; then
    echo "нужен root: sudo bash $0" >&2
    exit 2
fi
if [[ ! -x "$BIN" ]]; then
    echo "бинарь не найден: $BIN (собери таргет uvent_example_happy_netns)" >&2
    exit 2
fi

cleanup() { ip netns del "$NS" 2>/dev/null || true; }
trap cleanup EXIT
cleanup

ip netns add "$NS"
ip -n "$NS" link set lo up
ip -n "$NS" addr add 10.99.0.2/32 dev lo
ip -n "$NS" addr add fd00::2/128 dev lo nodad

fails=0
run() {
    local name="$1"; shift
    if ip netns exec "$NS" "$@"; then :; else
        fails=$((fails + 1))
    fi
}

run reject "$BIN" reject

ip netns exec "$NS" ip6tables -A OUTPUT -d fd00::2 -p tcp --dport 45909 -j DROP
run drop "$BIN" drop
ip netns exec "$NS" ip6tables -D OUTPUT -d fd00::2 -p tcp --dport 45909 -j DROP

run v6live "$BIN" v6live

if [[ $fails -eq 0 ]]; then
    echo "happy netns smoke: ALL PASS"
else
    echo "happy netns smoke: $fails FAILURES"
    exit 1
fi
