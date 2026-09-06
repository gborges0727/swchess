// Builds the walk animation of one piece in one compass direction.
//
// A walking piece reads two files. <PIECE>.INI holds one section per
// direction, and <PIECE>.DLL holds one bitmap per step. Section 10 of
// docs/research/capture-player.md places this code in segment 14 of
// XCHESS.EXE, starting at FUN_1068_01d5, and reports that the walker draws
// through the sprite layer in segment 4 rather than the capture compositor.
// The two players share the frame delay, the busy wait against timeGetTime,
// and nothing that draws.
//
// A direction section looks like this, from AT.INI:
//
//   [S]
//   001=0,2
//   002=0,6
//   count=16
//
// Each numbered key holds two numbers separated by a comma. The first is how
// far right the piece moves on that step, the second is how far down. Both
// are pixels on the 640 by 480 screen, and both may be negative. [N] runs its
// dy negative and [S] runs its dy positive, which is why the y axis points
// down. This module reports the two numbers per step and the running sum, and
// it never turns a board square into a pixel. Another module owns that.
//
// The bitmap for a step is one higher than the INI key that moves it. INI key
// 001 of AT's [S] draws AT_S002, and rotation key 000 draws AT_R001. That
// holds for every section of all twelve pieces.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets/piece_dll.h"

namespace swchess::anim {

// The eight compass directions the piece INIs name.
enum class Direction {
    N,
    NE,
    E,
    SE,
    S,
    SW,
    W,
    NW,
};

// All eight, in the order this header lists them.
extern const Direction kDirections[8];

// The INI section name of a direction, for example "NE".
const char* directionName(Direction direction);

// Reads a section name such as "ne" or "NE" as a direction. Returns false
// when the text names none of the eight.
bool parseDirection(const std::string& text, Direction* direction);

// The section name of the rotation set. A rotating piece turns in place, so
// the original names its bitmaps with "%s_R%03d" instead of a direction.
inline constexpr const char* kRotationSection = "R";

// One step of a walk.
struct WalkStep {
    std::size_t index = 0;  // position in the sequence, counting from 0
    std::string key;        // the INI key, for example "001"
    int keyNumber = 0;      // that key as a number
    int dx = 0;             // pixels right on this step, negative for left
    int dy = 0;             // pixels down on this step, negative for up
    std::string raw;        // the value the INI holds, for example "0,2"
    // True when the value did not hold two numbers. DV.INI [E] writes
    // "006=10" with no comma, so dy stays 0 and this flag says so.
    bool malformed = false;

    std::string resourceName;             // "AT_S002"
    const PieceBitmap* bitmap = nullptr;  // points into the cached PieceDll
    int width = 0;
    int height = 0;

    int cumulativeDx = 0;  // sum of dx through this step
    int cumulativeDy = 0;
};

// Every step of one direction section, ready to play.
struct WalkSequence {
    std::string piece;    // "AT"
    std::string section;  // "S", "NE" or "R"
    std::string iniPath;
    std::string dllPath;

    bool present = false;      // the INI holds this section
    int declaredCount = 0;     // the `count` key
    std::size_t iniStepCount = 0;  // numbered keys the section lists

    std::vector<WalkStep> steps;  // exactly declaredCount of them
    int totalDx = 0;
    int totalDy = 0;

    std::int64_t frameDelayMs = 120;  // CM.INI [defaults] frame_delay

