"""Generate the interpolated frames for one capture.

The run has four parts. It composes every pose on one padded canvas, writes the
colour and alpha pictures RIFE reads, runs RIFE once per transition per channel,
then divides the colour back out and writes RGBA frames plus a manifest.

RIFE's directory mode maps output i to input position i * count / numframe and
interpolates between the two inputs around it. A directory holding just the two
poses of one transition, run with numframe = 2 * D, therefore answers every time
step i / D in one process. D is the common denominator of the time steps that
transition needs, so one process covers all seven or eight samples inside a
120 ms gap instead of seven or eight processes. A transition whose denominator
comes out too large falls back to one process per sample.
"""

import hashlib
import json
import math
import multiprocessing
import os
import shutil
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction

from . import fixture, images, png, timeline

MODEL_NAME = "rife-v4.6"
DEFAULT_BINARY = ".cache/rife/bin/rife-ncnn-vulkan"
DEFAULT_MODEL = ".cache/rife/bin/rife-v4.6"
MAX_DENOMINATOR = 128
RIFE_PROCESSES = 3


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def tool_commit(root):
    """Return the git commit of the repository, or None outside git."""
    try:
        out = subprocess.run(["git", "-C", root, "rev-parse", "HEAD"],
                             capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() or None if out.returncode == 0 else None


def load_spec(assets, capture, resolved_path):
    """Read resolved.json, or build the fixture when it is not there yet."""
    path = resolved_path or os.path.join(assets, "captures", capture, "resolved.json")
    if os.path.exists(path):
        with open(path) as fh:
            return json.load(fh), path, sha256(path)
    spec = fixture.build(capture, "original/win3x/cd")
    return spec, None, None


def _decimal(value):
    """Round a Fraction of milliseconds to six decimal places."""
    scaled = value * 1000000
    whole = (scaled.numerator * 2 + scaled.denominator) // (scaled.denominator * 2)
    return whole / 1000000


def prepare(spec, source_dir, work_dir):
    """Compose every pose and write the colour and alpha pictures for RIFE.

    Returns the canvas rectangle and the composed RGBA buffer of every pose.
    """
    rect = images.union_rect(spec["poses"])
    _, _, width, height = rect
    for name in ("rgb", "alpha"):
        path = os.path.join(work_dir, name)
        shutil.rmtree(path, ignore_errors=True)
        os.makedirs(path)
    composed = []
    for i, pose in enumerate(spec["poses"]):
        rgba = images.compose(os.path.join(source_dir, pose["image"]), pose, rect)
        composed.append(rgba)
        rgb, alpha = images.split(rgba)
        png.write_rgb(os.path.join(work_dir, "rgb", "%05d.png" % i), width, height,
                      images.rows(rgb, width, 3), level=1)
        png.write_gray(os.path.join(work_dir, "alpha", "%05d.png" % i), width, height,
                       images.rows(alpha, width, 1), level=1)
    return rect, composed


def _run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("rife failed: %s\n%s" % (" ".join(cmd), result.stderr[-2000:]))
    return cmd


def plan_rife(entries, work_dir, binary, model):
    """Build the RIFE command list and say where each sample's pictures land.

    Returns (commands, paths). `paths` maps a sample position to the colour and
    alpha file the recombine step should read.
    """
    wanted = {}
    for n, entry in enumerate(entries):
        if entry["kind"] == timeline.INTERP:
            wanted.setdefault(entry["pose"], []).append((n, entry["s"]))

    commands = []
    paths = {}
    for k in sorted(wanted):
        samples = wanted[k]
        denominator = 1
        for _, s in samples:
            denominator = denominator * s.denominator // math.gcd(denominator, s.denominator)
        if denominator <= MAX_DENOMINATOR:
            for channel in ("rgb", "alpha"):
                pair_dir = os.path.join(work_dir, channel, "pair%04d" % k)
                out_dir = os.path.join(work_dir, channel + "_out", "pair%04d" % k)
                shutil.rmtree(pair_dir, ignore_errors=True)
                shutil.rmtree(out_dir, ignore_errors=True)
                os.makedirs(pair_dir)
                os.makedirs(out_dir)
                for j in (0, 1):
                    os.link(os.path.join(work_dir, channel, "%05d.png" % (k + j)),
                            os.path.join(pair_dir, "%d.png" % j))
                commands.append([binary, "-m", model, "-i", pair_dir, "-o", out_dir,
                                 "-n", str(2 * denominator)])
            for n, s in samples:
                step = s.numerator * (denominator // s.denominator)
                paths[n] = tuple(
                    os.path.join(work_dir, channel + "_out", "pair%04d" % k,
                                 "%08d.png" % (step + 1))
                    for channel in ("rgb", "alpha"))
        else:
            for n, s in samples:
                pair = []
                for channel in ("rgb", "alpha"):
                    out_dir = os.path.join(work_dir, channel + "_out")
                    os.makedirs(out_dir, exist_ok=True)
                    out_path = os.path.join(out_dir, "%08d.png" % n)
                    commands.append([binary, "-m", model,
                                     "-0", os.path.join(work_dir, channel, "%05d.png" % k),
                                     "-1", os.path.join(work_dir, channel, "%05d.png" % (k + 1)),
                                     "-s", "%.9f" % float(s), "-o", out_path])
                    pair.append(out_path)
                paths[n] = tuple(pair)
    return commands, paths


def _recombine_one(job):
    """Worker: read one colour and alpha frame, write one RGBA frame."""
    rgb_path, alpha_path, out_path, width, height, cutoff = job
    rw, rh, rc, rgb = png.read(rgb_path)
    aw, ah, ac, alpha = png.read(alpha_path)
    if (rw, rh) != (width, height) or (aw, ah) != (width, height):
        raise ValueError("rife returned %dx%d for %s" % (rw, rh, rgb_path))
    if rc != 3:
        rgb = bytes(b for i, b in enumerate(rgb) if i % rc < 3)
    grey = bytes(alpha) if ac == 1 else bytes(alpha[i * ac] for i in range(width * height))
    rgba = images.combine(bytes(rgb), grey, cutoff)
    png.write_rgba(out_path, width, height, images.rows(rgba, width, 4))
    return out_path


def _write_copy(job):
    """Worker: write one source copy, hold, or empty frame."""
    out_path, width, height, rgba = job
    png.write_rgba(out_path, width, height, images.rows(rgba, width, 4))
    return out_path


def describe(spec, entries, fps, rect):
    """Print the plan for --dry-run."""
    kinds = {}
    for entry in entries:
        kinds[entry["kind"]] = kinds.get(entry["kind"], 0) + 1
    print("capture %s, %d poses, end %s ms, %d frames at %d fps"
          % (spec["capture"], len(spec["poses"]), spec["end_ms"], len(entries), fps))
    print("canvas %dx%d at (%d, %d)" % (rect[2], rect[3], rect[0], rect[1]))
    print("frames by kind: %s" % ", ".join("%s %d" % kv for kv in sorted(kinds.items())))


def generate(capture, assets, out_dir, fps=60, resolved_path=None,
             binary=DEFAULT_BINARY, model=DEFAULT_MODEL, dry_run=False,
             jobs=None, keep_work=False):
    """Build every output frame and the manifest. Returns the manifest."""
    started = time.time()
    root = os.getcwd()
    spec, spec_path, spec_hash = load_spec(assets, capture, resolved_path)
    source_dir = os.path.join(assets, "captures", capture)
    entries = timeline.plan(spec, fps)

    if dry_run:
        describe(spec, entries, fps, images.union_rect(spec["poses"]))
        return None

    os.makedirs(out_dir, exist_ok=True)
    work_dir = os.path.join(out_dir, "work")
    os.makedirs(work_dir, exist_ok=True)
    rect, composed = prepare(spec, source_dir, work_dir)
    _, _, width, height = rect

    commands, frame_paths = plan_rife(entries, work_dir, binary, model)
    with ThreadPoolExecutor(max_workers=RIFE_PROCESSES) as pool:
        for _ in pool.map(_run, commands):
            pass

    index = {p["index"]: i for i, p in enumerate(spec["poses"])}
    empty = bytes(width * height * 4)
    copy_jobs = []
    interp_jobs = []
    names = []
    for n, entry in enumerate(entries):
        name = "frame%05d.png" % n
        names.append(name)
        out_path = os.path.join(out_dir, name)
        if entry["kind"] == timeline.INTERP:
            rgb_path, alpha_path = frame_paths[n]
            interp_jobs.append((rgb_path, alpha_path, out_path, width, height,
                                images.ALPHA_CUTOFF))
        elif entry["kind"] == timeline.BLANK:
            copy_jobs.append((out_path, width, height, empty))
        else:
            copy_jobs.append((out_path, width, height,
                              bytes(composed[index[entry["source"][0]]])))

    with multiprocessing.Pool(jobs or os.cpu_count()) as pool:
        for _ in pool.imap_unordered(_write_copy, copy_jobs, chunksize=4):
            pass
        for _ in pool.imap_unordered(_recombine_one, interp_jobs, chunksize=4):
            pass

    manifest = build_manifest(capture, spec, spec_path, spec_hash, entries, names,
                              rect, fps, binary, model, commands, source_dir, root)
    manifest["wall_seconds"] = round(time.time() - started, 2)
    with open(os.path.join(out_dir, "manifest.json"), "w") as fh:
        json.dump(manifest, fh, indent=1)
        fh.write("\n")
    if not keep_work:
        shutil.rmtree(work_dir, ignore_errors=True)
    return manifest


def build_manifest(capture, spec, spec_path, spec_hash, entries, names, rect, fps,
                   binary, model, commands, source_dir, root):
    """Describe the run in enough detail to reproduce and check it."""
    x, y, width, height = rect
    frames = [{
        "file": name,
        "t_ms": _decimal(entry["t"]),
        "duration_ms": _decimal(entry["duration"]),
        "kind": entry["kind"],
        "source": entry["source"],
        "s": float(entry["s"]) if entry["s"] is not None else None,
    } for entry, name in zip(entries, names)]
    sounds = [{"pose": p["index"], "t_ms": p["t_ms"], "sound": p["sound"]}
              for p in spec["poses"] if p.get("sound")]
    return {
        "capture": capture,
        "fps": fps,
        "tool": {
            "commit": tool_commit(root),
            "model": MODEL_NAME,
            "model_sha256": sha256(os.path.join(model, "flownet.bin")),
            "binary_sha256": sha256(binary),
            "alpha_cutoff": images.ALPHA_CUTOFF,
            "rife_commands": len(commands),
            "rife_example": " ".join(commands[0]) if commands else None,
        },
        "input": {
            "resolved": spec_path,
            "resolved_sha256": spec_hash,
            "source_dir": source_dir,
            "from_fixture": spec_path is None,
            "poses": [
                {"index": p["index"], "t_ms": p["t_ms"], "image": p["image"],
                 "x": p["x"], "y": p["y"], "w": p["w"], "h": p["h"],
                 "sha256": sha256(os.path.join(source_dir, p["image"]))}
                for p in spec["poses"]
            ],
        },
        "canvas": spec["canvas"],
        "frame_rect": {"x": x, "y": y, "w": width, "h": height},
        "end_ms": spec["end_ms"],
        "final_wav": spec.get("final_wav"),
        "cuts": spec.get("cuts") or [],
        "pre_sounds": spec.get("pre_sounds") or [],
        "sounds": sounds,
        "frames": frames,
    }
