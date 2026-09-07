"""Generate the 60 frames per second walk cycles of one piece.

A walking piece draws four to sixteen hand drawn pictures, one every 100
milliseconds. That is ten pictures a second next to the sixty the capture
films now draw, so the character's legs snap from pose to pose. This module
asks RIFE for five pictures between each pair of poses, which turns one 100
millisecond step into six frames at 60 frames per second.

The cycle loops, so the last pose interpolates back to the first one and a
walk that runs past the end of the sequence keeps moving. The frames of one
direction come in different sizes, so every one of them is composed onto a
single canvas first, placed where src/board/placement.cpp would draw it around
the anchor the piece stands on. The manifest records that canvas and the
anchor inside it, and the game draws the frame at the anchor rather than
sizing the rectangle from the picture.

Colour and alpha go through RIFE separately, the same way tools/interp does
for the captures. RIFE reads three channels and drops the fourth, so the
colour goes in premultiplied over black and comes back divided by the
interpolated alpha.

RIFE's directory mode maps output i to input position i * count / numframe.
Feeding it the whole cycle plus a copy of the first pose, count = N + 1
pictures, and asking for 6 * count frames therefore puts output i at position
i / 6. One process per channel covers a whole direction.
"""

import json
import os
import shutil
import time
from concurrent.futures import ThreadPoolExecutor

from . import images, png
from .pipeline import (DEFAULT_BINARY, DEFAULT_MODEL, MODEL_NAME, RIFE_PROCESSES, _run,
                       sha256, tool_commit)

PIECES = ["AT", "BF", "C3", "CB", "DV", "EM", "LO", "LS", "R2", "SP", "ST", "YO"]

# The directions the game walks in. GameSession::walkFor asks for one of these
# eight and falls back to the nearest one that has frames, so the four extra
# sections some pieces carry never reach the screen.
SECTIONS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]

# How many pictures one 100 millisecond step becomes.
FRAMES_PER_STEP = 6
STEP_MS = 100


def cycle_names(manifest, section):
    """Return the resource names of one direction, in the order it walks them.

    src/anim/walk.cpp reads the numbered keys of the INI section in file order,
    keeps the first `count` of them, and draws the bitmap one number above each
    key. This follows the same rule, so the pictures line up with the frames
    the game already draws.
    """
    for sequence in manifest["sequences"]:
        if sequence["direction"] != section or sequence["kind"] != "walk":
            continue
        declared = sequence["declared_count"] or 0
        steps = sequence["steps"][:declared]
        if not steps:
            return []
        sizes = {b["resource_name"]: b for b in sequence["bitmaps"]}
        names = []
        for step in steps:
            name = "%s_%s%03d" % (manifest["piece"], section, step["frame"] + 1)
            if name not in sizes:
                raise RuntimeError("%s has no bitmap %s" % (manifest["piece"], name))
            names.append(sizes[name])
        return names
    return []


