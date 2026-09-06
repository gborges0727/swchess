#!/bin/sh
# Builds the application icon at build time.
#
#   packaging/make-icns.sh <source.png or ""> <out.icns> <work directory>
#
# The source is assets/ui/STLGO16.png, which tools/extract writes from the
# original CD. That file is not in the repository, so this script draws a
# plain blue square instead whenever the source is missing. Nothing generated
# from the original artwork is ever committed.
set -eu

source_png=${1:-}
out_icns=$2
work_dir=$3

rm -rf "$work_dir"
mkdir -p "$work_dir"
square="$work_dir/square.png"

if [ -n "$source_png" ] && [ -f "$source_png" ]; then
    # sips keeps the aspect ratio, so pad the logo onto a 1024 by 1024 canvas.
    sips -s format png "$source_png" --out "$square" >/dev/null
    sips -Z 1024 "$square" >/dev/null
    sips -p 1024 1024 "$square" >/dev/null
else
    python3 - "$square" <<'PY'
import struct, sys, zlib

size = 1024
red, green, blue = 0x1b, 0x63, 0x73
row = bytes([0]) + bytes([red, green, blue]) * size
raw = row * size


def chunk(tag, body):
    head = tag + body
    return struct.pack(">I", len(body)) + head + struct.pack(">I", zlib.crc32(head))


png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(raw, 9))
png += chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
fi

iconset="$work_dir/icon.iconset"
mkdir -p "$iconset"
for edge in 16 32 128 256 512; do
    sips -z "$edge" "$edge" "$square" --out "$iconset/icon_${edge}x${edge}.png" >/dev/null
    double=$((edge * 2))
    sips -z "$double" "$double" "$square" --out "$iconset/icon_${edge}x${edge}@2x.png" >/dev/null
done

mkdir -p "$(dirname "$out_icns")"
iconutil -c icns "$iconset" -o "$out_icns"
