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


// One point of the screen line a moving piece follows.
struct PathPoint {
    int x = 0;
    int y = 0;

    friend bool operator==(const PathPoint&, const PathPoint&) = default;
};

// The pixels GDI's LineDDA hands its callback, from (x0,y0) to (x1,y1). The
// list starts on the first point and ends on the last, and it steps one pixel
// along the longer axis each time.
std::vector<PathPoint> linePoints(int x0, int y0, int x1, int y1);

// The points a moving piece actually stands on. LINEPROC at 1008:5617 tests
// the low bit of a counter and drops every other callback, so this keeps the
// points at even positions and always keeps the last one.
std::vector<PathPoint> walkPath(int x0, int y0, int x1, int y1);

// The direction section a move walks in. `fileDelta` is the column change and
// `rankDelta` is the rank change, both in board squares. A turned board
// negates both, which is what FUN_1068_0013 does. Rows grow downward on
// screen, so a move toward rank 1 walks south.
Direction walkDirection(int fileDelta, int rankDelta, bool turned);

// One walk frame lasts this long. FUN_1068_0fe6 busy-waits on timeGetTime
// until 100 milliseconds have passed, whatever CM.INI says about the capture
// frame delay.
inline constexpr std::int64_t kWalkFrameMs = 100;

// How many points of the path one frame carries the piece forward.
//
// The path holds every other pixel, so eight points is sixteen screen pixels
// a frame and a square takes three or four frames. The INI dx and dy describe
// how far the artwork's own feet move, three pixels a frame for R2-D2, and
// pacing the walk by those numbers would take a piece a second and a half to
// cross one square. The frames cycle instead, which is what a walk cycle is
// for, and the piece keeps this steady pace whatever character it is.
inline constexpr int kWalkPointsPerFrame = 8;

// The bitmap the caller should draw and where to put it.
struct WalkDraw {
    bool visible = false;
    // The walk frame. Null while sliding, which means the caller draws the
    // piece's ordinary sheet cell instead.
    const PieceBitmap* bitmap = nullptr;
    int x = 0;  // the anchor the piece stands on, a point of the path
    int y = 0;
    int width = 0;
    int height = 0;
    std::size_t stepIndex = 0;   // which walk frame, counting from 0
    std::size_t pointIndex = 0;  // which path point the piece stands on
    // How far along the path the piece has come, 0 at the first point and
    // 1 at the last. The caller mixes the two squares' depths with it.
    double progress = 0.0;
};

struct WalkUpdate {
    WalkDraw draw;
    bool finished = false;
};

// Moves one piece along the screen line between two square centers.
//
// FUN_1008_148b projects both squares, runs LineDDA between the two centers,
// and moves the piece to every second point the callback reports. With
// walking on, each point belongs to a walk frame, the frames cycle through
// the direction's sequence, and one frame stands on the screen for 100 ms.
// With walking off the piece slides along the same line with no frames.
//
// The caller hands every advance the current animation time in milliseconds,
// so a caller that advances once per display refresh and a caller that
// advances every millisecond draw the same frames at the same points.
class WalkPlayer {
public:
    WalkPlayer() = default;

    // Walk `sequence` along `path`. The sequence must outlive the player.
    // Passing null, or a sequence with no steps, slides instead. A path
    // shorter than two points finishes at once.
    void start(const WalkSequence* sequence, std::vector<PathPoint> path, std::int64_t nowMs);

    // The same, building the path between the two screen points.
    void start(const WalkSequence* sequence, int fromX, int fromY, int toX, int toY,
               std::int64_t nowMs);

    // Move the clock to `nowMs` and report the frame on screen. Time never
    // runs backwards here: a smaller `nowMs` than the last one leaves the
    // clock where it was.
    WalkUpdate advance(std::int64_t nowMs);

    // End the walk now and put the piece on the last point of the path.
    void skip();

    bool isFinished() const { return finished_; }
    bool isRunning() const { return !path_.empty() && !finished_; }
    bool isSliding() const { return sliding_; }

    std::int64_t elapsedMs() const { return elapsedMs_; }
    // How long the whole walk runs, always a whole number of frames.
    std::int64_t durationMs() const { return durationMs_; }

    const WalkSequence* sequence() const { return sequence_; }
    const std::vector<PathPoint>& path() const { return path_; }

    // Where each frame puts the piece, one entry per frame of the walk.
    const std::vector<WalkDraw>& positions() const { return positions_; }

private:
    void build();

    const WalkSequence* sequence_ = nullptr;
    std::vector<PathPoint> path_;
    std::vector<WalkDraw> positions_;
    bool sliding_ = false;
    std::int64_t startedMs_ = 0;
    std::int64_t elapsedMs_ = 0;
    std::int64_t durationMs_ = 0;
    bool finished_ = true;
};

// One pose of a piece turning in place.
//
// This is the other stepper, FUN_1068_1140. It leaves the piece on its square
// and adds the INI dx and dy to that base position, one frame per 100 ms. The
// walk between squares ignores those numbers and takes its positions from the
// LineDDA path, so this helper is the only place they still move anything.
struct TurnPose {
    const PieceBitmap* bitmap = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The poses a piece turns through, from the rotation sequence loadRotation
// reads. `baseX` and `baseY` are the anchor it stands on.
std::vector<TurnPose> turnInPlace(const WalkSequence& rotation, int baseX, int baseY);

}  // namespace swchess::anim
