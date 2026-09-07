#!/bin/bash
# Checks swchess-extract against the cache `python3 -m tools.extract` wrote.
#
#   export_oracle_test.sh <swchess-extract> <cd dir> <assets dir> <scratch dir>
#
# It extracts the CD again into the scratch directory and compares every file
# with the existing cache. Two differences are expected and allowed. The
# interpolation lane writes interp60 and walk60 folders that the extractor
# never produces, and catalog.json records the moment of the run and the
# output directory, which differ by definition. Everything else has to match
# byte for byte.
set -uo pipefail

if [ $# -ne 4 ]; then
  echo "usage: $0 <swchess-extract> <cd dir> <assets dir> <scratch dir>" >&2
  exit 2
fi

extract="$1"
cd_dir="$2"
assets="$3"
scratch="$4"

rm -rf "$scratch"
mkdir -p "$(dirname "$scratch")"

if ! "$extract" --cd "$cd_dir" --out "$scratch" > "$scratch.log" 2>&1; then
  echo "FAIL swchess-extract exited nonzero"
  tail -20 "$scratch.log"
  exit 1
fi

# Every file except catalog.json must be identical. The two folders the
# interpolation lane writes have no counterpart in a fresh extraction.
differences=$(diff -rq "$assets" "$scratch" 2>&1 \
  | grep -v '/interp60' \
  | grep -v '/walk60' \
  | grep -v 'catalog\.json')

if [ -n "$differences" ]; then
  echo "FAIL the two caches differ"
  echo "$differences" | head -40
  exit 1
fi
echo "ok   every extracted file matches the Python cache byte for byte"

# catalog.json is compared with the two fields that record the run itself
# blanked out.
blank() {
  sed -e 's/"generated_unix_time": [0-9]*/"generated_unix_time": 0/' \
      -e 's#"output_directory": "[^"]*"#"output_directory": ""#' "$1"
}

if ! diff <(blank "$assets/catalog.json") <(blank "$scratch/catalog.json") > "$scratch.catalog"; then
  echo "FAIL catalog.json differs beyond the run time and the output directory"
  head -40 "$scratch.catalog"
  exit 1
fi
echo "ok   catalog.json matches apart from the run time and the output directory"

rm -rf "$scratch" "$scratch.log" "$scratch.catalog"
echo "PASS"
