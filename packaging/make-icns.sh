#!/bin/sh
# Builds the application icon at build time.
#
#   packaging/make-icns.sh <ignored> <out.icns> <work directory>
#
# The picture comes from packaging/icon.png, which this repository owns.
# packaging/make-icon-png.py draws it. The first argument is ignored. It used
# to name a picture decoded from the game CD, and the release must not carry
# anything from the CD. CMake still passes that argument.
set -eu

out_icns=$2
work_dir=$3

here=$(cd "$(dirname "$0")" && pwd)
source_png="$here/icon.png"

if [ ! -f "$source_png" ]; then
    echo "the icon is missing from $source_png" >&2
    echo "run: python3 packaging/make-icon-png.py packaging/icon.png" >&2
    exit 1
fi

rm -rf "$work_dir"
mkdir -p "$work_dir"
square="$work_dir/square.png"
sips -s format png "$source_png" --out "$square" >/dev/null

iconset="$work_dir/icon.iconset"
mkdir -p "$iconset"
for edge in 16 32 128 256 512; do
    sips -z "$edge" "$edge" "$square" --out "$iconset/icon_${edge}x${edge}.png" >/dev/null
    double=$((edge * 2))
    sips -z "$double" "$double" "$square" --out "$iconset/icon_${edge}x${edge}@2x.png" >/dev/null
done

mkdir -p "$(dirname "$out_icns")"
iconutil -c icns "$iconset" -o "$out_icns"
