#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/services/someip_sd_cpp/build"

cmake -S "$ROOT_DIR/services/someip_sd_cpp" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j
"$BUILD_DIR/test_sd_service"
