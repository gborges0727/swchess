"""Check that assets/captures/*/resolved.json matches --dump-timeline exactly.

tools/extract/captures.py writes resolved.json from the same rules the
player in src/anim/capture.cpp implements. This script re-derives the
timeline the C++ way, by running the viewer's --dump-timeline mode, and
diffs it against every resolved.json under an assets directory. A mismatch
here means the Python extractor and the C++ player disagree about when a
pose appears, which sound it carries, when that sound starts, or when the
capture ends.

Usage:
    python3 timeline_parity.py <swchess-viewer path> <cd dir> <assets dir>
"""

import glob
import json
import os
import re
import subprocess
import sys


def parse_dump(text):
    """Split --dump-timeline output into its pose rows and its end_ms."""
    rows = {}
    end_ms = None
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            match = re.search(r"\bend (-?\d+)\s*$", line)
            if match:
                end_ms = int(match.group(1))
            continue
        if line.startswith("index"):
            continue
        fields = line.split()
        # index t_ms x y w h sound mode sound_t_ms
        index, t_ms, x, y = (int(fields[0]), int(fields[1]), int(fields[2]), int(fields[3]))
        # The viewer writes "-" in all three sound columns for a pose whose
        # cue named nothing the sound library holds.
        name = fields[6] if fields[6] != "-" else None
        mode = fields[7] if fields[7] != "-" else None
        sound_t_ms = int(fields[8]) if fields[8] != "-" else None
        rows[index] = {"t_ms": t_ms, "x": x, "y": y, "sound_name": name,
                       "sound_mode": mode, "sound_t_ms": sound_t_ms}
    if end_ms is None:
        raise ValueError("no '# ... end N' line in --dump-timeline output")
    return rows, end_ms


def main():
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <swchess-viewer> <cd dir> <assets dir>", file=sys.stderr)
        return 2
    viewer, cd_dir, assets_dir = sys.argv[1:4]

    if not os.path.isdir(assets_dir):
        print(f"skip: {assets_dir} does not exist (run python3 -m tools.extract first)")
        return 0

    resolved_paths = sorted(glob.glob(os.path.join(assets_dir, "captures", "*", "resolved.json")))
    if not resolved_paths:
        print(f"skip: no resolved.json files under {assets_dir}/captures")
        return 0

    failures = []
    for path in resolved_paths:
        name = os.path.basename(os.path.dirname(path))
        with open(path) as fh:
            resolved = json.load(fh)

        result = subprocess.run(
            [viewer, "--cd", cd_dir, "--capture", name, "--dump-timeline", name],
            capture_output=True,
            text=True,
            check=True,
        )
        rows, cpp_end_ms = parse_dump(result.stdout)

        py_poses = {p["index"]: p for p in resolved["poses"]}
        if set(py_poses) != set(rows):
            failures.append(
                f"{name}: pose indices differ, python {sorted(py_poses)} vs cpp {sorted(rows)}"
            )
            continue

        for index, py_pose in py_poses.items():
            cpp_row = rows[index]
            for field in ("t_ms", "x", "y"):
                if py_pose[field] != cpp_row[field]:
                    failures.append(
                        f"{name} pose {index}: {field} python={py_pose[field]} cpp={cpp_row[field]}"
                    )
            # The sound a pose carries, and the millisecond the original
            # starts it. A sync sound starts before its pose reaches the
            # screen, so these two times differ on purpose.
            sound = py_pose.get("sound")
            py_sound = {
                "sound_name": sound["name"] if sound else None,
                "sound_mode": sound["mode"] if sound else None,
                "sound_t_ms": sound.get("t_ms") if sound else None,
            }
            for field, want in py_sound.items():
                if want != cpp_row[field]:
                    failures.append(
                        f"{name} pose {index}: {field} python={want} cpp={cpp_row[field]}"
                    )

        if resolved["end_ms"] != cpp_end_ms:
            failures.append(f"{name}: end_ms python={resolved['end_ms']} cpp={cpp_end_ms}")

    if failures:
        print(f"FAIL: {len(failures)} mismatch(es)")
        for line in failures:
            print(f"  {line}")
        return 1

    print(f"ok: {len(resolved_paths)} capture timelines, their poses and their sound cues "
          "match --dump-timeline exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
