#!/usr/bin/env bash
# Quality gate: GCC and clang builds with tests, sanitizers, cppcheck, clang-tidy, a restart check and REST fuzzing.
# Needs cmake, gcc, clang, clang-tidy, cppcheck, libFuzzer and the build dependencies.
#
#   tools/check.sh                   FUZZ_SECONDS=60 by default
#   FUZZ_SECONDS=600 tools/check.sh
#
# Without installing the tools:
#   podman build -t norelecbot-dev -f tools/Containerfile tools
#   podman run --rm -it --cap-add=SYS_PTRACE -v "$PWD":/src:Z -w /src norelecbot-dev tools/check.sh
set -euo pipefail
cd "$(dirname "$0")/.."

FUZZ_SECONDS="${FUZZ_SECONDS:-60}"
OUT="$PWD/build-check"

run_build() {
    local name="$1"
    shift
    echo "==> $name"
    cmake -S . -B "$OUT/$name" "$@" >/dev/null
    cmake --build "$OUT/$name" -j"$(nproc)"
    ctest --test-dir "$OUT/$name" --output-on-failure
}

run_build gcc-release -DCMAKE_BUILD_TYPE=Release
run_build gcc-sanitize -DCMAKE_BUILD_TYPE=Debug -DNORELECBOT_SANITIZE=ON
run_build clang-fuzz -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
    -DNORELECBOT_FUZZ=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "==> cppcheck"
# cppcheck 2.17 knows at most C++20; the C++23 parts are checked by the compilers and clang-tidy.
cppcheck --enable=warning,style,performance,portability --language=c++ --std=c++20 --quiet --error-exitcode=1 \
    --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=normalCheckLevelMaxBranches \
    --suppress=constParameterReference:src/telegram.cpp \
    --suppress=useStlAlgorithm:src/rest_routes.cpp \
    -I src src tests fuzz

echo "==> clang-tidy"
clang-tidy -p "$OUT/clang-fuzz" --quiet src/*.cpp tests/*.cpp fuzz/*.cpp 2> >(grep -v "warnings generated" >&2)

echo "==> restart check"
tools/check_restart.sh bin/norelecbot

echo "==> fuzz REST routes for ${FUZZ_SECONDS}s"
corpus="$OUT/fuzz-corpus"
mkdir -p "$corpus"
for path in /health /quote /leaderboard /user/Norelec /user/@lord_possum /user/ /user/a/b; do
    printf '\000%s' "$path" > "$corpus/$(printf '%s' "$path" | tr '/@' '_-').seed"
done
(cd "$OUT" && "$OLDPWD/bin/fuzz-rest-routes" -max_total_time="$FUZZ_SECONDS" -max_len=256 "$corpus")

echo "==> all checks passed"
