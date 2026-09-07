// Turns a resolved capture timeline into an exact 60 frames a second plan.
//
// This is the C++ port of tools/interp/timeline.py. Every sample sits at
// n * 1000 / fps milliseconds. A sample that lands on an authored pose time
// copies that pose. A sample after the last pose copies the last pose, which
// is the hold. A sample inside a transition asks RIFE for the picture at
// s = (t - t0) / (t1 - t0). A cut named in the spec stops interpolation into
// that pose, so the earlier pose stays up until the cut. A sample before the
// first pose draws nothing, because the original spends its first frame delay
// on a pose it never puts on screen.
#pragma once

#include <string>
#include <vector>

#include "export/json_write.h"
#include "interp/fraction.h"

namespace swchess::interp {

using swchess::exporter::Json;

// How one output frame was made. The strings match the manifest.
enum class FrameKind { Blank, Copy, Hold, Interp };

const char* frameKindName(FrameKind kind);

// One planned output frame.
struct PlannedFrame {
    Fraction t;                 // when it goes up, in milliseconds
    Fraction duration;          // until the next one, or until end_ms
    FrameKind kind = FrameKind::Blank;
    std::vector<int> source;    // the pose indexes it came from
    bool hasStep = false;
    Fraction step;              // s, only for an interpolated frame
    std::size_t pose = 0;       // where the earlier pose sits in the pose list
};

// The number of samples: ceil(endMs * fps / 1000).
std::int64_t sampleCount(std::int64_t endMs, int fps);

// Plans every output frame for one resolved capture.
std::vector<PlannedFrame> planFrames(const Json& spec, int fps);

// The manifest's `sounds` list: one entry per sounding pose, in time order.
// Each one is timed by the sound's own t_ms, which for a sync cue is earlier
// than the pose that carries it.
Json soundEvents(const Json& spec);

}  // namespace swchess::interp
