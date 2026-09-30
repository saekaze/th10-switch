#!/bin/bash
# Host-side tests that do not need game data, GLES, or the Switch SDK.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "== decode932/936 =="
g++ -O2 -std=c++17 -I src -I wasm2c tests/test_decode.cpp \
    $(pkg-config --cflags --libs freetype2) -o /tmp/th10_test_decode
/tmp/th10_test_decode

echo "== batching / pacing =="
g++ -O2 -std=c++17 -I src tests/test_batch.cpp -o /tmp/th10_test_batch
/tmp/th10_test_batch

echo "== r4 frame clock pacing =="
g++ -O2 -std=c++17 tests/test_pacing.cpp -o /tmp/th10_test_pacing
/tmp/th10_test_pacing

echo "== header syntax =="
g++ -std=c++17 -fsyntax-only -I src src/draw_batch.hpp
g++ -std=c++17 -fsyntax-only -I src src/host_graphics.hpp
g++ -std=c++17 -fsyntax-only -I src -I wasm2c src/host_graphics.cpp

echo "== native triangle resample vs module (compiles the module, ~1 min) =="
if [ "${SKIP_MODULE_TESTS:-0}" != "1" ]; then
    B=/tmp/th10_test_module
    mkdir -p "$B"
    CFLAGS_M="-O2 -ffp-contract=off -w"
    gcc $CFLAGS_M -Iwasm2c -c wasm2c/th10_wasm.c -o "$B/th10_wasm.o"
    gcc $CFLAGS_M -Iwasm2c -c wasm2c/wasm-rt-impl.c -o "$B/wasm-rt-impl.o"
    gcc $CFLAGS_M -Iwasm2c -c wasm2c/wasm-rt-mem-impl.c -o "$B/wasm-rt-mem-impl.o"
    g++ -O2 -std=c++17 -ffp-contract=off -I src -I wasm2c $(pkg-config --cflags sdl2 freetype2) \
        tests/test_resample.cpp src/host_resample.cpp src/host_fonts.cpp src/host_files.cpp \
        src/host_graphics.cpp src/host_audio.cpp src/audio_mixer.cpp src/host_time.cpp \
        src/gl_renderer.cpp src/host_input.cpp "$B"/*.o \
        $(pkg-config --libs sdl2 freetype2) -lGLESv2 -lm -o "$B/test_resample"
    "$B/test_resample"
else
    echo "(skipped)"
fi

echo "ALL HOST TESTS PASSED"
