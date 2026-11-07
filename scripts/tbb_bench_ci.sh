#!/usr/bin/env bash
# Builds orca_bench against TBB 2021.5 and 2023.2 on one runner, then alternates them.
# Arms: A = 2021.5, B = 2023.2 default, C = 2023.2 with fast leave.
set -euxo pipefail

ROUNDS=${ROUNDS:-3}
BENCH_ARGS=${BENCH_ARGS:---policy quick --kind slice}
OS=$(uname -s)
JOBS=$(getconf _NPROCESSORS_ONLN)
OUT=$PWD/bench-out
mkdir -p "$OUT" "$OUT/bin"

# The branch's TBB.cmake names 2023.2. The deps start from 2021.5, like main.
perl -pi -e 's{uxlfoundation/oneTBB/archive/refs/tags/v2023.2.0.zip}{oneapi-src/oneTBB/archive/refs/tags/v2021.5.0.zip};
             s{6a7cf5f1873afd6efb82af3de40ae08f4898cd4fe7de5d7bf96cd24a6546cc9b}{83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47}' deps/TBB/TBB.cmake

if [ "$OS" = Darwin ]; then
    ./build_release_macos.sh -dx -j "$JOBS" -a arm64 -t 10.15
    BUILD=build/arm64
    COMPILER_ARGS=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 "-DCMAKE_IGNORE_PREFIX_PATH=/opt/local:/usr/local:/opt/homebrew" -DCMAKE_POLICY_VERSION_MINIMUM=3.10)
else
    mkdir -p deps/build/destdir
    ./build_linux.sh -drlL
    BUILD=build
    COMPILER_ARGS=(-DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld)
fi

TBB_CONFIG=$(find "$PWD/deps/build" -path '*usr/local*' -name TBBConfig.cmake | head -1)
PREFIX=${TBB_CONFIG%/lib*/cmake/TBB/TBBConfig.cmake}
test -d "$PREFIX/include/oneapi"

build_bench() {
    rm -rf "$BUILD"
    cmake -S . -B "$BUILD" -G "Ninja Multi-Config" "${COMPILER_ARGS[@]}" -DSLIC3R_PCH=ON -DORCA_TOOLS=ON -DORCA_BENCHMARKS=ON "-DCMAKE_PREFIX_PATH=$PREFIX"
    cmake --build "$BUILD" --config Release --target orca_bench
    mkdir -p "$OUT/bin/$1"
    cp "$BUILD/benchmarks/Release/orca_bench" "$OUT/bin/$1/"
}

build_bench A

curl -fsSL -o tbb.zip https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v2023.2.0.zip
echo "6a7cf5f1873afd6efb82af3de40ae08f4898cd4fe7de5d7bf96cd24a6546cc9b  tbb.zip" | shasum -a 256 -c -
unzip -q tbb.zip
cmake -S oneTBB-2023.2.0 -B tbb-build -G Ninja -DCMAKE_BUILD_TYPE=Release "${COMPILER_ARGS[@]}" -DBUILD_SHARED_LIBS=OFF -DTBB_TEST=OFF \
    -DTBB_DISABLE_HWLOC_AUTOMATIC_SEARCH=ON -DTBB_ENABLE_IPO=OFF -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$PWD/tbb-new" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build tbb-build
cmake --install tbb-build
rm -rf "$PREFIX/include/tbb" "$PREFIX/include/oneapi" "$PREFIX"/lib*/libtbb* "$PREFIX"/lib*/cmake/TBB "$PREFIX"/lib*/pkgconfig/tbb.pc
cp -R tbb-new/include/* "$PREFIX/include/"
LIBDIR=$(dirname "$(dirname "$(dirname "$TBB_CONFIG")")")
cp -R tbb-new/lib/* "$LIBDIR/"

build_bench B

{
    uname -a
    if [ "$OS" = Darwin ]; then sysctl -n machdep.cpu.brand_string hw.logicalcpu; else lscpu | sed -n 1,20p; fi
} > "$OUT/host.txt"

run_arm() { # arm round position
    local exe=$OUT/bin/$1/orca_bench
    unset ORCA_BENCH_FAST_LEAVE
    if [ "$1" = C ]; then
        exe=$OUT/bin/B/orca_bench
        export ORCA_BENCH_FAST_LEAVE=1
    fi
    # shellcheck disable=SC2086
    "$exe" $BENCH_ARGS --quiet --reporter null --out "$OUT/$1-r$2-$3.json" 2> "$OUT/$1-r$2-$3.log" || echo "arm $1 exited $?"
    cat "$OUT/$1-r$2-$3.log"
}

for round in $(seq 1 "$ROUNDS"); do
    pos=0
    for arm in A B C C B A; do
        date -u +%H:%M:%S
        run_arm "$arm" "$round" "$pos"
        pos=$((pos + 1))
    done
done
