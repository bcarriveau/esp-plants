#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 "$ROOT/tools/check_protocol_sync.py"

projects=(
  "firmware/waveshare-hub"
  "firmware/m5-h2-zigbee"
  "firmware/t5-hub"
  "firmware/xiao-soil-sensor"
)

for project in "${projects[@]}"; do
  echo
  echo "Building $project..."
  pio run -d "$ROOT/$project"
done

echo
echo "All ESP PLANTS PlatformIO builds completed."
