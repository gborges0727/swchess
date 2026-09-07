#!/bin/bash
# Checks swchess-extract against the cache `python3 -m tools.extract` wrote.
#
#   export_oracle_test.sh <swchess-extract> <cd dir> <assets dir> <scratch dir>
#
# It extracts the CD again into the scratch directory and compares every file
# it produced with the file of the same name in the existing cache. The cache
# also holds interp60 and walk60 folders that the interpolation lane writes,
# and those have no counterpart in a fresh extraction, so the comparison runs
# from the fresh side outward rather than both ways.
#
# catalog.json is the one file allowed to differ, and only in the two fields
# that record the run itself: the second it started and the directory it wrote
# to.
set -uo pipefail

if [ $# -ne 4 ]; then
  echo "usage: $0 <swchess-extract> <cd dir> <assets dir> <scratch dir>" >&2
  exit 2
fi

extract="$1"
cd_dir="$2"
assets="$3"
scratch="$4"

rm -rf "$scratch" "$scratch.log"
mkdir -p "$(dirname "$scratch")"

if ! "$extract" --cd "$cd_dir" --out "$scratch" > "$scratch.log" 2>&1; then
  echo "FAIL swchess-extract exited nonzero"
  tail -20 "$scratch.log"
  exit 1
fi

missing=0
differ=0
same=0
first=''
while IFS= read -r file; do
  relative="${file#"$scratch"/}"
  if [ "$relative" = 'catalog.json' ]; then
    continue
  fi
  if [ ! -f "$assets/$relative" ]; then
    missing=$((missing + 1))
    [ -z "$first" ] && first="$relative is not in the Python cache"
    continue
  fi
  if cmp -s "$file" "$assets/$relative"; then
    same=$((same + 1))
  else
    differ=$((differ + 1))
    [ -z "$first" ] && first="$relative differs"
  fi
done < <(find "$scratch" -type f)

if [ $missing -ne 0 ] || [ $differ -ne 0 ]; then
  echo "FAIL $differ files differ and $missing are not in the Python cache, first: $first"
  exit 1
fi
echo "ok   all $same extracted files match the Python cache byte for byte"

# catalog.json is compared with the three fields that record the run itself
# blanked out.
blank() {
  sed -e 's/"generated_unix_time": [0-9]*/"generated_unix_time": 0/' \
      -e 's#"output_directory": "[^"]*"#"output_directory": ""#' \
      -e 's#"cd_directory": "[^"]*"#"cd_directory": ""#' "$1"
}

if ! diff <(blank "$assets/catalog.json") <(blank "$scratch/catalog.json") > "$scratch.catalog"; then
  echo "FAIL catalog.json differs beyond the run time and the output directory"
  head -40 "$scratch.catalog"
  exit 1
fi
echo "ok   catalog.json matches apart from the run time and the output directory"

rm -rf "$scratch" "$scratch.log" "$scratch.catalog"
echo "PASS"
