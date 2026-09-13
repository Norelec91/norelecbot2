#!/usr/bin/env bash
# End-to-end check of the IRC client against a fake TLS server: the bot must verify the registration
# with a WHOIS and answer only a nick that numeric 307 marks as identified.
#
#   tools/check_irc.sh [binary]      default: bin/norelecbot
#   IRC_CHECK_PORT=18697 tools/check_irc.sh
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="$(realpath "${1:-bin/norelecbot}")"
FAKE="$(realpath tools/fake_ircd.py)"
PORT="${IRC_CHECK_PORT:-18697}"
WORK="$(mktemp -d)"
trap 'kill $(jobs -p) 2>/dev/null || true; wait || true; rm -rf "$WORK"' EXIT
cd "$WORK"

fail() {
    echo "!! irc check: $*" >&2
    echo "-- sent by the bot:" >&2
    cat transcript >&2 2>/dev/null || true
    tail -n 10 bot.log >&2 2>/dev/null || true
    exit 1
}

openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out certificate.pem -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" >/dev/null 2>&1

cat > .env <<ENV
NORELECBOT_CONQUISTER_ENABLED=0
NORELECBOT_API_HOST=127.0.0.1
NORELECBOT_API_PORT=$((PORT + 1))
NORELECBOT_IRC_ENABLED=1
NORELECBOT_IRC_SERVER=localhost
NORELECBOT_IRC_PORT=$PORT
NORELECBOT_IRC_NICK=bot
NORELECBOT_IRC_CHANNEL=#test
NORELECBOT_IRC_OWNER=Norelec
NORELECBOT_QUOTE_COST=0
ENV
echo '[]' > quotes.json

run_case() {
    local name="$1" registered="$2" expected="$3"
    rm -f transcript conquister.json
    python3 "$FAKE" --certificate certificate.pem --key key.pem --port "$PORT" --transcript transcript \
        $registered \
        --message ':Marco189!~m@host PRIVMSG #test :ciao a tutti' \
        --message ':Marco189!~m@host PRIVMSG #test :We @TheConquister37' > fake.log 2>&1 &
    local server=$!
    sleep 0.5
    SSL_CERT_FILE="$PWD/certificate.pem" "$BINARY" > bot.log 2>&1 &
    local bot=$!
    for ((attempt = 0; attempt < 100; attempt++)); do
        grep -q "$expected" transcript 2>/dev/null && break
        sleep 0.2
    done
    kill "$bot" 2>/dev/null || true
    wait "$bot" 2>/dev/null || true
    kill "$server" 2>/dev/null || true
    wait "$server" 2>/dev/null || true
    grep -q "$expected" transcript || fail "$name: expected '$expected'"
    echo "-- irc check: $name"
}

run_case "an identified nick plays" --registered 'PRIVMSG #test :.*sei in @TheConquister37'
grep -q 'WHOIS Marco189' transcript || fail "the registration was not verified"
[[ $(grep -c 'WHOIS Marco189' transcript) == 1 ]] || fail "the WHOIS answer was not cached"
if grep -q 'ciao a tutti' transcript; then fail "a plain message must not reach the bot"; fi

run_case "an unregistered nick is turned away" "" 'PRIVMSG #test :Marco189 devi essere registrato'
if grep -q 'sei in @TheConquister37' transcript; then fail "an unregistered nick must not play"; fi

echo "-- irc check: ok"
