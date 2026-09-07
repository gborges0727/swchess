// The readers for the two settings files the engine plays by.
//
// A .CMP file is one Chessmaster personality. The five that ship are the five
// play levels. The front end XCHESS.EXE reads one at 1048:1b9d, converts the
// fields, and pokes 68 bytes to the engine as the DDE command
// ePersonalitySet. The engine process CHESSAPP.EXE never opens a file.
//
// CMWIN.DAT holds the front end's own settings, and the clock the engine
// thinks on is in there rather than in the .CMP file. XCHESS.EXE reads it at
// 1008:37bc and insists it is exactly 144 bytes.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "chess.h"

namespace swchess::engine::original {

// Six piece values in tenths of a pawn, in the order a .CMP file writes them.
// The first is what the personality editor calls Own Center Pawn. The engine
// maps it to the king and then skips it, so it changes nothing.
// The other five line up with chess::PieceType, which also runs king, queen,
// rook, bishop, knight, pawn.
struct PieceValues {
    std::int16_t centerPawn{10};  // 0x22 and 0x2E
    std::int16_t queen{90};       // 0x24 and 0x30
    std::int16_t rook{50};        // 0x26 and 0x32
    std::int16_t bishop{30};      // 0x28 and 0x34
    std::int16_t knight{30};      // 0x2A and 0x36
    std::int16_t pawn{10};        // 0x2C and 0x38

    // Reads one of the six by chess::PieceType.
    std::int16_t of(chess::PieceType type) const;

    friend bool operator==(const PieceValues&, const PieceValues&) = default;
};

// The values every shipped personality is measured against. XCHESS.EXE holds
// them at 11d8:2257 and turns each .CMP word into a percentage of its default
// before sending it on.
constexpr PieceValues kDefaultPieceValues{10, 90, 50, 30, 30, 10};

// One .CMP file, field by field.
struct Personality {
    std::string title;        // 0x00, 32 bytes. The level is found by this,
                              // not by the file name.
    std::uint16_t magic{0};   // 0x20, 0x201A in all five. Nothing checks it.
    PieceValues own{};        // 0x22, what the engine's own pieces are worth
    PieceValues opponent{};   // 0x2E, what the other side's pieces are worth

    // 0x3A. The engine scores a draw at (contempt - 2) * 5000. All five files
    // hold 2, so a draw is worth nothing to any shipped level.
    std::int16_t contempt{2};

    // 0x3C, in full moves. The engine refuses a book move past twice this.
    std::int16_t bookMoves{0};

    // 0x3E, 0 to 60. The engine throws away (60 - accuracy) percent of the
    // moves it generates, at random, from the second ply down. This is what
    // makes a low level play badly. It does not search less deeply.
    std::int16_t accuracy{60};

    // 0x40. The engine takes 100 minus this, then knocks
    // pawnValue * that / 400 off every knight, bishop, rook and queen. A low
    // setting makes the engine give material away, which is how Kamikaze
    // earns its name.
    std::int16_t pieceVersusPawn{100};

    // 0x42. The engine takes 100 minus this, then multiplies every piece by
    // 1 + that / 150. A low setting makes the engine count material heavily.
    std::int16_t materialWeight{100};

    std::string name;         // 0x44, 32 bytes, the name the player sees
    std::uint8_t playerType{2};  // 0x64, 1 a human, 2 the computer
    std::uint8_t ponder{0};      // 0x65, 1 lets the engine think on the
                                 // human's clock

    // How many plies of book the level allows.
    int bookPlies() const { return bookMoves * 2; }
    // What share of generated moves the engine throws away, 0 to 100.
    int skipPercent() const;

    // Reads the file. Throws std::runtime_error when it is missing or is not
    // 102 bytes.
    static Personality load(const std::string& path);
};

// The size of every shipped .CMP file.
constexpr std::size_t kPersonalitySize = 102;

// The clock and depth settings from CMWIN.DAT. Every shipped level thinks on
// the same clock, because the level file carries no clock of its own.
struct TimeControl {
    // Offset 0x64. 501 means a fixed number of seconds a move, 502 means a
    // fixed depth. The shipped file says 501.
    std::uint16_t mode{501};
    std::int16_t secondsPerMove{5};  // 0x66
    std::int16_t fixedDepth{4};      // 0x6C, used only in mode 502

    static constexpr std::uint16_t kSecondsPerMove = 501;
    static constexpr std::uint16_t kFixedDepth = 502;

    // Reads the file. Throws std::runtime_error when it is missing or is not
    // 144 bytes.
    static TimeControl load(const std::string& path);
};

// The size of CMWIN.DAT. XCHESS.EXE refuses any other size.
constexpr std::size_t kTimeControlSize = 144;

}  // namespace swchess::engine::original
