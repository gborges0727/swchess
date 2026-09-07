// The capture review mode, the acceptance tool for the interpolated captures.
//
// docs/plan.md section 3 asks for synchronized original and enhanced views of
// one capture. This mode draws both halves of that comparison side by side.
// The left half plays the authored 120 ms poses and the right half plays the
// generated 60 frames per second pictures, and one animation clock drives
// both. A reviewer steps the clock, slows it down, changes the backdrop, and
// swaps the two sides to check that the difference is real.
//
//   swchess-viewer --cd original/win3x/cd --assets assets --review BBWB
//   swchess-viewer --cd original/win3x/cd --assets assets --review BBWB
//       --dump-at 5000 out.ppm
//
// The --dump-at form writes one side-by-side PPM through the software
// compositor and never opens a window, so it runs on a machine with no
// display.
#pragma once

#include <cstdint>
#include <string>

namespace swchess::app {

// What the review mode needs to run. `assetsDir` holds
// captures/<NAME>/interp60, which tools/interp writes.
struct ReviewOptions {
    std::string cdDir;
    std::string assetsDir = "assets";
    std::string capture;
    std::int64_t dumpAtMs = -1;  // below zero opens a window instead
    std::string dumpPath;
};

// Opens the review window and returns when the reviewer quits.
int runReview(const ReviewOptions& options);

// Writes one side-by-side frame at `options.dumpAtMs` and returns.
int dumpReview(const ReviewOptions& options);

}  // namespace swchess::app
