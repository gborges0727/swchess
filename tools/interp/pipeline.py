"""Generate the 60 fps interpolated frames for one capture.

The run has four parts. It composes every pose on one padded canvas, writes the
colour and alpha pictures RIFE reads, runs RIFE once per channel over the whole
capture, then divides the colour back out and writes RGBA frames plus a
manifest.

RIFE's directory mode maps output i to input position i * count / numframe and
interpolates between the two poses around it. Picking numframe so that ratio
equals one 60 fps step measured in poses puts every output exactly on a sample
time, which is why the whole capture takes two RIFE processes instead of one per
frame. The pose list is padded with copies of the last pose until the ratio is
exact; the padding only feeds output positions past the last real pose, which
the hold covers with a copy instead.
"""

import hashlib
import json
import multiprocessing
import os
import shutil
import subprocess
import sys
import time
from fractions import Fraction

from . import fixture, images, png, timeline

MODEL_NAME = "rife-v4.6"
DEFAULT_BINARY = ".cache/rife/bin/rife-ncnn-vulkan"
DEFAULT_MODEL = ".cache/rife/bin/rife-v4.6"


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
    """Round a Fraction of milliseconds to six decimal places as a float."""
    scaled = value * 1000000
    whole = (scaled.numerator * 2 + scaled.denominator) // (scaled.denominator * 2)
    return whole / 1000000


def prepare(spec, source_dir, work_dir):
    """Compose every pose and write the colour and alpha pictures for RIFE.

    Returns the canvas rectangle and the composed RGBA buffers.
    """
    rect = images.union_rect(spec["poses"])
    _, _, width, height = rect
    rgb_dir = os.path.join(work_dir, "rgb")
    alpha_dir = os.path.join(work_dir, "alpha")
    for path in (rgb_dir, alpha_dir):
        shutil.rmtree(path, ignore_errors=True)
        os.makedirs(path)
    composed = []
    for i, pose in enumerate(spec["poses"]):
        rgba = images.compose(os.path.join(source_dir, pose["image"]), pose, rect)
        composed.append(rgba)
        rgb, alpha = images.split(rgba)
        png.write_rgb(os.path.join(rgb_dir, "%05d.png" % i), width, height,
                      images.rows(rgb, width, 3), level=1)
        png.write_gray(os.path.join(alpha_dir, "%05d.png" % i), width, height,
                       images.rows(alpha, width, 1), level=1)
    return rect, composed


def pad_inputs(work_dir, count, multiple):
    """Copy the last pose picture until the input count divides evenly."""
    padded = count
    while padded % multiple:
        for name in ("rgb", "alpha"):
            src = os.path.join(work_dir, name, "%05d.png" % (count - 1))
            shutil.copyfile(src, os.path.join(work_dir, name, "%05d.png" % padded))
        padded += 1
    return padded


