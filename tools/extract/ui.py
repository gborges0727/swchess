"""Write the title screens and the letter artwork out of TITLERES.DLL.

TITLERES.DLL holds four bitmap resources. STARTITL, LEGAL and STLGO16 are 640 by
480. LEGFONT is 640 by 253 and starts at file offset 0xe2c00. All four are plain
uncompressed 8-bit DIBs, so they need no RLE decoding.
"""

import hashlib
import json
import os

from . import dib, ne, png
from .cdfs import cd_path


def extract(cd_dir, out_dir):
    """Write every TITLERES bitmap as an opaque PNG and return catalog data."""
    os.makedirs(out_dir, exist_ok=True)
    path = cd_path(cd_dir, "TITLERES.DLL")
    blob, resources = ne.read_file(path)
    entries = []
    for res in resources:
        if res.type_id != ne.RT_BITMAP:
            continue
        bitmap = dib.parse_dib(blob, res.offset)
        name = f"{res.name_id}.png"
        out = os.path.join(out_dir, name)
        png.write_rgb(out, bitmap["width"], bitmap["height"], dib.to_rgb_rows(bitmap))
        with open(out, "rb") as fh:
            digest = hashlib.sha256(fh.read()).hexdigest()
        entries.append(
            {
                "source": "TITLERES.DLL",
                "resource_name": res.name_id,
                "resource_offset": res.offset,
                "resource_offset_hex": f"{res.offset:#x}",
                "resource_length": res.length,
                "width": bitmap["width"],
                "height": bitmap["height"],
                "bit_count": bitmap["bit_count"],
                "output": f"ui/{name}",
                "output_sha256": digest,
            }
        )
    with open(os.path.join(out_dir, "manifest.json"), "w") as fh:
        json.dump({"source": "TITLERES.DLL", "bitmaps": entries}, fh, indent=1)
    return {"ui_bitmaps": entries, "ui_bitmap_count": len(entries)}
