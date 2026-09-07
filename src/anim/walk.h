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
#include <memory>
#include <string>
#include <vector>

#include "anim/player.h"
#include "anim/png_read.h"
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
// The path holds every other pixel, so two points is four screen pixels
// a frame and a square takes three or four frames. The INI dx and dy describe
// how far the artwork's own feet move, three pixels a frame for R2-D2, and
// pacing the walk by those numbers would take a piece a second and a half to
// cross one square. The frames cycle instead, which is what a walk cycle is
// for, and the piece keeps this steady pace whatever character it is.
inline constexpr int kWalkPointsPerFrame = 2;

// One generated picture of a walk cycle.
struct Walk60Frame {
    std::string file;  // "S/frame00007.png", relative to the walk60 directory
    std::size_t step = 0;  // which hand drawn pose it follows
    std::size_t sub = 0;   // 0 for the pose itself, 1 to 5 for the ones between
    PngImage image;        // straight RGBA, the canvas size
};

// The 60 frames per second pictures of one direction, as tools/interp --walk
// writes them.
//
// The hand drawn pictures of one direction come in different sizes, so the
// tool composes every one of them onto a single canvas around the anchor the
// piece stands on. `anchorX` and `anchorY` say where that anchor sits inside
// the canvas, and the caller draws the picture from there rather than sizing
// the rectangle from the picture.
struct Walk60Sequence {
    std::string piece;      // "AT"
    std::string section;    // "S"
    std::string directory;  // the walk60 directory the frames came from
    int canvasWidth = 0;
    int canvasHeight = 0;
    int anchorX = 0;
    int anchorY = 0;
    int framesPerStep = 6;
    std::int64_t stepMs = kWalkFrameMs;
    std::size_t cycleSteps = 0;  // hand drawn poses this cycle holds

    // cycleSteps * framesPerStep pictures, in cycle order.
    std::vector<Walk60Frame> frames;

    // The picture standing `ms` after the walk started. The cycle repeats, so
    // a time past the end of it comes round again. Returns null when the
    // sequence holds no frames.
    const Walk60Frame* frameAt(std::int64_t ms) const;
};

// Reads `assetsDir`/pieces/<piece>/walk60/manifest.json and the frames of one
// direction. Returns nothing when that manifest does not exist or names no
// such direction, which is how a piece the tool has not reached yet reports
// itself. Throws std::runtime_error when the manifest is there but unreadable.
//
// The same sequence is handed out again after the first read. The cache keeps
// the last few directions and drops the rest, because one direction of one
// piece holds about three megabytes of pixels. Holding the returned pointer
// keeps those pixels alive after the cache has dropped them.
std::shared_ptr<const Walk60Sequence> sharedWalk60(const std::string& assetsDir,
                                                   const std::string& piece,
                                                   const std::string& section);

// The bitmap the caller should draw and where to put it.
struct WalkDraw {
    bool visible = false;
    // The walk frame. Null while sliding, which means the caller draws the
    // piece's ordinary sheet cell instead.
    const PieceBitmap* bitmap = nullptr;
    // The generated picture to draw instead of that bitmap, or null when the
    // walk has none. It lives as long as the player runs.
    const Walk60Frame* frame = nullptr;
    int anchorX = 0;  // where the anchor sits inside that picture
    int anchorY = 0;
    // The anchor the piece stands on. Under Original120ms this is a point of
    // the path. Under Interpolated60 it sits between two of them, on the same
    // straight line.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::size_t stepIndex = 0;   // which walk frame, counting from 0
    // Which path point the piece last stood on. Under Interpolated60 the
    // anchor above has already moved past it toward the next one.
    std::size_t pointIndex = 0;
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

    // Picks how the piece moves between the 100 ms points of the path.
    //
    // Original120ms jumps the piece eight pixels every 100 ms, which is what
    // the original does. Interpolated60 slides it between those two points by
    // the clock, so a caller drawing at 60 frames per second moves it about
    // 1.3 pixels a frame. Both take the same time and both stand on the same
    // point whenever the clock reaches a 100 ms mark.
    void setCadence(Cadence cadence) { cadence_ = cadence; }
    Cadence cadence() const { return cadence_; }

    // Hands the player the generated 60 frames per second pictures of this
    // direction. Under Interpolated60 it draws one of those every sixth of a
    // 100 ms step instead of holding one hand drawn picture for the whole
    // step. Passing null, or a cycle whose pose count differs from the
    // sequence the walk runs, goes back to the hand drawn pictures. Call it
    // before start.
    void setFrames60(std::shared_ptr<const Walk60Sequence> frames);
    const Walk60Sequence* frames60() const { return frames60_.get(); }

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
    std::shared_ptr<const Walk60Sequence> frames60_;
    bool useFrames60_ = false;
    std::vector<PathPoint> path_;
    std::vector<WalkDraw> positions_;
    Cadence cadence_ = Cadence::Original120ms;
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
