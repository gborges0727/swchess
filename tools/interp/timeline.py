"""Turn a resolved capture timeline into an exact 60 fps sample plan.

Every sample time is n * 1000 / fps milliseconds, held as a Fraction so no
rounding creeps in. A sample that lands exactly on an authored pose time copies
that pose. A sample after the last pose copies the last pose, which is the hold.
A sample inside a transition asks RIFE for s = (t - t0) / (t1 - t0). A cut named
in `cuts` stops interpolation into that pose, so the earlier pose stays on
screen until the cut time.
"""

from fractions import Fraction

COPY = "copy"
HOLD = "hold"
INTERP = "interp"


def sample_count(end_ms, fps):
    """Number of samples: ceil(end_ms * fps / 1000)."""
    total = Fraction(end_ms) * fps / 1000
    whole = total.numerator // total.denominator
    return whole + (1 if total.denominator != 1 or total != whole else 0)


def plan(spec, fps):
    """Return one entry per output frame, in time order.

    Each entry is a dict with `t` (Fraction milliseconds), `kind`, `source`
    (one pose index for a copy or a hold, two for an interpolation) and `s`
    (a Fraction for an interpolation, otherwise None).
    """
    poses = spec["poses"]
    times = [Fraction(p["t_ms"]) for p in poses]
    cuts = set(spec.get("cuts") or [])
    end = Fraction(spec["end_ms"])
    step = Fraction(1000, fps)

    entries = []
    k = 0
    for n in range(sample_count(spec["end_ms"], fps)):
        t = n * step
        while k + 1 < len(poses) and times[k + 1] <= t:
            k += 1
        if t == times[k]:
            entries.append({"t": t, "kind": COPY, "source": [poses[k]["index"]], "s": None})
        elif k + 1 >= len(poses):
            entries.append({"t": t, "kind": HOLD, "source": [poses[k]["index"]], "s": None})
        elif poses[k + 1]["index"] in cuts:
            entries.append({"t": t, "kind": HOLD, "source": [poses[k]["index"]], "s": None})
        else:
            s = (t - times[k]) / (times[k + 1] - times[k])
            entries.append({
                "t": t,
                "kind": INTERP,
                "source": [poses[k]["index"], poses[k + 1]["index"]],
                "s": s,
                "pose": k,
            })
    for i, entry in enumerate(entries):
        nxt = entries[i + 1]["t"] if i + 1 < len(entries) else end
        entry["duration"] = nxt - entry["t"]
    return entries


def uniform_gap(spec):
    """Return the pose spacing in milliseconds when every gap is the same.

    Returns None when the poses are not evenly spaced, which sends the pipeline
    down the one-process-per-sample path.
    """
    times = [p["t_ms"] for p in spec["poses"]]
    if len(times) < 2:
        return None
    gap = times[1] - times[0]
    if gap <= 0:
        return None
    for a, b in zip(times, times[1:]):
        if b - a != gap:
            return None
    return Fraction(gap)
