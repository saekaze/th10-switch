#!/bin/bash
# Build touhou10.nro for Nintendo Switch (mirrors Saekaze's build_switch.sh).
#
# NOTE: switch.specs (libnx) expands %:getenv(DEVKITPRO ...) on every compiler
# and linker call, so DEVKITPRO must be set both at configure and build time.
# This script handles that itself.
#
# Usage: scripts/build_switch.sh [build-dir] [extra ninja args...]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT}/build-switch}"
shift 2>/dev/null || true

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLCHAIN="${DEVKITPRO}/cmake/Switch.cmake"

if [ ! -f "${TOOLCHAIN}" ]; then
    echo "devkitPro not found in ${DEVKITPRO}." >&2
    echo "Install devkitA64 + switch portlibs and set DEVKITPRO." >&2
    exit 1
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f build.ninja ]; then
    cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" -DCMAKE_BUILD_TYPE=Release "${ROOT}"
fi

exec ninja "$@"