    // True when the section lists more numbered keys than `count` claims.
    // AT.INI [W] lists 16 and declares 15. The loader drops the extras the
    // way the original does, and this flag records that it happened.
    bool extraKeysIgnored = false;
};

// Reads `cdDir`/<piece>.INI and <piece>.DLL and builds the walk.
//
// How the loader resolves a `count` that disagrees with the file: the
// original reads `count` and loops that many times, so it never looks past
// the count. The loader walks the section's numbered keys in file order and
// keeps the first `count` of them. Extra keys past the count are ignored,
// which is what AT.INI [W] needs, and a section that lists fewer numbered
// keys than it declares throws. Extra bitmaps in the DLL are ignored the same
// way, which is what LO's [S] and [NW] need. A missing bitmap throws.
//
// Throws std::runtime_error when a file is missing, when the section lists
// too few keys, or when a step names a bitmap the DLL does not hold.
WalkSequence loadWalk(const std::string& cdDir, const std::string& piece, Direction direction);

// Reads the rotation set, the frames a piece turns through in place. Returns
// a sequence with `present` false when the piece INI has no [R] section.
WalkSequence loadRotation(const std::string& cdDir, const std::string& piece);

// Reads any section by name. The eight compass directions and "R" go through
// the two calls above. This one also reaches the four sections the compass
// set leaves out: [US], [UN], [DN] and [DS], which YO and R2 use.
WalkSequence loadWalkSection(const std::string& cdDir, const std::string& piece,
                             const std::string& section);

// Decodes `cdDir`/<piece>.DLL once and hands out the same copy after that.
// The returned reference lives as long as the process.
const PieceDll& sharedPieceDll(const std::string& cdDir, const std::string& piece);

// How WalkPlayer turns the INI steps into screen positions.
//
// IniSteps adds the INI dx and dy as the file writes them. The piece ends
// wherever the steps put it, which is not always the target square, because
// nothing in the research note says the original scales them. This is the
// default until segment 14 is decompiled.
//
// ScaleToTarget stretches the cumulative displacement so the last step lands
// on the target exactly. The shape of the walk survives and the length
// changes.
enum class WalkFit {
    IniSteps,
    ScaleToTarget,
};

const char* walkFitName(WalkFit fit);

// The bitmap the caller should draw and where to put it.
struct WalkDraw {
    bool visible = false;
    const PieceBitmap* bitmap = nullptr;
    int x = 0;  // the piece's anchor after this step, in screen pixels
    int y = 0;
    int width = 0;
    int height = 0;
    std::size_t stepIndex = 0;
};

struct WalkUpdate {
    WalkDraw draw;
    bool finished = false;
};

// Runs one WalkSequence against the same clock CapturePlayer uses.
//
// The caller hands every advance the current animation time in
// milliseconds. Step i stands on the screen from i * frame_delay until the
// next step replaces it, so a caller that advances once per display refresh
// and a caller that advances every millisecond draw the same steps.
class WalkPlayer {
public:
    WalkPlayer() = default;

    void setFit(WalkFit fit) { fit_ = fit; }
    WalkFit fit() const { return fit_; }

    // Walk `sequence` from (fromX, fromY) to (toX, toY), starting at wall
    // time `nowMs`. The sequence must outlive the player. Passing null stops
    // the player.
    void start(const WalkSequence* sequence, int fromX, int fromY, int toX, int toY,
               std::int64_t nowMs);

    // Move the clock to `nowMs` and report the step on screen. Time never
    // runs backwards here: a smaller `nowMs` than the last one leaves the
    // clock where it was.
    WalkUpdate advance(std::int64_t nowMs);

    // End the walk now and put the piece on its last step.
    void skip();

    bool isFinished() const { return finished_; }
    bool isRunning() const { return sequence_ != nullptr && !finished_; }

    std::int64_t elapsedMs() const { return elapsedMs_; }
    // How long the whole walk runs: one frame delay per step.
    std::int64_t durationMs() const { return durationMs_; }

    const WalkSequence* sequence() const { return sequence_; }

    // Where each step puts the piece, one entry per step.
    const std::vector<WalkDraw>& positions() const { return positions_; }

    // How far the last step misses the target by. Zero on both axes under
    // ScaleToTarget, and whatever the INI steps leave over under IniSteps.
    int residualX() const { return residualX_; }
    int residualY() const { return residualY_; }

private:
    WalkDraw drawAt(std::int64_t ms) const;

    const WalkSequence* sequence_ = nullptr;
    WalkFit fit_ = WalkFit::IniSteps;
    std::vector<WalkDraw> positions_;
    std::int64_t startedMs_ = 0;
    std::int64_t elapsedMs_ = 0;
    std::int64_t durationMs_ = 0;
    int residualX_ = 0;
    int residualY_ = 0;
    bool finished_ = true;
};

}  // namespace swchess::anim
