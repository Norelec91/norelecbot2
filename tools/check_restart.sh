#!/usr/bin/env bash
# Restart check for the REST server: a second instance must be refused, and after a restart that leaves
# server-side connections in TIME_WAIT the API must answer again within a few seconds.
#
#   tools/check_restart.sh [binary]      default: bin/norelecbot
#   RESTART_CHECK_PORT=18095 tools/check_restart.sh
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="$(realpath "${1:-bin/norelecbot}")"
PORT="${RESTART_CHECK_PORT:-18090}"
WORK="$(mktemp -d)"
trap 'kill $(jobs -p) 2>/dev/null || true; wait || true; rm -rf "$WORK"' EXIT
printf 'NORELECBOT_CONQUISTER_ENABLED=0\nNORELECBOT_API_HOST=127.0.0.1\nNORELECBOT_API_PORT=%s\n' "$PORT" > "$WORK/.env"
cd "$WORK"

fail() {
    echo "!! restart check: $*" >&2
    tail -n 5 bot.log >&2 || true
    exit 1
}

# "Connection: close" makes the server close first, leaving its side of the connection in TIME_WAIT.
healthy() {
    local status
    status=$({
        printf 'GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' >&3
        IFS= read -r -t 2 line <&3 && printf '%s' "$line"
    } 2>/dev/null 3<>"/dev/tcp/127.0.0.1/$PORT") || return 1
    [[ $status == "HTTP/1.1 200"* ]]
}

wait_healthy() {
    for ((attempt = 0; attempt < $1 * 10; attempt++)); do
        healthy && return 0
        sleep 0.1
    done
    return 1
}

stop() {
    kill -TERM "$1" 2>/dev/null || fail "the bot had already exited"
    wait "$1" || fail "the bot did not stop cleanly (exit $?)"
}

! healthy || fail "port $PORT is already in use"
"$BINARY" 2>>bot.log &
first=$!
wait_healthy 10 || fail "the API did not start on port $PORT"

set +e
timeout 3 "$BINARY" 2>>bot.log
status=$?
set -e
[[ $status == 1 ]] && grep -q "could not listen" bot.log || fail "a second instance was not refused (exit $status)"

healthy || fail "the API stopped answering"
stop "$first"
"$BINARY" 2>>bot.log &
first=$!
wait_healthy 3 || fail "the API did not answer within 3 s after a restart"
stop "$first"

echo "restart check: second instance refused, API back right after a restart"
