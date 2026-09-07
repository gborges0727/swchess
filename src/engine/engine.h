// The computer opponent behind the engine buttons.
//
// The original game ran its engine as a second program, CHESSAPP.EXE, and the
// front end talked to it over DDE. This port runs the same engine in process.
// src/engine/original/ holds the port of CHESSAPP.EXE. This header is the
// contract the game shell programs against, so the shell can be wired up
// before the port lands and any later engine can take the same seat.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chess.h"

namespace swchess::engine {

// The five play-level buttons, in the order the bar shows them. Each one
// names a .CMP file on the CD (NEWCOMER.CMP .. EXPERT.CMP) that holds the
// original engine's settings for that level.
enum class Level { Newcomer = 0, Novice, Moderate, Hard, Expert };

const char* levelFileName(Level level);  // "NEWCOMER.CMP" and so on

struct Config {
    std::string cdDir;  // where BOOK.DAT, XBOOK.DAT and the .CMP files live
    Level level = Level::Newcomer;
};

// A thinking engine. requestMove answers on the caller's thread later, from
// poll(), never from inside requestMove itself. hint answers the same way.
class Engine : public chess::MoveProvider {
public:
    ~Engine() override = default;

    virtual void setLevel(Level level) = 0;
    virtual Level level() const = 0;

    // Asks for the move the engine would play for the side to move. The
    // answer comes through the callback from poll().
    virtual void requestHint(const chess::Position& position, chess::RequestId id,
                             chess::MoveCallback done) = 0;

    // Stops thinking and answers the open request with the best move so far.
    // This is the FORCE button.
    virtual void forceMove() = 0;

    // Runs finished callbacks on the calling thread. The shell calls this
    // once per frame.
    virtual void poll() = 0;
};

// The engine ported from CHESSAPP.EXE. Throws std::runtime_error when the
// book or level files are missing from cdDir.
std::unique_ptr<Engine> makeOriginalEngine(const Config& config);

// A stand-in that plays a random legal move after a short delay. It lets the
// shell run when the original port is not built, and the tests use it.
std::unique_ptr<Engine> makeRandomEngine(const Config& config, std::uint32_t seed = 1);

}  // namespace swchess::engine
