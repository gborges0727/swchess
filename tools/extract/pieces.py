"""Decode the walking and rotation sprites in the twelve piece DLLs.

Each piece DLL has zero code segments. It is a resource container. Every bitmap
resource holds a BITMAPINFOHEADER, a palette and the same escape-byte RLE the
ANX files use, so the ANX decoder reads them without change. A resource name
such as AT_S002 names the piece, the direction and the frame.

The matching INI lists a movement step for every frame, grouped by direction.
The manifest keeps the INI text next to the resource that the frame names, and
it flags any direction where the INI count and the resource count disagree.
"""

import hashlib
import json
import os
import re

from . import ne, png
from .ini import IniFile, as_int
from ..reference import anx

PIECES = ["AT", "BF", "C3", "CB", "DV", "EM", "LO", "LS", "R2", "SP", "ST", "YO"]

# The direction sections every piece INI can carry, in the order they appear.
DIRECTIONS = ["S", "N", "E", "W", "NE", "NW", "SE", "SW", "R", "US", "UN", "DN", "DS"]

NAME_PATTERN = re.compile(r"^(?P<piece>[A-Z0-9]{2})_(?P<direction>[A-Z]{1,2})(?P<frame>\d+)$")


def classify(name):
    """Split a resource name such as AT_S002 into piece, direction and frame."""
    match = NAME_PATTERN.match(str(name))
    if not match:
        return None
    return {
        "piece": match.group("piece"),
        "direction": match.group("direction"),
        "frame": int(match.group("frame")),
        "digits": len(match.group("frame")),
    }


def extract(cd_dir, out_dir):
    """Write every piece folder under out_dir and return catalog data."""
    os.makedirs(out_dir, exist_ok=True)
    bitmap_entries = []
    piece_summaries = []
    mismatches = []

    for piece in PIECES:
        dll_path = os.path.join(cd_dir, piece + ".DLL")
        blob, resources = ne.read_file(dll_path)
        bitmaps = [r for r in resources if r.type_id == ne.RT_BITMAP]
        folder = os.path.join(out_dir, piece)
        os.makedirs(folder, exist_ok=True)

        by_direction = {}
        for res in bitmaps:
            rec = anx.decode_record(blob, res.offset, res.offset + res.length)
            if not rec["complete"]:
                raise ValueError(f"{piece}.DLL resource {res.name_id} decoded short")
            name = f"{res.name_id}.png"
            rows = anx.to_rgba(rec)
            png.write_rgba(os.path.join(folder, name), rec["width"], rec["height"], rows)
            with open(os.path.join(folder, name), "rb") as fh:
                digest = hashlib.sha256(fh.read()).hexdigest()
            parsed = classify(res.name_id)
            entry = {
                "piece": piece,
                "source": piece + ".DLL",
                "resource_name": res.name_id,
                "resource_offset": res.offset,
                "resource_length": res.length,
                "direction": parsed["direction"] if parsed else None,
                "frame": parsed["frame"] if parsed else None,
                "width": rec["width"],
                "height": rec["height"],
                "escape": rec["escape"],
                "compression": rec["compression"],
                "palette_entries": len(rec["palette"]) // 4,
                "palette_sha256": hashlib.sha256(rec["palette"]).hexdigest(),
                "transparent_index": 0,
                "output": f"pieces/{piece}/{name}",
                "output_sha256": digest,
            }
            bitmap_entries.append(entry)
            if parsed:
                by_direction.setdefault(parsed["direction"], []).append(entry)

        ini = IniFile(os.path.join(cd_dir, piece + ".INI"))
        sequences = []
        for direction in DIRECTIONS:
            section = ini.section(direction)
            if section is None and direction not in by_direction:
                continue
            raw = section.raw() if section else {}
            declared = as_int(raw.get("count"))
            steps = []
            for key, value in raw.items():
                if key.lower() == "count":
                    continue
                parts = [p.strip() for p in value.split(",")]
                steps.append(
                    {
                        "key": key,
                        "frame": as_int(key),
                        "dx": as_int(parts[0]) if parts else None,
                        "dy": as_int(parts[1]) if len(parts) > 1 else None,
                        "raw": value,
                    }
                )
            frames = sorted(by_direction.get(direction, []), key=lambda e: e["frame"])
            sequence = {
                "direction": direction,
                "kind": "rotation" if direction == "R" else "walk",
                "declared_count": declared,
                "ini_step_count": len(steps),
                "bitmap_count": len(frames),
                "steps": steps,
                "bitmaps": [
                    {
                        "resource_name": f["resource_name"],
                        "frame": f["frame"],
                        "image": os.path.basename(f["output"]),
                        "width": f["width"],
                        "height": f["height"],
                    }
                    for f in frames
                ],
            }
            if declared is not None and (declared != len(frames) or declared != len(steps)):
                reasons = []
                if declared != len(frames):
                    reasons.append("the INI count differs from the number of bitmaps in the DLL")
                if declared != len(steps):
                    reasons.append("the INI count differs from the number of step lines in the section")
                note = {
                    "piece": piece,
                    "direction": direction,
                    "declared_count": declared,
                    "ini_step_count": len(steps),
                    "bitmap_count": len(frames),
                    "note": " and ".join(reasons),
                }
                sequence["count_mismatch"] = note
                mismatches.append(note)
            sequences.append(sequence)

        manifest = {
            "piece": piece,
            "dll": piece + ".DLL",
            "ini": piece + ".INI",
            "bitmap_count": len(bitmaps),
            "sequences": sequences,
        }
        with open(os.path.join(folder, "manifest.json"), "w") as fh:
            json.dump(manifest, fh, indent=1)

        piece_summaries.append(
            {
                "piece": piece,
                "bitmap_count": len(bitmaps),
                "directions": [s["direction"] for s in sequences],
                "output": f"pieces/{piece}/manifest.json",
            }
        )

    return {
        "pieces": piece_summaries,
        "bitmaps": bitmap_entries,
        "bitmap_count": len(bitmap_entries),
        "count_mismatches": mismatches,
    }
