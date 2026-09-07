#!/bin/bash
# Checks swchess-interpolate against what `python3 -m tools.interp` produced.
#
#   interp_oracle_test.sh <swchess-interpolate> <assets dir> <scratch dir> \
#       <rife binary> <model dir>
#
# It interpolates BBWB and BNWN again and compares every frame with the ones
# already in the cache. RIFE gives the same answer twice on the same GPU, so
# the frames have to match byte for byte. The manifest is compared with four
# fields blanked out, because each one records the run rather than its result.
# Those are the repository commit, the example RIFE command, which names this
# run's working directory, the wall clock time, and the hash of the RIFE
# binary. That last hash changes every time tools/rife/build.sh runs again.
# The frames matched across two such builds, one targeting macOS 26 and one
# targeting macOS 11, so a different binary hash is not a different picture.
# The test says out loud whether the two runs used the same binary.
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
      -e 's/"binary_sha256": .*/"binary_sha256": "",/' \
      -e 's/"wall_seconds": .*/"wall_seconds": 0/' "$1"
}

hashOf() {
  grep -o '"binary_sha256": "[a-f0-9]*"' "$1" | head -1
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
  echo "ok   $capture: the manifest matches apart from the four fields that record the run"
  if [ "$(hashOf "$stored/manifest.json")" = "$(hashOf "$fresh/manifest.json")" ]; then
    echo "     both runs used the same RIFE binary"
  else
    echo "     the two runs used different RIFE builds and produced the same frames"
  fi

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
