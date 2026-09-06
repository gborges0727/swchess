#!/bin/bash
# Prove the built rife-ncnn-vulkan interpolates on this Mac.
# Draws a white square at x=20 and the same square at x=60 on black,
# asks for the frame halfway between them, and checks the square landed
# near x=40. Prints PASS or FAIL.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLS="$ROOT/tools/rife"
BIN="$ROOT/.cache/rife/bin/rife-ncnn-vulkan"
WORK="$ROOT/.cache/rife/smoke"
PY="${PYTHON:-/opt/homebrew/bin/python3}"

if [ ! -x "$BIN" ]; then
  echo "FAIL: $BIN is missing, run tools/rife/build.sh first"
  exit 1
fi

rm -rf "$WORK"
mkdir -p "$WORK"

RIFE_TOOLS="$TOOLS" "$PY" - "$WORK" <<'PYEOF'
import sys, os
sys.path.insert(0, os.environ["RIFE_TOOLS"])
import png_util

W = H = 128
SIDE = 20

def square(cx):
    px = bytearray(W * H * 3)
    for y in range(H // 2 - SIDE // 2, H // 2 + SIDE // 2):
        for x in range(cx - SIDE // 2, cx + SIDE // 2):
            i = (y * W + x) * 3
            px[i:i + 3] = b"\xff\xff\xff"
    return bytes(px)

work = sys.argv[1]
png_util.write_rgb(os.path.join(work, "a.png"), W, H, square(20))
png_util.write_rgb(os.path.join(work, "b.png"), W, H, square(60))
PYEOF
rc=$?
if [ $rc -ne 0 ]; then
  echo "FAIL: could not write the test images"
  exit 1
fi

echo "running inference"
start=$(date +%s.%N)
"$BIN" -m "$ROOT/.cache/rife/bin/rife-v4.6" \
  -0 "$WORK/a.png" -1 "$WORK/b.png" -s 0.5 -o "$WORK/mid.png"
rc=$?
end=$(date +%s.%N)
elapsed=$(echo "$end - $start" | bc)
printf 'inference took %.2f s\n' "$elapsed"

if [ $rc -ne 0 ] || [ ! -f "$WORK/mid.png" ]; then
  echo "FAIL: rife-ncnn-vulkan exited $rc and wrote no mid.png"
  exit 1
fi

RIFE_TOOLS="$TOOLS" "$PY" - "$WORK/mid.png" <<'PYEOF'
import sys, os
sys.path.insert(0, os.environ["RIFE_TOOLS"])
import png_util

w, h, reds = png_util.read_rgb(sys.argv[1])
cx = png_util.centroid_x(w, h, reds)
print("centroid x = %.2f, expected 40 within 8" % cx)
sys.exit(0 if abs(cx - 40.0) <= 8.0 else 2)
PYEOF
rc=$?

if [ $rc -eq 0 ]; then
  echo "PASS"
  exit 0
fi
echo "FAIL: the interpolated square is not near x=40"
exit 1
