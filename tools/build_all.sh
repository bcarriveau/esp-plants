#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 "$ROOT/tools/check_protocol_sync.py"

echo
echo "Building T5 hub..."
pio run -d "$ROOT/firmware/t5-hub"

echo
echo "Building XIAO sensor..."
pio run -d "$ROOT/firmware/xiao-soil-sensor"

echo
echo "Both PlatformIO builds completed."
