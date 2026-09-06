// Reads the 60 frames per second sequence tools/interp writes for one capture.
//
// The tool places every pose of a capture on one shared rectangle, samples the
// timeline every 1000/60 milliseconds, and asks RIFE for the pictures between
// poses. It writes those pictures as straight RGBA PNGs plus a manifest.json
// that records the time, the duration and the origin of each one, and copies
// the capture's sound cues across untouched. tools/interp/README.md describes
// the run. This reader loads the manifest and every frame into memory so the
// presentation loop never touches the disk.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "anim/capture.h"
#include "anim/png_read.h"

namespace swchess::anim {

// How the tool made one frame.
//
// Blank is an empty canvas before the first pose. Copy is a pose byte for
// byte. Interp is a picture RIFE made between two poses. Hold is the last
// pose standing still.
enum class InterpKind {
    Blank,
    Copy,
    Interp,
    Hold,
};

const char* interpKindName(InterpKind kind);

// One picture the player can put on the canvas.
struct InterpFrame {
    std::string file;          // "frame00123.png", relative to the interp60 directory
    double tMs = 0.0;          // when this picture replaces the one before it
    double durationMs = 0.0;   // how long it stands, until the next tMs
    InterpKind kind = InterpKind::Blank;
    std::vector<int> source;   // the pose indexes it came from, empty when blank
    double step = 0.0;         // where between the two poses, 0 when not interpolated
    bool hasStep = false;      // false when the manifest wrote null
    PngImage image;            // straight RGBA, frameRect.width by frameRect.height
};

// One sound cue the manifest copied from the capture timeline.
struct InterpSound {
    std::size_t poseIndex = 0;  // the pose that carries it, 0 for a pre-sound
    std::string name;           // the WAVE resource name, for example "CLANK1.WAV"
    std::string mode;           // "async", "sync" or "wait_previous"
    std::int64_t durationMs = 0;
    std::int64_t startMs = 0;
};

// Everything one interpolated capture needs to play.
struct InterpSequence {
    std::string name;              // "BBWB"
    std::string directory;         // the interp60 directory the frames came from
    int fps = 60;
    CanvasRect canvas;             // the game canvas, 640 by 480
    CanvasRect frameRect;          // where every frame sits inside the canvas
    std::int64_t endMs = 0;        // when the last picture is erased

    std::vector<InterpSound> sounds;      // one per sounding pose, in time order
    std::vector<InterpSound> preSounds;   // cues the capture starts with
    bool hasFinalSound = false;
    InterpSound finalSound;               // the [NAME_OFFSET] cue, when there is one

    std::vector<InterpFrame> frames;      // in time order, frames.front().tMs is 0

    // The frame standing at `ms`, or nullptr before the first frame and at or
    // past endMs.
    const InterpFrame* frameAt(double ms) const;
};

// Reads `assetsDir`/captures/`captureName`/interp60/manifest.json and every
// frame it names. Returns nothing when that manifest does not exist, which is
// how a capture the background run has not reached yet reports itself.
// Throws std::runtime_error when the manifest is there but unreadable.
std::optional<InterpSequence> loadInterp(const std::string& assetsDir,
                                         const std::string& captureName);

}  // namespace swchess::anim
