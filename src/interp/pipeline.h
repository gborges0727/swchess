// Builds the 60 frames a second sequence for one capture.
//
// This is the C++ port of tools/interp/pipeline.py. The run has four parts. It
// composes every pose on one padded canvas, writes the colour and alpha
// pictures RIFE reads, runs rife-ncnn-vulkan once per transition per channel,
// then divides the colour back out and writes RGBA frames plus a manifest.
//
// RIFE's directory mode maps output i to input position i * count / numframe
// and interpolates between the two inputs around it. A directory holding just
// the two poses of one transition, run with numframe = 2 * D, therefore
// answers every time step i / D in one process. D is the common denominator of
// the time steps that transition needs, so one process covers all seven or
// eight samples inside a 120 ms gap. A transition whose denominator comes out
// too large falls back to one process per sample.
//
// The frames land in a directory beside the requested one and move into place
// at the end, so an interrupted run leaves the previous sequence untouched.
#pragma once

#include <atomic>
#include <string>

#include "export/json_write.h"

namespace swchess::interp {

using swchess::exporter::Json;

// The model the pipeline was tuned against.
extern const char* const kModelName;

// A transition needing a finer time step than this computes more throwaway
// frames than it saves in process starts, so it falls back to one process per
// sample.
constexpr int kMaxDenominator = 512;

// How many rife-ncnn-vulkan processes run at once.
constexpr int kRifeProcesses = 3;

// What one run of the pipeline needs.
struct PipelineOptions {
    std::string capture;       // "BBWB"
    std::string assetsDir;     // the asset cache root
    std::string outDir;        // where the frames and the manifest go
    std::string resolvedPath;  // empty picks the default under assetsDir
    std::string rifeBinary;
    std::string modelDir;
    int fps = 60;
    int jobs = 0;         // frame writing workers, 0 means one per core
    bool keepWork = false;
    bool dryRun = false;
};

// Runs the whole pipeline and returns the manifest. Returns a null Json for a
// dry run. `cancelled` is polled between steps, and a cancelled run throws
// after removing its own working directory.
Json generate(const PipelineOptions& options, const std::atomic<bool>* cancelled);

// Rewrites one finished manifest's sound cues from the resolved.json it names
// and nothing else. The frames stay where they are and RIFE never runs.
// Returns true when the file changed.
bool refreshCues(const std::string& outDir, const std::string& resolvedPath);

}  // namespace swchess::interp
