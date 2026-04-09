#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

OUT_BIN="tools/someip_idl/tests/test_generated_params"

g++ -std=c++17 -Wall -Wextra -pedantic \
    -I tools/someip_idl/generated \
    tools/someip_idl/tests/test_generated_params.cpp \
    -o "$OUT_BIN"

"$OUT_BIN"
