"""Decode the 72 capture animations and write one folder per capture.

Every capture owns an ANX file such as BBWB.ANX and a section in a piece INI
such as BB.INI. The ANX stores each distinct bitmap once, so a folder holds one
RGBA PNG per distinct record. timeline.json then lists the frames in play order
and points each one at its PNG, so a repeated pose keeps its own timeline entry.
"""

import glob
import hashlib
import json
import os
import re

from . import cues, png
from .ini import IniFile, as_int
from ..reference import anx

# Every art= value ends in a three digit frame number, as in BF_C3003.BMP for
# frame 3 of the BF_C3 sequence. The stem is greedy so the number stays three
# digits even when the stem itself ends in a digit.
ART_PATTERN = re.compile(r"^(?P<stem>.+?)(?P<frame>\d{3})\.(?P<ext>\w+)$", re.I)


def parse_art(value):
    """Split an art= path into its folder, its file name and its frame number."""
    if not value:
        return None
    tail = value.replace("/", "\\").split("\\")
    name = tail[-1]
    folder = tail[-2] if len(tail) > 1 else None
    match = ART_PATTERN.match(name)
    out = {"path": value, "folder": folder, "file": name, "frame": None, "stem": None}
    if match:
        out["frame"] = int(match.group("frame"))
        out["stem"] = match.group("stem")
    return out


def ini_for(capture):
    """BBWB comes from BB.INI, WNBR comes from WN.INI."""
    return capture[:2].upper() + ".INI"


def extract(cd_dir, out_dir, sound_index, frame_delay_default):
    """Write every capture folder under out_dir and return catalog data."""
    os.makedirs(out_dir, exist_ok=True)
    ini_cache = {}
    captures = []
    record_entries = []
    unresolved = []
    aliases = []
    total_records = 0
    total_timeline = 0

    for path in sorted(glob.glob(os.path.join(cd_dir, "*.ANX"))):
        capture = os.path.splitext(os.path.basename(path))[0].upper()
        ini_name = ini_for(capture)
        if ini_name not in ini_cache:
            ini_cache[ini_name] = IniFile(os.path.join(cd_dir, ini_name))
        ini = ini_cache[ini_name]

        with open(path, "rb") as fh:
            blob = fh.read()
        count, offsets, records = anx.parse(blob)

        folder = os.path.join(out_dir, capture)
        os.makedirs(folder, exist_ok=True)

        images = {}
        for off in sorted(records):
            rec = records[off]
            if not rec["complete"]:
                raise ValueError(f"{capture} record {off:#x} decoded short")
            name = f"rec{off:08x}.png"
            rows = anx.to_rgba(rec)
            png.write_rgba(os.path.join(folder, name), rec["width"], rec["height"], rows)
            with open(os.path.join(folder, name), "rb") as fh:
                digest = hashlib.sha256(fh.read()).hexdigest()
            images[off] = name
            record_entries.append(
                {
                    "capture": capture,
                    "source": os.path.basename(path),
                    "record_offset": off,
                    "file_offset": anx.BASE + off,
                    "width": rec["width"],
                    "height": rec["height"],
                    "bit_count": rec["bit_count"],
                    "escape": rec["escape"],
                    "compression": rec["compression"],
                    "palette_entries": len(rec["palette"]) // 4,
                    "palette_sha256": hashlib.sha256(rec["palette"]).hexdigest(),
                    "transparent_index": 0,
                    "output": f"captures/{capture}/{name}",
                    "output_sha256": digest,
                }
            )
            total_records += 1

        section = ini.section(capture)
        keys = list(section.keys) if section else []
        offset_section = ini.section(capture + "_OFFSET")
        placement = {
            "raw": offset_section.raw() if offset_section else {},
            "x": as_int(offset_section.get("x"), 0) if offset_section else 0,
            "y": as_int(offset_section.get("y"), 0) if offset_section else 0,
            "hold": as_int(offset_section.get("hold")) if offset_section else None,
        }

        entries = []
        recovered_keys = 0
        for index in range(count):
            key = keys[index] if index < len(keys) else None
            key_source = "listed"
            if key is None:
                # WN.INI lists only 71 keys under [WNBR] while WNBR.ANX holds 80
                # frames and WN.INI still carries [WNBR_072] through [WNBR_080].
                # Fall back to the naming the file already uses so no frame
                # loses its INI fields.
                guess = f"{capture}_{index + 1:03d}"
                if ini.section(guess) is not None:
                    key = guess
                    key_source = "recovered from the frame section name"
                    recovered_keys += 1
                else:
                    key_source = "missing"
            frame = ini.section(key) if key else None
            raw = frame.raw() if frame else {}
            wav_raw = frame.get("wav") if frame else None
            sound = None
            if wav_raw:
                sound = cues.resolve(wav_raw, sound_index, capture, ini_name, key)
                if sound["status"] == "missing":
                    unresolved.append(sound)
                elif sound["status"] == "alias":
                    aliases.append(sound)
            off = offsets[index]
            rec = records[off]
            entries.append(
                {
                    "index": index,
                    "ini_key": key,
                    "ini_key_source": key_source,
                    "frame_number": index + 1,
                    "record_offset": off,
                    "image": images[off],
                    "width": rec["width"],
                    "height": rec["height"],
                    "x": as_int(raw.get("x"), 0),
                    "y": as_int(raw.get("y"), 0),
                    "pause": as_int(raw.get("pause")),
                    "hold": as_int(raw.get("hold")),
                    "art": parse_art(raw.get("art")),
                    "sound": sound,
                    "ini": raw,
                }
            )
        total_timeline += len(entries)

        timeline = {
            "capture": capture,
            "anx_file": os.path.basename(path),
            "ini_file": ini_name,
            "ini_section": capture,
            "frame_count_in_anx": count,
            "ini_key_count": len(keys),
            "keys_match_anx_count": len(keys) == count,
            "keys_recovered_from_section_names": recovered_keys,
            "distinct_records": len(records),
            "reused_references": count - len(records),
            "frame_delay_ms": frame_delay_default,
            "capture_offset": placement,
            "entries": entries,
        }
        with open(os.path.join(folder, "timeline.json"), "w") as fh:
            json.dump(timeline, fh, indent=1)

        captures.append(
            {
                "capture": capture,
                "anx_file": os.path.basename(path),
                "ini_file": ini_name,
                "distinct_records": len(records),
                "timeline_entries": count,
                "reused_references": count - len(records),
                "keys_match_anx_count": len(keys) == count,
                "keys_recovered_from_section_names": recovered_keys,
                "output": f"captures/{capture}/timeline.json",
            }
        )

    return {
        "captures": captures,
        "records": record_entries,
        "distinct_record_count": total_records,
        "timeline_entry_count": total_timeline,
        "capture_count": len(captures),
        "sound_aliases": aliases,
        "unresolved_sounds": unresolved,
    }
