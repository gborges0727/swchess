"""Decode the 72 capture animations and write one folder per capture.

Every capture owns an ANX file such as BBWB.ANX and a section in a piece INI
such as BB.INI. The ANX stores each distinct bitmap once, so a folder holds one
RGBA PNG per distinct record. timeline.json lists every timeline entry with the
fields it came from, and resolved.json lists only the poses the original draws,
each with the millisecond it appears at.

docs/research/capture-player.md is the source for the rules here. The timeline
index and the ANX record index are the same number. Each frame's position comes
from table 2 of the ANX plus the capture's [XXXX_OFFSET] x and y. The player
decodes pose 0 but never draws it, shows every later pose frame_delay apart, and
holds the last pose before erasing it.
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

# FUN_1058_0ce6 loads SPACE256.BMP or THRON256.BMP as the backdrop, and both are
# 640 by 480. FUN_1058_0150 clips every frame to that rectangle.
CANVAS = {"x": 0, "y": 0, "w": 640, "h": 480}

# The defaults GetPrivateProfileInt passes at 1058:11bf, 1058:11d9 and
# 1058:1221 when the [XXXX_OFFSET] section leaves a key out.
DEFAULT_OFFSET_X = 215
DEFAULT_OFFSET_Y = 100
DEFAULT_HOLD_MS = 1000

# pause= picks one of three sound behaviours at 1058:0a44 through 1058:0a9d.
SOUND_MODES = {0: "async", 1: "sync", 2: "wait_previous"}


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


def resolve_final_wav(raw, sound_index):
    """Resolve the [XXXX_OFFSET] wav= that plays after the hold.

    FUN_1008_1519 tries the name as a file on disk and then as a WAVE resource,
    so this lookup sees the loose WAV files as well as SWCAUDIO.DLL.
    """
    if not raw:
        return None
    name = raw.strip().upper()
    for source in ("files", "resources"):
        if name in sound_index[source]:
            return {
                "name": name,
                "raw": raw,
                "source": source,
                "duration_ms": sound_index[source][name] or 0,
            }
    return {"name": name, "raw": raw, "source": None, "duration_ms": 0,
            "status": "silent_in_original"}


def build_timing(entries, frame_delay, hold_ms, final_wav):
    """Walk the timeline the way FUN_1058_0a0a does and time every pose.

    Pose 0 is decoded but never drawn, yet its iteration still spends a full
    frame_delay (and blocks on its own sound the same as any other pose)
    before the loop moves on, so every later pose lands frame_delay later than
    it would if pose 0's iteration were skipped outright. This mirrors
    src/anim/capture.cpp's loop exactly: the per-iteration step is
    max(block, frame_delay) whether or not that iteration draws.

    Returns the drawn poses, the time the last pose is erased, how much time
    the blocking sounds added, and pose 0's own sound (if it has one) as
    pre_sounds.
    """
    poses = []
    pre_sounds = []
    clock = 0
    last_start = 0
    last_step = 0
    pending_end = None
    blocking_count = 0
    blocking_ms = 0

    for entry in entries:
        start = clock
        sound = None
        sound_start = clock
        cue = entry["sound"]
        if cue and cue["status"] == "resolved":
            duration = cue["duration_ms"] or 0
            pause = entry["pause"] or 0
            # A frame with a sound first waits out an earlier pause=2 sound.
            if pending_end is not None:
                if pending_end > clock:
                    blocking_count += 1
                    blocking_ms += pending_end - clock
                    clock = pending_end
                pending_end = None
            sound_start = clock
            if pause == 1:
                # sndPlaySound with SND_SYNC blocks for the whole sound.
                clock += duration
                blocking_count += 1
                blocking_ms += duration
            elif pause == 2:
                pending_end = clock + duration
            sound = {
                "name": cue["resolved"],
                "mode": SOUND_MODES.get(pause, "async"),
                "duration_ms": duration,
            }

        if entry["index"] != 0:
            record = dict(entry["pose"])
            record["index"] = entry["index"]
            record["t_ms"] = clock
            record["sound"] = sound
            poses.append(record)
        elif sound:
            pre_sounds.append(
                {
                    "t_ms": sound_start,
                    "name": sound["name"],
                    "mode": sound["mode"],
                    "duration_ms": sound["duration_ms"],
                }
            )

        # Every iteration, drawn or not, spends max(block, frame_delay)
        # before the next one starts.
        last_start = start
        last_step = max(clock - start, frame_delay)
        clock = start + last_step

    if entries:
        loop_end = last_start + last_step
        end_ms = max(loop_end, last_start + hold_ms)
    else:
        end_ms = hold_ms
    if final_wav:
        end_ms += final_wav["duration_ms"]
    return poses, end_ms, blocking_count, blocking_ms, pre_sounds


def extract(cd_dir, out_dir, sound_index, frame_delay_default):
    """Write every capture folder under out_dir and return catalog data."""
    os.makedirs(out_dir, exist_ok=True)
    ini_cache = {}
    captures = []
    record_entries = []
    unresolved = []
    total_records = 0
    total_timeline = 0
    total_poses = 0
    blocking_captures = 0
    summary = []

    for path in sorted(glob.glob(os.path.join(cd_dir, "*.ANX"))):
        capture = os.path.splitext(os.path.basename(path))[0].upper()
        ini_name = ini_for(capture)
        if ini_name not in ini_cache:
            ini_cache[ini_name] = IniFile(os.path.join(cd_dir, ini_name))
        ini = ini_cache[ini_name]

        with open(path, "rb") as fh:
            blob = fh.read()
        count, offsets, records = anx.parse(blob)
        positions = anx.positions(blob)
        lengths = anx.compressed_lengths(blob)

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
        # FUN_1058_0e15 runs one iteration per key name and checks the counter
        # against the ANX frame count, so the key list is the timeline. WNBR.ANX
        # declares 80 records while [WNBR] lists 71 keys, and the last nine
        # records never play.
        if len(keys) > count:
            raise ValueError(
                f"{capture}: {len(keys)} INI keys but only {count} ANX records"
            )
        offset_section = ini.section(capture + "_OFFSET")
        offset_raw = offset_section.raw() if offset_section else {}
        offset_x = as_int(offset_raw.get("x"), DEFAULT_OFFSET_X)
        offset_y = as_int(offset_raw.get("y"), DEFAULT_OFFSET_Y)
        hold_ms = as_int(offset_raw.get("hold"), DEFAULT_HOLD_MS)
        final_wav = resolve_final_wav(offset_raw.get("wav"), sound_index)
        placement = {
            "raw": offset_raw,
            "x": offset_x,
            "y": offset_y,
            "x_from_default": "x" not in {k.lower() for k in offset_raw},
            "y_from_default": "y" not in {k.lower() for k in offset_raw},
            "hold_ms": hold_ms,
            "wav": final_wav,
        }

        entries = []
        for index, key in enumerate(keys):
            frame = ini.section(key)
            raw = frame.raw() if frame else {}
            wav_raw = frame.get("wav") if frame else None
            sound = None
            if wav_raw:
                sound = cues.resolve(
                    wav_raw, sound_index["resources"], capture, ini_name, key
                )
                if sound["status"] == "silent_in_original":
                    unresolved.append(sound)
            off = offsets[index]
            rec = records[off]
            table_x, table_y = positions[index]
            entries.append(
                {
                    "index": index,
                    "ini_key": key,
                    "frame_number": index + 1,
                    "record_offset": off,
                    "image": images[off],
                    "width": rec["width"],
                    "height": rec["height"],
                    "compressed_length": lengths[index],
                    "table_x": table_x,
                    "table_y": table_y,
                    "x": table_x + offset_x,
                    "y": table_y + offset_y,
                    "ini_x": as_int(raw.get("x")),
                    "ini_y": as_int(raw.get("y")),
                    # Only wav= and pause= reach the player, and pause= counts
                    # for nothing without a wav= beside it.
                    "pause": as_int(raw.get("pause")),
                    "art": parse_art(raw.get("art")),
                    "sound": sound,
                    "ini": raw,
                    "pose": {
                        "image": images[off],
                        "x": table_x + offset_x,
                        "y": table_y + offset_y,
                        "w": rec["width"],
                        "h": rec["height"],
                    },
                }
            )
        total_timeline += len(entries)

        poses, end_ms, blocking_count, blocking_ms, pre_sounds = build_timing(
            entries, frame_delay_default, hold_ms, final_wav
        )
        total_poses += len(poses)
        nominal = (
            max(len(entries) - 1, 0) * frame_delay_default
            + hold_ms
            + (final_wav["duration_ms"] if final_wav else 0)
        )
        added = end_ms - nominal
        if blocking_count:
            blocking_captures += 1

        last = -1
        for pose in poses:
            if pose["t_ms"] < last:
                raise ValueError(f"{capture}: pose times run backwards at {pose['index']}")
            last = pose["t_ms"]
            if not os.path.exists(os.path.join(folder, pose["image"])):
                raise ValueError(f"{capture}: missing image {pose['image']}")

        resolved = {
            "capture": capture,
            "frame_delay_ms": frame_delay_default,
            "hold_ms": hold_ms,
            "canvas": dict(CANVAS),
            "pre_sounds": pre_sounds,
            "poses": [
                {
                    "index": p["index"],
                    "t_ms": p["t_ms"],
                    "image": p["image"],
                    "x": p["x"],
                    "y": p["y"],
                    "w": p["w"],
                    "h": p["h"],
                    "sound": p["sound"],
                }
                for p in poses
            ],
            "end_ms": end_ms,
            "final_wav": (
                {"name": final_wav["name"], "duration_ms": final_wav["duration_ms"]}
                if final_wav
                else None
            ),
            "cuts": [],
        }
        with open(os.path.join(folder, "resolved.json"), "w") as fh:
            json.dump(resolved, fh, indent=1)

        for entry in entries:
            entry.pop("pose", None)
        timeline = {
            "capture": capture,
            "anx_file": os.path.basename(path),
            "ini_file": ini_name,
            "ini_section": capture,
            "frame_count_in_anx": count,
            "ini_key_count": len(keys),
            "timeline_length": len(entries),
            "unused_anx_records": count - len(entries),
            "distinct_records": len(records),
            "reused_references": len(entries) - len(set(offsets[: len(entries)])),
            "frame_delay_ms": frame_delay_default,
            "capture_offset": placement,
            "canvas": dict(CANVAS),
            "poses_shown": len(poses),
            "end_ms": end_ms,
            "blocking_sound_count": blocking_count,
            "blocking_stall_ms": blocking_ms,
            "nominal_end_ms": nominal,
            "blocking_added_ms": added,
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
                "frame_count_in_anx": count,
                "timeline_entries": len(entries),
                "unused_anx_records": count - len(entries),
                "poses_shown": len(poses),
                "hold_ms": hold_ms,
                "end_ms": end_ms,
                "blocking_sound_count": blocking_count,
                "blocking_stall_ms": blocking_ms,
                "nominal_end_ms": nominal,
                "blocking_added_ms": added,
                "offset_x": offset_x,
                "offset_y": offset_y,
                "final_wav": final_wav,
                "compressed_lengths": lengths[:count],
                "output": f"captures/{capture}/resolved.json",
            }
        )
        summary.append(
            f"  {capture} poses {len(poses):3d} end_ms {end_ms:6d} "
            f"blocking sounds {blocking_count}"
            + (
                f" (stalled {blocking_ms} ms, end_ms {added} ms later than"
                f" the {nominal} ms a run with no blocking sound takes)"
                if blocking_count
                else ""
            )
        )

    return {
        "captures": captures,
        "records": record_entries,
        "distinct_record_count": total_records,
        "timeline_entry_count": total_timeline,
        "pose_count": total_poses,
        "capture_count": len(captures),
        "captures_with_blocking_sounds": blocking_captures,
        "silent_in_original_sounds": unresolved,
        "summary_lines": summary,
    }
