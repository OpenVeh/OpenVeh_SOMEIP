#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PARAM_OUT="$ROOT_DIR/tools/someip_idl/tests/generated_app/params"
APP_OUT="$ROOT_DIR/tools/someip_idl/tests/generated_app/app"
TEST_BIN="$ROOT_DIR/tools/someip_idl/tests/generated_app/test_generated_app_framework"
IDL_DIR="$ROOT_DIR/tools/someip_idl/examples/vehicle_node"

python3 "$ROOT_DIR/tools/someip_idl/generate_someip_params.py" "$IDL_DIR" --out "$PARAM_OUT"
python3 "$ROOT_DIR/tools/someip_idl/generate_someip_app.py" "$IDL_DIR" --out "$APP_OUT"

g++ -std=c++17 -Wall -Wextra -pedantic \
    -I "$PARAM_OUT" \
    -I "$APP_OUT" \
    -I "$ROOT_DIR/services/someip_app_framework/include" \
    "$ROOT_DIR/tools/someip_idl/tests/generated_app/test_generated_app_framework.cpp" \
    -o "$TEST_BIN"

"$TEST_BIN"
