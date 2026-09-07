"""Check that a lowercased copy of the CD renders the same frame.

The original CD names every file in uppercase. A copy made on macOS or
Windows can lowercase the names, and Linux then fails to open the uppercase
name. This test copies the CD into a temporary directory, lowercases every
filename, and dumps the same frame from both directories. The two PPM files
must match byte for byte.
"""

import argparse
import filecmp
import os
import shutil
import subprocess
import sys
import tempfile


def lowercase_copy(cd_dir, into):
    shutil.copytree(cd_dir, into)
    for name in os.listdir(into):
        low = name.lower()
        if low == name:
            continue
        # Two renames, because macOS and Windows treat the two names as one
        # file and a direct rename is a no-op there.
        os.rename(os.path.join(into, name), os.path.join(into, name + ".tmp"))
        os.rename(os.path.join(into, name + ".tmp"), os.path.join(into, low))


def dump(game, cd_dir, assets_dir, out_path):
    command = [game, "--cd", cd_dir, "--assets", assets_dir, "--skip-title",
               "--dump-at", "500", out_path]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise SystemExit(f"{' '.join(command)} exited {result.returncode}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("game")
    parser.add_argument("cd_dir")
    parser.add_argument("assets_dir")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as work:
        lower = os.path.join(work, "cd")
        lowercase_copy(args.cd_dir, lower)

        real_ppm = os.path.join(work, "real.ppm")
        lower_ppm = os.path.join(work, "lower.ppm")
        dump(args.game, args.cd_dir, args.assets_dir, real_ppm)
        dump(args.game, lower, args.assets_dir, lower_ppm)

        if not filecmp.cmp(real_ppm, lower_ppm, shallow=False):
            raise SystemExit("the lowercased CD renders a different frame")
    print("the lowercased CD renders the same frame")


if __name__ == "__main__":
    main()
