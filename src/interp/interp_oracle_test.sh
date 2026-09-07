#!/bin/bash
# Checks swchess-interpolate against what `python3 -m tools.interp` produced.
#
#   interp_oracle_test.sh <swchess-interpolate> <assets dir> <scratch dir> \
#       <rife binary> <model dir>
#
# It interpolates BBWB and BNWN again and compares every frame with the ones
# already in the cache. RIFE gives the same answer twice on the same GPU, so
# the frames have to match byte for byte. The manifest is compared with the
# three fields that record the run itself blanked out: the repository commit,
# the example RIFE command, which names the working directory of this run, and
# the wall clock time.
set -uo pipefail

if [ $# -ne 5 ]; then
  echo "usage: $0 <swchess-interpolate> <assets dir> <scratch dir> <rife> <model>" >&2
  exit 2
fi

interpolate="$1"
assets="$2"
scratch="$3"
rife="$4"
model="$5"

# The stored manifests name their input as 'assets/captures/NAME/resolved.json'
# because the Python run started at the repo root. Start there too so the paths
# in the new manifests read the same.
root=$(cd "$(dirname "$assets")" && pwd)
name=$(basename "$assets")
cd "$root" || exit 1

rm -rf "$scratch"
mkdir -p "$scratch"

blank() {
  sed -e 's/"commit": .*/"commit": null,/' \
      -e 's#"rife_example": .*#"rife_example": "",#' \
      -e 's/"wall_seconds": .*/"wall_seconds": 0/' "$1"
}

status=0
for capture in BBWB BNWN; do
  stored="$name/captures/$capture/interp60"
  fresh="$scratch/$capture"

  if ! "$interpolate" --capture "$capture" --assets "$name" --out "$fresh" \
       --rife "$rife" --model "$model" > "$scratch/$capture.log" 2>&1; then
    echo "FAIL $capture: swchess-interpolate exited nonzero"
    tail -20 "$scratch/$capture.log"
    status=1
    continue
  fi

  # The contact sheet comes from tools/interp/contact.py, which this helper
  # does not draw.
  differences=$(diff -rq "$stored" "$fresh" 2>&1 \
    | grep -v 'contact\.png' \
    | grep -v 'manifest\.json')
  if [ -n "$differences" ]; then
    echo "FAIL $capture: the frames differ from the Python run"
    echo "$differences" | head -20
    status=1
    continue
  fi
  frames=$(find "$fresh" -name 'frame*.png' | wc -l | tr -d ' ')
  echo "ok   $capture: all $frames frames match the Python run byte for byte"

  if ! diff <(blank "$stored/manifest.json") <(blank "$fresh/manifest.json") \
       > "$scratch/$capture.manifest"; then
    echo "FAIL $capture: the manifest differs beyond the commit, the example command and the run time"
    head -30 "$scratch/$capture.manifest"
    status=1
    continue
  fi
  echo "ok   $capture: the manifest matches apart from the commit, the example command and the run time"

  if ! "$interpolate" --check "$fresh" > "$scratch/$capture.check" 2>&1; then
    echo "FAIL $capture: the fresh output does not pass its own checks"
    cat "$scratch/$capture.check"
    status=1
    continue
  fi
  echo "ok   $capture: the fresh output passes every check"
done

if [ $status -eq 0 ]; then
  rm -rf "$scratch"
  echo "PASS"
fi
exit $status
