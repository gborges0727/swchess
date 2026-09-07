"""Command line entry point for the asset extractor.

    python3 -m tools.extract --cd original/win3x/cd --out assets
    python3 -m tools.extract --selftest

The first command reads the original CD directory and fills the output tree. The
second decodes a few records again and checks that each one produces exactly
width times height pixels.
"""

import argparse
import hashlib
import json
import os
import random
import sys
import time

from . import VERSION, audio, captures, locales, ne, pieces, sheets, ui, verify
from .cdfs import cd_exists, cd_glob, cd_path
from .ini import IniFile, as_int
from ..reference import anx

DEFAULT_CD = "original/win3x/cd"

EXPECTED = {
    "capture_records": 4799,
    "timeline_entries": 5414,
    "poses_shown": 5342,
    "captures": 72,
    "piece_bitmaps": 1344,
    "wave_records": 110,
    "distinct_wave_names": 109,
    "bmp_files": 8,
    "locale_files": 4,
}

# The three cue problems the plan names. FUN_1058_0c4d uppercases the INI value
# and looks it up as a WAVE resource with no repair of any kind, so all three
# play nothing in the original. The extractor records them whether or not it
# also finds them on its own, so a reader of catalog.json sees all three.
KNOWN_CUE_NOTES = [
    {
        "kind": "silent_in_original",
        "ini_file": "WN.INI",
        "section": "WNBR_019",
        "raw": "atftstep.awv",
        "resolved": None,
        "note": "ATFTSTEP.AWV is not a WAVE resource, and the player never tries ATFTSTEP.WAV",
    },
    {
        "kind": "silent_in_original",
        "ini_file": "WP.INI",
        "section": "WPBB_002",
        "raw": "r2alarm\\.wav",
        "resolved": None,
        "note": "R2ALARM\\.WAV keeps the stray backslash, and the player never tries R2ALARM.WAV",
    },
    {
        "kind": "silent_in_original",
        "ini_file": "BB.INI",
        "section": "BBWQ_007",
        "raw": "LEIA2.WAV",
        "resolved": None,
        "note": "SWCAUDIO.DLL holds no LEIA2.WAV",
    },
]

SOURCE_EXTENSIONS = [".ANX", ".INI", ".BMP", ".WAV"]
SOURCE_FILES = [
    "AT.DLL", "BF.DLL", "C3.DLL", "CB.DLL", "DV.DLL", "EM.DLL",
    "LO.DLL", "LS.DLL", "R2.DLL", "SP.DLL", "ST.DLL", "YO.DLL",
    "SWCAUDIO.DLL", "TITLERES.DLL",
    "RESENG.DLL", "RESFRN.DLL", "RESGER.DLL", "RESSPN.DLL",
]


def hash_sources(cd_dir):
    """Hash every original file the extractor reads."""
    names = set(SOURCE_FILES)
    for extension in SOURCE_EXTENSIONS:
        for path in cd_glob(cd_dir, extension):
            names.add(os.path.basename(path).upper())
    out = {}
    for name in sorted(names):
        if not cd_exists(cd_dir, name):
            continue
        path = cd_path(cd_dir, name)
        with open(path, "rb") as fh:
            data = fh.read()
        out[name] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    return out