def run_rife(binary, model, in_dir, out_dir, numframe, gpu=None):
    """Run one RIFE pass over a directory and return the command line."""
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir)
    cmd = [binary, "-m", model, "-i", in_dir, "-o", out_dir, "-n", str(numframe)]
    if gpu is not None:
        cmd += ["-g", str(gpu)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("rife failed: %s\n%s" % (" ".join(cmd), result.stderr[-2000:]))
    return cmd


def run_rife_pair(binary, model, first, second, out_path, s, gpu=None):
    """Run one RIFE inference for a single time step."""
    cmd = [binary, "-m", model, "-0", first, "-1", second,
           "-s", "%.9f" % s, "-o", out_path]
    if gpu is not None:
        cmd += ["-g", str(gpu)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("rife failed: %s\n%s" % (" ".join(cmd), result.stderr[-2000:]))
    return cmd


def _recombine_one(job):
    """Worker: read one colour and alpha frame, write one RGBA frame."""
    rgb_path, alpha_path, out_path, width, height, cutoff = job
    rw, rh, rc, rgb = png.read(rgb_path)
    aw, ah, ac, alpha = png.read(alpha_path)
    if (rw, rh) != (width, height) or (aw, ah) != (width, height):
        raise ValueError("rife returned %dx%d for %s" % (rw, rh, rgb_path))
    if rc != 3:
        rgb = bytes(b for i, b in enumerate(rgb) if i % rc < 3)
    grey = bytes(alpha[i * ac] for i in range(width * height)) if ac != 1 else bytes(alpha)
    rgba = images.combine(bytes(rgb), grey, cutoff)
    png.write_rgba(out_path, width, height, images.rows(rgba, width, 4))
    return out_path


def _write_copy(job):
    """Worker: write one source copy frame."""
    out_path, width, height, rgba = job
    png.write_rgba(out_path, width, height, images.rows(rgba, width, 4))
    return out_path


def generate(capture, assets, out_dir, fps=60, resolved_path=None,
             binary=DEFAULT_BINARY, model=DEFAULT_MODEL, dry_run=False,
             jobs=None, keep_work=False):
    """Build every output frame and the manifest. Returns the manifest."""
    started = time.time()
    root = os.getcwd()
    spec, spec_path, spec_hash = load_spec(assets, capture, resolved_path)
    source_dir = os.path.join(assets, "captures", capture)
    entries = timeline.plan(spec, fps)
    gap = timeline.uniform_gap(spec)
    step = Fraction(1000, fps) / gap if gap else None
    batched = step is not None and step < 1

    if dry_run:
        kinds = {}
        for entry in entries:
            kinds[entry["kind"]] = kinds.get(entry["kind"], 0) + 1
        rect = images.union_rect(spec["poses"])
        print("capture %s, %d poses, end %d ms, %d frames at %d fps"
              % (capture, len(spec["poses"]), spec["end_ms"], len(entries), fps))
        print("canvas %dx%d at (%d, %d)" % (rect[2], rect[3], rect[0], rect[1]))
        print("frames by kind: %s" % ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
        print("batched rife: %s" % ("yes, step %s poses" % step if batched else "no"))
        return None

    os.makedirs(out_dir, exist_ok=True)
    work_dir = os.path.join(out_dir, "work")
    os.makedirs(work_dir, exist_ok=True)
    rect, composed = prepare(spec, source_dir, work_dir)
    _, _, width, height = rect

    commands = []
    frame_paths = {}
    if batched:
        padded = pad_inputs(work_dir, len(spec["poses"]), step.numerator)
        numframe = int(padded / step)
        for name in ("rgb", "alpha"):
            commands.append(run_rife(binary, model,
                                     os.path.join(work_dir, name),
                                     os.path.join(work_dir, name + "_out"),
                                     numframe))
        for n, entry in enumerate(entries):
            if entry["kind"] == timeline.INTERP:
                frame_paths[n] = (
                    os.path.join(work_dir, "rgb_out", "%08d.png" % (n + 1)),
                    os.path.join(work_dir, "alpha_out", "%08d.png" % (n + 1)),
                )
    else:
        for name in ("rgb_out", "alpha_out"):
            path = os.path.join(work_dir, name)
            shutil.rmtree(path, ignore_errors=True)
            os.makedirs(path)
        for n, entry in enumerate(entries):
            if entry["kind"] != timeline.INTERP:
                continue
            k = entry["pose"]
            pair = []
            for name in ("rgb", "alpha"):
                out_path = os.path.join(work_dir, name + "_out", "%08d.png" % (n + 1))
                commands.append(run_rife_pair(
                    binary, model,
                    os.path.join(work_dir, name, "%05d.png" % k),
                    os.path.join(work_dir, name, "%05d.png" % (k + 1)),
                    out_path, float(entry["s"])))
                pair.append(out_path)
            frame_paths[n] = tuple(pair)

    index = {p["index"]: i for i, p in enumerate(spec["poses"])}
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
    frames = []
    for entry, name in zip(entries, names):
        frame = {
            "file": name,
            "t_ms": _decimal(entry["t"]),
            "duration_ms": _decimal(entry["duration"]),
            "kind": entry["kind"],
            "source": entry["source"],
            "s": float(entry["s"]) if entry["s"] is not None else None,
        }
        frames.append(frame)
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
            "commands": [" ".join(c) for c in commands[:4]],
            "command_count": len(commands),
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
        "sounds": sounds,
        "frames": frames,
    }
