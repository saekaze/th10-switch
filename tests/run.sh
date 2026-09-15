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

echo "== header syntax =="
g++ -std=c++17 -fsyntax-only -I src src/draw_batch.hpp
g++ -std=c++17 -fsyntax-only -I src src/host_graphics.hpp
g++ -std=c++17 -fsyntax-only -I src -I wasm2c src/host_graphics.cpp

echo "ALL HOST TESTS PASSED"