def run_extract(cd_dir, out_dir):
    """Fill the output tree and write catalog.json. Returns the counts."""
    started = time.time()
    os.makedirs(out_dir, exist_ok=True)

    cm = IniFile(cd_path(cd_dir, "CM.INI"))
    defaults = cm.section("defaults")
    frame_delay = as_int(defaults.get("frame_delay"), 120) if defaults else 120

    print("hashing source files")
    sources = hash_sources(cd_dir)

    print("extracting audio")
    audio_data, sound_index = audio.extract(cd_dir, os.path.join(out_dir, "audio"))

    print("extracting captures")
    capture_data = captures.extract(
        cd_dir, os.path.join(out_dir, "captures"), sound_index, frame_delay
    )

    print("extracting piece sprites")
    piece_data = pieces.extract(cd_dir, os.path.join(out_dir, "pieces"))

    print("extracting piece sheets and backgrounds")
    set_data = sheets.extract_sets(cd_dir, os.path.join(out_dir, "sets"))
    background_data = sheets.extract_backgrounds(cd_dir, os.path.join(out_dir, "backgrounds"))

    print("extracting language string tables")
    locale_data = locales.extract(cd_dir, os.path.join(out_dir, "locales"))

    print("extracting title artwork")
    ui_data = ui.extract(cd_dir, os.path.join(out_dir, "ui"))

    counts = {
        "capture_records": capture_data["distinct_record_count"],
        "timeline_entries": capture_data["timeline_entry_count"],
        "poses_shown": capture_data["pose_count"],
        "captures": capture_data["capture_count"],
        "piece_bitmaps": piece_data["bitmap_count"],
        "wave_records": audio_data["resource_record_count"],
        "distinct_wave_names": audio_data["distinct_resource_names"],
        "bmp_files": set_data["set_count"] + background_data["background_count"],
        "locale_files": locale_data["locale_file_count"],
    }

    # Start from the three cases the plan names, then fold in whatever the run
    # found. A case that appears in both keeps one entry and gains a flag.
    unresolved = [dict(note, confirmed_by_extractor=False) for note in KNOWN_CUE_NOTES]
    index = {(n["ini_file"], n["section"], n["raw"].lower()): n for n in unresolved}

    def record(kind, found, reason_key):
        key = (found["ini_file"], found["section"], found["raw"].strip().lower())
        existing = index.get(key)
        if existing is not None:
            existing["confirmed_by_extractor"] = True
            return
        entry = {
            "kind": kind,
            "ini_file": found["ini_file"],
            "section": found["section"],
            "raw": found["raw"],
            "resolved": found.get("resolved"),
            "note": found.get(reason_key, ""),
            "confirmed_by_extractor": True,
        }
        index[key] = entry
        unresolved.append(entry)

    for found in capture_data["silent_in_original_sounds"]:
        record("silent_in_original", found, "silent_reason")

    catalog = {
        "extractor_version": VERSION,
        "generated_unix_time": int(started),
        "cd_directory": os.path.abspath(cd_dir),
        "output_directory": os.path.abspath(out_dir),
        "frame_delay_ms": frame_delay,
        "transparent_palette_index": 0,
        "counts": counts,
        "expected_counts": EXPECTED,
        "counts_match_expected": {k: counts[k] == EXPECTED[k] for k in EXPECTED},
        "unresolved": unresolved,
        "sources": sources,
        "canvas": captures.CANVAS,
        "captures_with_blocking_sounds": capture_data["captures_with_blocking_sounds"],
        "captures": capture_data["captures"],
        "capture_records": capture_data["records"],
        "pieces": piece_data["pieces"],
        "piece_bitmaps": piece_data["bitmaps"],
        "piece_count_mismatches": piece_data["count_mismatches"],
        "sets": set_data["sets"],
        "backgrounds": background_data["backgrounds"],
        "audio": audio_data,
        "locales": locale_data["locales"],
        "ui": ui_data["ui_bitmaps"],
    }
    with open(os.path.join(out_dir, "catalog.json"), "w") as fh:
        json.dump(catalog, fh, indent=1)

    print()
    for line in capture_data["summary_lines"]:
        print(line)
    print(
        f"  {capture_data['captures_with_blocking_sounds']} of "
        f"{capture_data['capture_count']} captures have a blocking sound"
    )
    print()
    print(f"extractor {VERSION} finished in {time.time() - started:.1f} seconds")
    for key in EXPECTED:
        mark = "ok " if counts[key] == EXPECTED[key] else "BAD"
        print(f"  {mark} {key}: {counts[key]} (expected {EXPECTED[key]})")
    print(f"  catalog: {os.path.join(out_dir, 'catalog.json')}")
    return counts


