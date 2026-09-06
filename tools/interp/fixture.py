"""Build a stand-in resolved.json for one capture straight from the raw files.

The capture extraction lane owns the real assets/captures/<NAME>/resolved.json.
Until it lands, `python3 -m tools.interp.fixture --capture BBWB` writes the same
schema from original/win3x/cd. Delete this file once the real writer ships.

Frame positions come from the second table in the ANX header at file offset
0x25c, which holds one signed 16-bit x and y per frame. The player adds the
[<NAME>_OFFSET] x and y from the capture INI to those values, so this does the
same. A missing offset key adds nothing.

Pose i appears at (i - 1) * frame_delay_ms and the last pose stays on screen for
hold_ms, so end_ms is (count - 1) * frame_delay_ms + hold_ms.
"""

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.reference import anx  # noqa: E402

POSITION_TABLE = 0x25C
FRAME_DELAY_MS = 120
HOLD_MS = 1000
CANVAS = {"x": 0, "y": 0, "w": 640, "h": 480}
SOUND_MODES = {0: "async", 1: "sync", 2: "async"}


def read_ini(path):
    """Return the INI as a dict of section name to dict of key to value.

    Keys and section names keep their original case. A repeated key keeps the
    first value, which is what GetPrivateProfileString returns.
    """
    sections = {}
    current = None
    with open(path, "rb") as fh:
        text = fh.read().decode("latin-1")
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1].strip()
            sections.setdefault(current, {})
        elif current is not None and "=" in line:
            key, value = line.split("=", 1)
            sections[current].setdefault(key.strip(), value.strip())
    return sections


def build(capture, cd_dir):
    """Return the resolved.json dictionary for one capture."""
    ini_path = os.path.join(cd_dir, capture[:2] + ".INI")
    anx_path = os.path.join(cd_dir, capture + ".ANX")
    ini = read_ini(ini_path)
    with open(anx_path, "rb") as fh:
        data = fh.read()
    count, offsets, records = anx.parse(data)

    offset_section = ini.get(capture + "_OFFSET", {})
    off_x = int(offset_section.get("x", 0))
    off_y = int(offset_section.get("y", 0))
    hold_ms = int(offset_section.get("hold", HOLD_MS))
    final_wav = offset_section.get("wav") or None
    if final_wav:
        final_wav = final_wav.upper()

    poses = []
    for i in range(count):
        raw_x, raw_y = struct.unpack_from("<hh", data, POSITION_TABLE + 4 * i)
        record = records[offsets[i]]
        key = "%s_%03d" % (capture, i + 1)
        frame = ini.get(key, {})
        sound = None
        name = frame.get("wav")
        if name:
            pause = int(frame.get("pause", 0) or 0)
            sound = {
                "name": name.upper(),
                "mode": SOUND_MODES.get(pause, "async"),
                "pause": pause,
                "duration_ms": None,
            }
        poses.append({
            "index": i + 1,
            "t_ms": i * FRAME_DELAY_MS,
            "image": "rec%08x.png" % offsets[i],
            "x": raw_x + off_x,
            "y": raw_y + off_y,
            "w": record["width"],
            "h": record["height"],
            "sound": sound,
        })

    return {
        "capture": capture,
        "frame_delay_ms": FRAME_DELAY_MS,
        "hold_ms": hold_ms,
        "canvas": dict(CANVAS),
        "poses": poses,
        "end_ms": (count - 1) * FRAME_DELAY_MS + hold_ms,
        "final_wav": final_wav,
        "cuts": [],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--capture", required=True)
    parser.add_argument("--cd", default="original/win3x/cd")
    parser.add_argument("--out", help="path to write; default is stdout")
    args = parser.parse_args(argv)
    text = json.dumps(build(args.capture, args.cd), indent=1) + "\n"
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
