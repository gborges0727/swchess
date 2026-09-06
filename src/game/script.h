// Plays a written list of moves through GameSession with a simulated clock.
//
// Nothing here opens a window and nothing waits on real time. The runner
// steps the animation clock in fixed millisecond ticks, clicks the two
// squares of each move as a player would, and can composite one picture at a
// chosen animation time. The game executable uses it for its headless dump
// and game_test uses it for its checks.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "assets/bmp.h"
#include "game/session.h"

namespace swchess::game {

// One state the session entered while the script ran.
struct StateSample {
    std::int64_t timeMs = 0;
    AnimState state = AnimState::Idle;
    std::string move;  // the move being played when the state changed
};

struct ScriptOptions {
    std::string cdDir;
    // Where the interpolated capture frames live, empty for none.
    std::string assetsDir;
    Settings settings{};
    // Moves in long algebraic, such as "e2e4" or "e7e8q".
    std::vector<std::string> moves;
    std::int64_t dumpAtMs = -1;  // negative composites nothing
    std::string dumpPath;        // empty writes no file
    std::int64_t stepMs = 10;
    std::int64_t limitMs = 900000;
    // The clock here is simulated, so a capture must not start before its
    // interpolated frames finish loading. Waiting keeps one script drawing
    // the same pictures on every run.
    bool waitForInterp = true;
};

struct ScriptResult {
    std::vector<StateSample> states;  // one entry per state change, in order
    std::vector<std::string> played;  // the moves the rules module accepted
    std::string rejected;             // the first move it would not take
    std::string finalFen;
    std::map<std::string, int> soundPlays;
    // Every cue that started, with the animation time it started at.
    std::vector<GameSession::SoundPlay> soundLog;
    std::int64_t endedMs = 0;

    bool dumped = false;
    Image dump{};
    AnimState dumpState = AnimState::Idle;
    std::string dumpCapture;         // the capture playing at the dump time
    bool dumpHasPose = false;        // a film picture was on the screen
    bool dumpHasRecord = false;      // that picture was an authored pose
    bool dumpHasFrame = false;       // that picture was an interpolated frame
    // Which pictures the capture drew at the dump time.
    anim::Cadence dumpCadence = anim::Cadence::Original120ms;
    // "blank", "copy", "interp" or "hold" for an interpolated frame, empty
    // for an authored pose.
    std::string dumpFrameKind;
    std::size_t dumpPoseIndex = 0;
    std::uint32_t dumpRecordOffset = 0;
    int dumpX = 0;
    int dumpY = 0;
    int dumpWidth = 0;
    int dumpHeight = 0;
};

// Splits "e2e4 e7e5" into its moves. Spaces, tabs and commas separate them.
std::vector<std::string> splitScript(const std::string& text);

// Runs the script. Throws std::runtime_error when a CD file is missing.
ScriptResult runScript(const ScriptOptions& options);

}  // namespace swchess::game
