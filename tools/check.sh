#!/usr/bin/env bash
# Quality gate, in two depths.
#
#   tools/check.sh          quick: one clang build with ASan/UBSan and the tests, clang-tidy on the files
#                           that differ from origin/main, the restart check and the IRC check
#   tools/check.sh full     everything: GCC release, GCC with sanitizers, clang, cppcheck, clang-tidy on
#                           every file, the restart and IRC checks, REST fuzzing for FUZZ_SECONDS (60)
#
# The quick depth is for everyday changes; run the full one after touching rest_routes, http_server,
# irc*, storage or the build, and now and then anyway. deploy/deploy.sh still builds and tests the GCC
# release on its own, so a GCC-only warning cannot reach the VPS.
#
# Without installing the tools:
#   podman build -t norelecbot-dev -f tools/Containerfile tools
#   podman run --rm -it --cap-add=SYS_PTRACE -v "$PWD":/src:Z -w /src norelecbot-dev tools/check.sh [full]
set -euo pipefail
cd "$(dirname "$0")/.."

DEPTH="${1:-quick}"
case "$DEPTH" in
quick | full) ;;
*)
    echo "usage: $0 [quick|full]" >&2
    exit 2
    ;;
esac

FUZZ_SECONDS="${FUZZ_SECONDS:-60}"
OUT="$PWD/build-check"
STARTED=$SECONDS
step_started=$SECONDS
current=""

# Announces a step and prints how long the previous one took.
step() {
    [ -z "$current" ] || echo "    $current: $((SECONDS - step_started)) s"
    current="$1"
    step_started=$SECONDS
    echo "==> $1"
}

run_build() {
    local name="$1"
    shift
    step "$name"
    cmake -S . -B "$OUT/$name" "$@" >/dev/null
    cmake --build "$OUT/$name" -j"$(nproc)"
    ctest --test-dir "$OUT/$name" --output-on-failure
}

# The translation units clang-tidy has to look at: all of them after a header changed, since a header
# reaches into many of them, otherwise only the sources that differ from origin/main, committed or not.
tidy_targets() {
    local changed
    if ! changed="$(git -c safe.directory="$PWD" diff --name-only origin/main -- src tests fuzz 2>/dev/null)"; then
        echo "ALL"
        return
    fi
    changed="$changed
$(git -c safe.directory="$PWD" ls-files --others --exclude-standard -- src tests fuzz 2>/dev/null)"
    if grep -qE '\.hpp$' <<<"$changed"; then
        echo "ALL"
        return
    fi
    grep -E '\.cpp$' <<<"$changed" | while read -r file; do
        [ -f "$file" ] && echo "$PWD/$file"
    done || true
}

run_tidy() {
    local targets=("$@")
    # One file at a time took five minutes; run-clang-tidy spreads them over the cores.
    if ! run-clang-tidy -p "$OUT/clang-fuzz" -quiet -j "$(nproc)" "${targets[@]}" > "$OUT/clang-tidy.log" 2>&1; then
        grep -vE "^\[|warnings generated|^Enabled checks|^$" "$OUT/clang-tidy.log" >&2 || true
        exit 1
    fi
}

if [ "$DEPTH" = full ]; then
    run_build gcc-release -DCMAKE_BUILD_TYPE=Release
    run_build gcc-sanitize -DCMAKE_BUILD_TYPE=Debug -DNORELECBOT_SANITIZE=ON
fi
# Clang with ASan and UBSan: the one build the quick depth keeps, and the compile database clang-tidy reads.
run_build clang-fuzz -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
    -DNORELECBOT_FUZZ=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if [ "$DEPTH" = full ]; then
    step "cppcheck"
    # cppcheck 2.17 knows at most C++20; the C++23 parts are checked by the compilers and clang-tidy.
    cppcheck --enable=warning,style,performance,portability --language=c++ --std=c++20 --quiet --error-exitcode=1 \
        --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=normalCheckLevelMaxBranches \
        --suppress=constParameterReference:src/telegram.cpp \
        --suppress=useStlAlgorithm:src/rest_routes.cpp \
        -I src src tests fuzz
fi

if [ "$DEPTH" = full ]; then
    step "clang-tidy (every file)"
    run_tidy
else
    mapfile -t targets < <(tidy_targets)
    if [ "${#targets[@]}" -eq 0 ]; then
        step "clang-tidy: no source differs from origin/main"
    elif [ "${targets[0]}" = ALL ]; then
        step "clang-tidy (every file: a header changed)"
        run_tidy
    else
        step "clang-tidy (${#targets[@]} changed files)"
        # run-clang-tidy reads its positional arguments as regular expressions over the file paths.
        run_tidy "${targets[@]/#/^}"
    fi
fi

step "restart check"
tools/check_restart.sh bin/norelecbot

step "irc check"
tools/check_irc.sh bin/norelecbot

if [ "$DEPTH" = full ]; then
    step "fuzz REST routes for ${FUZZ_SECONDS}s"
    corpus="$OUT/fuzz-corpus"
    mkdir -p "$corpus"
    for path in /health /quote /leaderboard /user/Norelec /user/@lord_possum /user/ /user/a/b; do
        printf '\000%s' "$path" > "$corpus/$(printf '%s' "$path" | tr '/@' '_-').seed"
    done
    (cd "$OUT" && "$OLDPWD/bin/fuzz-rest-routes" -max_total_time="$FUZZ_SECONDS" -max_len=256 "$corpus")
fi

step "all $DEPTH checks passed in $((SECONDS - STARTED)) s"