def place(width, height):
    """Where one picture's top left corner sits relative to the anchor.

    src/board/placement.cpp centres a sprite on the anchor and puts the anchor
    a quarter of the width above the bottom edge, which is where a standing
    character's feet are.
    """
    return -(width // 2), -(height - width // 4)


def canvas_for(frames):
    """Return (width, height, anchor x, anchor y) covering every picture."""
    lefts = []
    tops = []
    rights = []
    bottoms = []
    for frame in frames:
        left, top = place(frame["width"], frame["height"])
        lefts.append(left)
        tops.append(top)
        rights.append(left + frame["width"])
        bottoms.append(top + frame["height"])
    left = min(lefts)
    top = min(tops)
    return max(rights) - left, max(bottoms) - top, -left, -top


def compose(path, frame, width, height, anchor_x, anchor_y):
    """Read one picture and place it on the canvas around the anchor."""
    src_w, src_h, channels, pixels = png.read(path)
    if channels != 4:
        raise ValueError("%s is not RGBA" % path)
    if (src_w, src_h) != (frame["width"], frame["height"]):
        raise ValueError("%s is %dx%d, the manifest says %dx%d"
                         % (path, src_w, src_h, frame["width"], frame["height"]))
    left, top = place(src_w, src_h)
    out = bytearray(width * height * 4)
    dst_x = left + anchor_x
    dst_y = top + anchor_y
    for row in range(src_h):
        start = ((dst_y + row) * width + dst_x) * 4
        out[start:start + src_w * 4] = pixels[row * src_w * 4:(row + 1) * src_w * 4]
    return out


def prepare(piece_dir, frames, work_dir, width, height, anchor_x, anchor_y):
    """Write the colour and alpha pictures RIFE reads. Returns the RGBA poses.

    The cycle loops, so the first pose is written again at the end. RIFE then
    interpolates the last pose back into the first one.
    """
    for name in ("rgb", "alpha"):
        path = os.path.join(work_dir, name)
        shutil.rmtree(path, ignore_errors=True)
        os.makedirs(path)
    composed = []
    for frame in frames:
        composed.append(compose(os.path.join(piece_dir, frame["image"]), frame, width, height,
                                anchor_x, anchor_y))
    for i in range(len(composed) + 1):
        rgba = composed[i % len(composed)]
        rgb, alpha = images.split(rgba)
        png.write_rgb(os.path.join(work_dir, "rgb", "%05d.png" % i), width, height,
                      images.rows(rgb, width, 3), level=1)
        png.write_gray(os.path.join(work_dir, "alpha", "%05d.png" % i), width, height,
                       images.rows(alpha, width, 1), level=1)
    return composed


def rife_commands(work_dir, count, binary, model):
    """One process per channel over the whole cycle."""
    commands = []
    for channel in ("rgb", "alpha"):
        out_dir = os.path.join(work_dir, channel + "_out")
        shutil.rmtree(out_dir, ignore_errors=True)
        os.makedirs(out_dir)
        commands.append([binary, "-m", model, "-i", os.path.join(work_dir, channel),
                         "-o", out_dir, "-n", str(FRAMES_PER_STEP * count)])
    return commands


def run_rife(command, attempts=3):
    """Run one RIFE process, retrying a failure.

    Three of these share one GPU. A run now and then comes back with a Vulkan
    error and no output, and the same command run again succeeds, so a failure
    waits a second and tries again before it stops the piece.
    """
    for attempt in range(attempts):
        try:
            return _run(command)
        except RuntimeError:
            if attempt + 1 == attempts:
                raise
            time.sleep(1.0)
    return None


def build_section(piece, section, piece_dir, out_root, binary, model, keep_work):
    """Write one direction's frames and return its manifest entry."""
    manifest = json.load(open(os.path.join(piece_dir, "manifest.json")))
    frames = cycle_names(manifest, section)
    if not frames:
        return None

    width, height, anchor_x, anchor_y = canvas_for(frames)
    section_dir = os.path.join(out_root, section)
    shutil.rmtree(section_dir, ignore_errors=True)
    os.makedirs(section_dir)
    work_dir = os.path.join(out_root, "work", section)
    os.makedirs(work_dir, exist_ok=True)

    composed = prepare(piece_dir, frames, work_dir, width, height, anchor_x, anchor_y)
    count = len(composed) + 1
    commands = rife_commands(work_dir, count, binary, model)
    for command in commands:
        run_rife(command)

    entries = []
    for step in range(len(composed)):
        for sub in range(FRAMES_PER_STEP):
            index = step * FRAMES_PER_STEP + sub
            name = "frame%05d.png" % index
            path = os.path.join(section_dir, name)
            if sub == 0:
                png.write_rgba(path, width, height, images.rows(composed[step], width, 4))
                kind = "copy"
                source = [step]
                position = 0.0
            else:
                # RIFE numbers its output files from one.
                rgb_path = os.path.join(work_dir, "rgb_out", "%08d.png" % (index + 1))
                alpha_path = os.path.join(work_dir, "alpha_out", "%08d.png" % (index + 1))
                rw, rh, rc, rgb = png.read(rgb_path)
                aw, ah, ac, alpha = png.read(alpha_path)
                if (rw, rh) != (width, height) or (aw, ah) != (width, height):
                    raise RuntimeError("rife returned %dx%d for %s" % (rw, rh, rgb_path))
                if rc != 3:
                    rgb = bytes(b for i, b in enumerate(rgb) if i % rc < 3)
                grey = (bytes(alpha) if ac == 1
                        else bytes(alpha[i * ac] for i in range(width * height)))
                rgba = images.combine(bytes(rgb), grey, images.ALPHA_CUTOFF)
                png.write_rgba(path, width, height, images.rows(rgba, width, 4))
                kind = "interp"
                source = [step, (step + 1) % len(composed)]
                position = sub / FRAMES_PER_STEP
            entries.append({
                "file": "%s/%s" % (section, name),
                "index": index,
                "step": step,
                "sub": sub,
                "t_ms": index * STEP_MS / FRAMES_PER_STEP,
                "kind": kind,
                "source": source,
                "s": position,
            })

    if not keep_work:
        shutil.rmtree(work_dir, ignore_errors=True)
    return {
        "section": section,
        "canvas": {"w": width, "h": height},
        "anchor": {"x": anchor_x, "y": anchor_y},
        "cycle": [{
            "resource_name": f["resource_name"],
            "image": f["image"],
            "w": f["width"],
            "h": f["height"],
            "sha256": sha256(os.path.join(piece_dir, f["image"])),
        } for f in frames],
        "frames": entries,
        "rife_commands": len(commands),
        "rife_example": " ".join(commands[0]),
    }


def generate(piece, assets, out_dir=None, sections=None, binary=DEFAULT_BINARY,
             model=DEFAULT_MODEL, keep_work=False, jobs=None):
    """Build every direction of one piece and write walk60/manifest.json."""
    started = time.time()
    piece_dir = os.path.join(assets, "pieces", piece)
    out_root = out_dir or os.path.join(piece_dir, "walk60")
    os.makedirs(out_root, exist_ok=True)

    wanted = sections or SECTIONS
    built = []
    # RIFE holds one GPU, so the same three processes the capture run uses
    # cover the eight directions without queueing on the driver.
    with ThreadPoolExecutor(max_workers=jobs or RIFE_PROCESSES) as pool:
        results = pool.map(
            lambda name: build_section(piece, name, piece_dir, out_root, binary, model,
                                       keep_work),
            wanted)
        for entry in results:
            if entry is not None:
                built.append(entry)
    if not keep_work:
        shutil.rmtree(os.path.join(out_root, "work"), ignore_errors=True)

    manifest = {
        "piece": piece,
        "fps": 60,
        "frames_per_step": FRAMES_PER_STEP,
        "step_ms": STEP_MS,
        "tool": {
            "commit": tool_commit(os.getcwd()),
            "model": MODEL_NAME,
            "model_sha256": sha256(os.path.join(model, "flownet.bin")),
            "binary_sha256": sha256(binary),
            "alpha_cutoff": images.ALPHA_CUTOFF,
        },
        "input": {"source_dir": piece_dir,
                  "manifest_sha256": sha256(os.path.join(piece_dir, "manifest.json"))},
        "sections": built,
    }
    manifest["wall_seconds"] = round(time.time() - started, 2)
    with open(os.path.join(out_root, "manifest.json"), "w") as fh:
        json.dump(manifest, fh, indent=1)
        fh.write("\n")
    return manifest


def check(out_root):
    """Verify one walk60 directory. Returns a list of problems, empty on pass."""
    problems = []
    path = os.path.join(out_root, "manifest.json")
    if not os.path.exists(path):
        return ["%s is missing" % path]
    with open(path) as fh:
        manifest = json.load(fh)
    for section in manifest["sections"]:
        width = section["canvas"]["w"]
        height = section["canvas"]["h"]
        expected = len(section["cycle"]) * manifest["frames_per_step"]
        if len(section["frames"]) != expected:
            problems.append("%s [%s] lists %d frames, %d poses need %d"
                            % (manifest["piece"], section["section"], len(section["frames"]),
                               len(section["cycle"]), expected))
        for frame in section["frames"]:
            file_path = os.path.join(out_root, frame["file"])
            if not os.path.exists(file_path):
                problems.append("%s is missing" % file_path)
                continue
            w, h, channels, _ = png.read(file_path)
            if (w, h) != (width, height) or channels != 4:
                problems.append("%s is %dx%d with %d channels, the canvas is %dx%d RGBA"
                                % (file_path, w, h, channels, width, height))
    return problems
