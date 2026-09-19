#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
g++ -std=c++17 -Wall -Wextra -Werror \
  -I "$ROOT/shared/plantlink" -I "$ROOT/shared/zg303z" \
  "$ROOT/tests/host/test_shared.cpp" -o "${TMPDIR:-/tmp}/esp_plants_shared_test"
"${TMPDIR:-/tmp}/esp_plants_shared_test"
