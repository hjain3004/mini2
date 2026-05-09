#!/usr/bin/env bash
# Build mini2 with Homebrew gRPC/Protobuf on macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"

PREFIX_PATH="${MINI2_PREFIX:-/opt/homebrew}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${ROOT}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="${PREFIX_PATH}"

cmake --build . -j"$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
echo "[build] done. binaries in ${BUILD_DIR}"
