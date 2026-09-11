#!/usr/bin/env bash
# Deploy the committed HEAD of this repository to the VPS.
#
#   deploy/deploy.sh             send sources, build, run tests, restart the bot
#   deploy/deploy.sh rollback    swap back to the previous binary and restart
#
# Environment:
#   DEPLOY_HOST           ssh destination, user@host (default: first line of deploy/host, gitignored)
#   DEPLOY_SSH            ssh command, may include options (default: ssh)
#   DEPLOY_ALLOW_DIRTY=1  deploy even with uncommitted changes (they are NOT sent)
set -euo pipefail

DEPLOY_SSH="${DEPLOY_SSH:-ssh}"
ACTION="${1:-deploy}"

case "$ACTION" in
deploy | rollback) ;;
*)
    echo "usage: $0 [deploy|rollback]" >&2
    exit 2
    ;;
esac

cd "$(dirname "$0")/.."

if [ -z "${DEPLOY_HOST:-}" ] && [ -f deploy/host ]; then
    DEPLOY_HOST="$(head -n 1 deploy/host)"
fi
if [ -z "${DEPLOY_HOST:-}" ]; then
    echo "!! set DEPLOY_HOST=user@host or write it in deploy/host" >&2
    exit 2
fi

# All ssh calls share one connection, so a password is asked at most once.
control_path="${XDG_RUNTIME_DIR:-/tmp}/norelecbot-deploy-%C"

remote() {
    # shellcheck disable=SC2086  # DEPLOY_SSH may contain options
    $DEPLOY_SSH -o ControlMaster=auto -o "ControlPath=$control_path" -o ControlPersist=60 \
        "$DEPLOY_HOST" "$@"
}

remote_close() {
    # shellcheck disable=SC2086
    $DEPLOY_SSH -o "ControlPath=$control_path" -O exit "$DEPLOY_HOST" >/dev/null 2>&1 || true
}

# Runs on the VPS as: bash -s -- <action> <commit>
remote_script() {
    cat <<'EOF'
set -euo pipefail
action="$1"
commit="$2"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
cd ~/norelecbot

port="$(sed -n 's/^NORELECBOT_API_PORT=//p' .env | tr -d "\"' ")"
port="${port:-8000}"

healthy() {
    systemctl --user is-active --quiet norelecbot &&
        curl -fsS -m 2 "http://127.0.0.1:$port/health" >/dev/null 2>&1
}

restart_and_check() {
    systemctl --user restart norelecbot
    for _ in $(seq 1 15); do
        # Healthy twice, 3 seconds apart: catches a bot that crashes right after starting.
        if healthy && sleep 3 && healthy; then
            journalctl --user -u norelecbot -n 5 --no-pager -o cat
            return 0
        fi
        sleep 1
    done
    echo "!! norelecbot is not healthy, last log lines:" >&2
    journalctl --user -u norelecbot -n 30 --no-pager -o cat >&2
    return 1
}

case "$action" in
deploy)
    unit=~/.config/systemd/user/norelecbot.service
    if [ -f deploy/norelecbot.service ] && ! cmp -s deploy/norelecbot.service "$unit"; then
        install -m 644 deploy/norelecbot.service "$unit"
        systemctl --user daemon-reload
        echo "==> systemd unit updated"
    fi
    [ -d build ] || cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    if [ -x bin/norelecbot ]; then
        cp -p bin/norelecbot bin/norelecbot.prev
    fi
    echo "==> building"
    cmake --build build -j"$(nproc)"
    echo "==> testing"
    ctest --test-dir build --output-on-failure
    echo "==> restarting"
    if ! restart_and_check; then
        echo "!! run 'deploy/deploy.sh rollback' to restore the previous binary" >&2
        exit 1
    fi
    printf '%s deploy %s\n' "$(date -Iseconds)" "$commit" >> DEPLOY_HISTORY
    ;;
rollback)
    if [ ! -x bin/norelecbot.prev ]; then
        echo "!! no previous binary to roll back to" >&2
        exit 1
    fi
    mv bin/norelecbot bin/norelecbot.swap
    mv bin/norelecbot.prev bin/norelecbot
    mv bin/norelecbot.swap bin/norelecbot.prev
    echo "==> restarting previous binary"
    restart_and_check
    printf '%s rollback\n' "$(date -Iseconds)" >> DEPLOY_HISTORY
    ;;
esac
EOF
}

trap remote_close EXIT
commit="$(git rev-parse --short HEAD)"

if [ "$ACTION" = deploy ]; then
    if [ -n "$(git status --porcelain)" ] && [ "${DEPLOY_ALLOW_DIRTY:-}" != 1 ]; then
        echo "!! uncommitted changes (only the committed HEAD is deployed):" >&2
        git status --short >&2
        echo "   commit them, or set DEPLOY_ALLOW_DIRTY=1 to deploy HEAD anyway" >&2
        exit 1
    fi
    echo "==> sending $commit to $DEPLOY_HOST"
    # -m: give extracted files the current time, so the build always sees them as changed.
    git archive --format=tar HEAD | remote 'tar -xm -C ~/norelecbot'
fi

remote_script | remote bash -s -- "$ACTION" "$commit"
echo "==> $ACTION completed"
