"""Command line for the offline capture interpolation pipeline.

    python3 -m tools.interp --capture BBWB --assets assets \
        --out assets/captures/BBWB/interp60 [--fps 60] [--dry-run]
    python3 -m tools.interp --walk AT --assets assets
    python3 -m tools.interp --walk all --assets assets
    python3 -m tools.interp --check assets/captures/BBWB/interp60
    python3 -m tools.interp --walk-check assets/pieces/AT/walk60
    python3 -m tools.interp --refresh-cues assets/captures/BBWB/interp60
"""

import argparse
import sys

from . import check, pipeline
from . import walk as walk_mode


def main(argv=None):
    parser = argparse.ArgumentParser(prog="python3 -m tools.interp",
                                     description=__doc__.splitlines()[0])
    parser.add_argument("--capture", help="capture name such as BBWB")
    parser.add_argument("--assets", default="assets", help="the asset cache root")
    parser.add_argument("--out", help="output directory for the frames and manifest")
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--resolved", help="path of resolved.json, default under --assets")
    parser.add_argument("--rife", default=pipeline.DEFAULT_BINARY)
    parser.add_argument("--model", default=pipeline.DEFAULT_MODEL)
    parser.add_argument("--jobs", type=int, help="worker processes, default one per core")
    parser.add_argument("--keep-work", action="store_true",
                        help="keep the RIFE input and output pictures")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the plan and run no inference")
    parser.add_argument("--check", metavar="OUT_DIR", help="verify a finished output directory")
    parser.add_argument("--walk", metavar="PIECE",
                        help="interpolate one piece's walk cycles, or all twelve with 'all'")
    parser.add_argument("--sections", help="comma separated directions, default all eight")
    parser.add_argument("--walk-check", metavar="OUT_DIR",
                        help="verify a finished walk60 directory")
    parser.add_argument("--refresh-cues", metavar="OUT_DIR",
                        help="rewrite one manifest's sound cues from resolved.json")
    args = parser.parse_args(argv)

    if args.refresh_cues:
        changed = pipeline.refresh_cues(args.refresh_cues, args.resolved)
        print("%s: cues %s" % (args.refresh_cues, "rewritten" if changed else "already current"))
        return 0

    if args.walk_check:
        problems = walk_mode.check(args.walk_check)
        for line in problems:
            print(line)
        print("%s: %s" % (args.walk_check, "PASS" if not problems else "FAIL"))
        return 0 if not problems else 1

    if args.walk:
        pieces = walk_mode.PIECES if args.walk == "all" else [args.walk]
        sections = args.sections.split(",") if args.sections else None
        for piece in pieces:
            manifest = walk_mode.generate(piece, args.assets, out_dir=args.out,
                                          sections=sections, binary=args.rife,
                                          model=args.model, keep_work=args.keep_work,
                                          jobs=args.jobs)
            frames = sum(len(s["frames"]) for s in manifest["sections"])
            print("%s: %d directions, %d frames in %.1f s"
                  % (piece, len(manifest["sections"]), frames, manifest["wall_seconds"]))
        return 0

    if args.check:
        problems = check.run(args.check)
        print("%s: %s" % (args.check, "PASS" if not problems else "FAIL"))
        return 0 if not problems else 1

    if not args.capture or not (args.out or args.dry_run):
        parser.error("--capture and --out are required unless --check is used")

    manifest = pipeline.generate(
        args.capture, args.assets, args.out or "", fps=args.fps,
        resolved_path=args.resolved, binary=args.rife, model=args.model,
        dry_run=args.dry_run, jobs=args.jobs, keep_work=args.keep_work)
    if manifest:
        print("wrote %d frames to %s in %.1f s"
              % (len(manifest["frames"]), args.out, manifest["wall_seconds"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