def run_selftest(cd_dir, seed=None):
    """Decode five ANX records and five piece bitmaps again and check them."""
    rng = random.Random(seed)
    anx_paths = cd_glob(cd_dir, ".ANX")
    if not anx_paths:
        print(f"no ANX files under {cd_dir}", file=sys.stderr)
        return 1

    failures = 0
    print("checking five ANX records")
    for _ in range(5):
        path = rng.choice(anx_paths)
        count, offsets, records = anx.load(path)
        off = rng.choice(sorted(records))
        rec = records[off]
        expected = rec["width"] * rec["height"]
        actual = len(rec["pixels"])
        ok = actual == expected
        failures += 0 if ok else 1
        print(
            f"  {'ok ' if ok else 'BAD'} {os.path.basename(path)} record {off:#x} "
            f"{rec['width']}x{rec['height']} decoded {actual} pixels, expected {expected}"
        )

    print("checking five piece bitmaps")
    for _ in range(5):
        piece = rng.choice(pieces.PIECES)
        blob, resources = ne.read_file(cd_path(cd_dir, piece + ".DLL"))
        bitmaps = [r for r in resources if r.type_id == ne.RT_BITMAP]
        res = rng.choice(bitmaps)
        rec = anx.decode_record(blob, res.offset, res.offset + res.length)
        expected = rec["width"] * rec["height"]
        actual = len(rec["pixels"])
        ok = actual == expected
        failures += 0 if ok else 1
        print(
            f"  {'ok ' if ok else 'BAD'} {piece}.DLL {res.name_id} "
            f"{rec['width']}x{rec['height']} decoded {actual} pixels, expected {expected}"
        )

    if failures:
        print(f"self test failed on {failures} records", file=sys.stderr)
        return 1
    print("self test passed")
    return 0


def run_alpha_check(cd_dir, out_dir, samples=5, seed=None):
    """Read written sprite PNGs back and confirm alpha 0 tracks palette index 0."""
    rng = random.Random(seed)
    failures = 0
    anx_paths = cd_glob(cd_dir, ".ANX")
    for _ in range(samples):
        path = rng.choice(anx_paths)
        capture = os.path.splitext(os.path.basename(path))[0].upper()
        count, offsets, records = anx.load(path)
        off = rng.choice(sorted(records))
        image = os.path.join(out_dir, "captures", capture, f"rec{off:08x}.png")
        ok, message = verify.alpha_matches_index(image, records[off])
        failures += 0 if ok else 1
        print(f"  {'ok ' if ok else 'BAD'} {capture}/rec{off:08x}.png {message}")
    for _ in range(samples):
        piece = rng.choice(pieces.PIECES)
        blob, resources = ne.read_file(cd_path(cd_dir, piece + ".DLL"))
        bitmaps = [r for r in resources if r.type_id == ne.RT_BITMAP]
        res = rng.choice(bitmaps)
        rec = anx.decode_record(blob, res.offset, res.offset + res.length)
        image = os.path.join(out_dir, "pieces", piece, f"{res.name_id}.png")
        ok, message = verify.alpha_matches_index(image, rec)
        failures += 0 if ok else 1
        print(f"  {'ok ' if ok else 'BAD'} {piece}/{res.name_id}.png {message}")
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(prog="python3 -m tools.extract", description=__doc__)
    parser.add_argument("--cd", default=DEFAULT_CD, help="the original CD directory, read only")
    parser.add_argument("--out", default="assets", help="where to write the extracted assets")
    parser.add_argument("--selftest", action="store_true", help="decode a few records again and stop")
    parser.add_argument("--check-alpha", action="store_true", help="read written sprite PNGs back and check their alpha")
    parser.add_argument("--seed", type=int, default=None, help="fix the random choices in the self test")
    args = parser.parse_args(argv)

    if not os.path.isdir(args.cd):
        print(f"no such directory: {args.cd}", file=sys.stderr)
        return 2

    if args.selftest:
        return run_selftest(args.cd, args.seed)

    if args.check_alpha:
        return 1 if run_alpha_check(args.cd, args.out, seed=args.seed) else 0

    counts = run_extract(args.cd, args.out)
    return 0 if all(counts[k] == EXPECTED[k] for k in EXPECTED) else 1


if __name__ == "__main__":
    raise SystemExit(main())
