#!/usr/bin/env python3
"""Builds the Windows icon from packaging/icon.png.

    python3 packaging/windows/make-ico.py packaging/icon.png packaging/windows/swchess.ico

The .ico it writes is checked in, so a build never runs this script. Run it
again when the source artwork changes. It needs Pillow.
"""
import sys

from PIL import Image

SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    source, target = sys.argv[1], sys.argv[2]
    Image.open(source).convert("RGBA").save(target, format="ICO", sizes=SIZES)
    print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
