# Offline capture interpolation

This turns one Star Wars Chess capture into a 60 fps sequence of RGBA frames.
It reads the resolved timeline the extraction lane writes and the pose PNGs
beside it, calls `rife-ncnn-vulkan` for the frames between poses, and writes the
frames plus a manifest that records how each one was made.

Run the whole capture, then verify it:

    python3 -m tools.interp --capture BBWB --assets assets \
        --out assets/captures/BBWB/interp60
    python3 -m tools.interp --check assets/captures/BBWB/interp60
    python3 -m tools.interp.contact assets/captures/BBWB/interp60

`--refresh-cues <out dir>` rewrites one finished manifest's `sounds`,
`pre_sounds` and `final_wav` from the `resolved.json` it names, and updates the
hash that says which file those came from. It touches no frame and runs no
inference, so re-cueing all 72 captures after a change to the extractor takes
seconds instead of a full regeneration:

    for d in assets/captures/*/interp60; do
        python3 -m tools.interp --refresh-cues "$d"
    done

`--dry-run` prints the frame plan and calls no inference. `--fps` changes the
sample rate. `--keep-work` leaves the pictures handed to RIFE in `work/` under
the output directory, which is where to look when a frame comes out wrong.

The RIFE binary and model default to `.cache/rife/bin/rife-ncnn-vulkan` and
`.cache/rife/bin/rife-v4.6`, which `tools/rife/build.sh` produces. Pass
`--rife` and `--model` to use others.

## What the run does

1. It reads `assets/captures/<NAME>/resolved.json`. When that file is missing it
   builds the same structure from `original/win3x/cd` through `fixture.py`, so
   the pipeline runs before the extraction lane lands. The manifest says which
   of the two it used.
2. It takes the union of every pose rectangle and places every pose on that one
   canvas. BBWB comes out 198 by 271 at (215, -16) inside the 640 by 480 game
   canvas. Poses of different sizes then share a frame of reference, so a pose
   that grows cannot read as movement.
3. It writes two pictures per pose. One holds the colour composited over black,
   which is the colour multiplied by alpha. The other holds the alpha channel as
   grey. RIFE loads three colour channels and throws alpha away, so alpha has to
   travel as its own picture.
4. It samples the timeline at `n * 1000 / fps` milliseconds. A sample that lands
   on a pose time copies that pose byte for byte. A sample before the first pose
   writes an empty canvas, because the original spends its first frame delay on
   a pose it never draws. A sample after the last pose copies the last pose,
   which is the hold. Everything else asks RIFE for `s = (t - t0) / (t1 - t0)`.
5. It divides the interpolated colour by the interpolated alpha, drops any pixel
   whose alpha falls below 16, and writes `frame%05d.png` as straight RGBA.
6. It writes `manifest.json` with the tool commit, the model and binary hashes,
   the hash of every input pose, and one record per frame giving the time, the
   duration, the source poses and the time step. It copies `end_ms`, `cuts`,
   `final_wav`, `pre_sounds` and every pose sound event across untouched.

A sound event in `sounds` names the pose that carries it and the millisecond the
original starts it, which `resolved.json` puts in the sound's own `t_ms`. That is
not always the pose's own time. A `sync` sound starts at the top of its iteration
and blocks, so it starts before its pose appears: BNWN pose 16 shows at 3132 ms
and starts `GRUNT2.WAV` at 1920. The C++ player refuses an interp60 sequence
whose cue times disagree with the timeline it loaded from the CD, so an event
timed by the pose instead of the sound stops that capture from using its
interpolated frames.

## Cuts

`cuts` lists the pose indexes a cut comes before. The sampler holds the
earlier pose on screen until the cut time and then switches, so no interpolation
crosses the boundary. BBWB has no cuts, so that path is not exercised yet.

## How the RIFE calls are batched

RIFE's directory mode maps output `i` to input position `i * count / numframe`
and interpolates between the two inputs around it. A directory holding just the
two poses of one transition, run with `numframe = 2 * D`, therefore answers every
time step `i / D` in one process. `D` is the smallest number that writes all the
time steps that transition needs as whole fractions of one.

A 120 ms gap sampled at 60 fps needs 7 or 8 steps, with `D` of 36 or 360, so one
process replaces 7 or 8. A transition whose `D` passes `MAX_DENOMINATOR` would
compute more throwaway frames than it saves in process starts, so it falls back
to one process per sample. BBWB uses 226 processes for 583 interpolated frames.
Its only fallback is the 632 ms gap where the player waits for IRREGULR.WAV.

On the M4 Pro over SSH one process costs about 0.29 seconds, and each extra
frame inside it costs about 2 milliseconds.

## Files

| File | What it does |
| --- | --- |
| `__main__.py` | The command line, including `--check` and `--refresh-cues` |
| `pipeline.py` | Composes poses, drives RIFE, writes frames and the manifest |
| `timeline.py` | Turns the resolved timeline into an exact sample plan |
| `images.py` | Union canvas, alpha split, and the divide back to straight alpha |
| `png.py` | Reads and writes 8-bit PNGs with zlib alone |
| `check.py` | Verifies a finished output directory |
| `contact.py` | Draws a contact sheet of every Nth frame over a checkerboard |
| `fixture.py` | Builds a stand-in `resolved.json` from the raw files |

`png.py` repeats the writer from `tools/extract/png.py` rather than importing
it, because the extraction lane is rewriting that package. It adds a reader,
which nothing else in the repo has, to read back what RIFE wrote.

## What `--check` verifies

- The frame count equals `ceil(end_ms * fps / 1000)`.
- The timestamps increase.
- The frame durations add up to `end_ms` exactly. Times are stored to six
  decimal places and read back as decimals, not floats, so the total is exact
  rather than close. The last frame takes a short duration when the sample grid
  does not divide `end_ms`.
- Every authored pose time that coincides with a sample copies its pose pixel
  for pixel. At 60 fps against a 120 ms cadence only every fifth pose time is
  also a sample time, and BBWB shifts by 632 ms partway through, so 2 of its 79
  poses qualify.
- Every empty frame sits before the first pose.
- Every sound event, every `pre_sounds` entry and the `final_wav` survive from
  the input, each at the time the input gives it. The sound times come from the
  sound's own `t_ms` in `resolved.json`, not from the pose's.
- Every frame carries the canvas size.
