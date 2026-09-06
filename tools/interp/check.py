"""Verify one interp output directory against its own manifest and inputs.

The checks are the ones docs/plan.md section 3 asks for. Times are read back as
Decimal so the duration total is compared exactly and not through binary floats.
"""

import json
import os
import struct
from decimal import Decimal

from . import images, png, timeline


def _headers(path):
    """Return (width, height) from a PNG header without decoding pixels."""
    with open(path, "rb") as fh:
        head = fh.read(24)
    width, height = struct.unpack_from(">II", head, 16)
    return width, height


def run(out_dir, verbose=True):
    """Return a list of failure strings. An empty list means the run passed."""
    with open(os.path.join(out_dir, "manifest.json")) as fh:
        manifest = json.load(fh, parse_float=Decimal)
    problems = []
    notes = []
    frames = manifest["frames"]
    fps = int(manifest["fps"])
    end_ms = Decimal(manifest["end_ms"])

    expected = timeline.sample_count(int(manifest["end_ms"]), fps)
    if len(frames) != expected:
        problems.append("frame count is %d, expected ceil(%s * %d / 1000) = %d"
                        % (len(frames), end_ms, fps, expected))
    else:
        notes.append("frame count %d matches ceil(%s ms * %d / 1000)" % (expected, end_ms, fps))

    times = [Decimal(f["t_ms"]) for f in frames]
    bad = [i for i in range(1, len(times)) if times[i] <= times[i - 1]]
    if bad:
        problems.append("timestamps do not increase at frames %s" % bad[:5])
    else:
        notes.append("timestamps increase from %s to %s" % (times[0], times[-1]))

    total = sum(Decimal(f["duration_ms"]) for f in frames)
    if total != end_ms:
        problems.append("durations add up to %s, end_ms is %s" % (total, end_ms))
    else:
        notes.append("durations add up to %s ms" % total)

    width = int(manifest["frame_rect"]["w"])
    height = int(manifest["frame_rect"]["h"])
    wrong = []
    for frame in frames:
        path = os.path.join(out_dir, frame["file"])
        if not os.path.exists(path):
            problems.append("missing frame file %s" % frame["file"])
            continue
        if _headers(path) != (width, height):
            wrong.append(frame["file"])
    if wrong:
        problems.append("%d frames are not %dx%d, first %s" % (len(wrong), width, height, wrong[0]))
    else:
        notes.append("all %d frames are %dx%d" % (len(frames), width, height))

    source_dir = manifest["input"]["source_dir"]
    rect = (int(manifest["frame_rect"]["x"]), int(manifest["frame_rect"]["y"]), width, height)
    by_time = {}
    for i, frame in enumerate(frames):
        by_time.setdefault(Decimal(frame["t_ms"]), i)
    # Two poses can share one t_ms (the capture recorded them at the same
    # millisecond even though they are distinct images). Only one sample can
    # ever sit at that instant, and the pipeline resolves the tie in favour
    # of the later pose in authoring order, the one still current when the
    # next transition begins. Check that same, later pose; the earlier one
    # at a shared time has no sample of its own to be copied into.
    poses_by_t = {}
    for pose in manifest["input"]["poses"]:
        poses_by_t.setdefault(Decimal(pose["t_ms"]), []).append(pose)
    copied = 0
    for pose_t, group in poses_by_t.items():
        if pose_t not in by_time:
            continue
        pose = group[-1]
        frame = frames[by_time[pose_t]]
        if frame["kind"] != "copy" or list(frame["source"]) != [pose["index"]]:
            problems.append("pose %d at %s ms is not copied, frame %s is %s"
                            % (pose["index"], pose_t, frame["file"], frame["kind"]))
            continue
        want = bytes(images.compose(os.path.join(source_dir, pose["image"]),
                                    {k: int(pose[k]) for k in ("x", "y", "w", "h")}, rect))
        got = png.read(os.path.join(out_dir, frame["file"]))[3]
        if bytes(got) != want:
            problems.append("frame %s does not match pose %d pixel for pixel"
                            % (frame["file"], pose["index"]))
        else:
            copied += 1
    notes.append("%d authored pose times land on a sample and all copy their pose" % copied)

    first_pose_t = min(Decimal(p["t_ms"]) for p in manifest["input"]["poses"])
    late_blanks = [f["file"] for f in frames
                   if f["kind"] == "blank" and Decimal(f["t_ms"]) >= first_pose_t]
    if late_blanks:
        problems.append("%d empty frames sit at or after the first pose, first %s"
                        % (len(late_blanks), late_blanks[0]))
    else:
        notes.append("%d empty frames, all before the first pose at %s ms"
                     % (sum(1 for f in frames if f["kind"] == "blank"), first_pose_t))

    resolved = manifest["input"]["resolved"]
    if resolved and os.path.exists(resolved):
        with open(resolved) as fh:
            spec = json.load(fh)
        want_sounds = [{"pose": p["index"], "t_ms": p["t_ms"], "sound": p["sound"]}
                       for p in spec["poses"] if p.get("sound")]
        want_pre = spec.get("pre_sounds") or []
        got_pre = json.loads(json.dumps(manifest["pre_sounds"], default=str))
        if json.loads(json.dumps(want_pre)) != got_pre:
            problems.append("the manifest pre_sounds differ from %s" % resolved)
        else:
            notes.append("%d pre_sounds kept from %s" % (len(want_pre), resolved))
    else:
        want_sounds = None
    got_sounds = json.loads(json.dumps(manifest["sounds"], default=str))
    if want_sounds is not None:
        if json.loads(json.dumps(want_sounds)) != got_sounds:
            problems.append("the manifest sound events differ from %s" % resolved)
        else:
            notes.append("all %d sound events kept from %s" % (len(want_sounds), resolved))
    else:
        notes.append("%d sound events in the manifest, the input file was not on disk to compare"
                     % len(got_sounds))

    if verbose:
        for note in notes:
            print("ok   %s" % note)
        for problem in problems:
            print("FAIL %s" % problem)
    return problems
