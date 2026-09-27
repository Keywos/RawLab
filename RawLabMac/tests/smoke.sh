#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/RawLab Mac.app/Contents/MacOS/RawLabMac"
test -x "$APP" || { echo 'FAIL: RawLab Mac executable missing'; exit 1; }
OUT="$(mktemp -d /tmp/rawlab-mac-smoke.XXXXXX)"
"$APP" --smoke "$ROOT/RawLab/RawLab/Resources/Samples/DSC09067.ARW" "$OUT/sony"
if [ -f "$ROOT/lutools/examples/DJI_20250602164503_0444_D.DNG" ]; then
    "$APP" --smoke "$ROOT/lutools/examples/DJI_20250602164503_0444_D.DNG" "$OUT/dji"
fi
if "$APP" --smoke "$OUT/missing.raw" "$OUT/missing"; then
    echo 'FAIL: missing RAW accepted'; exit 1
fi
echo "PASS: Mac smoke outputs in $OUT"
