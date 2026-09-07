// The position scoring ported from CHESSAPP.EXE.
//
// The leaf evaluation is FUN_1000_9708 at Ghidra 1000:9708, and the per-piece
// scorer it leans on is FUN_1000_70b4. The search setup FUN_1000_1fc0 builds
// the piece values and the piece-square tables once per search from the
// position in front of it.
//
// A pawn is worth 256. Every score in this port is in that unit, which is
// what makes INF, the original's 0x6400, mean a hundred pawns.
#pragma once

#include <array>

#include "chess.h"
#include "engine/original/cmp.h"

namespace swchess::engine::original {

// The cap the original calls INF.
constexpr int kInfinity = 0x6400;  // 25600

// A side mated at ply p scores p - (INF - 1), so mate in one move is 0x63FE
// and a mate further off scores less. The original's stop test is
// `bkscr[1] > INF - 32`, in FUN_1000_849d.
constexpr int matedScore(int ply) { return ply - (kInfinity - 1); }
constexpr int kMateThreshold = kInfinity - 32;  // 0x63E0
constexpr bool isMateScore(int score) {
    return score > kMateThreshold || score < -kMateThreshold;
}

// What the engine believes a piece is worth, before any personality scaling.
// The table sits at Data2 0x72FC. It runs pawn, king, knight, rook, bishop,
// queen there; here it runs in chess::PieceType order.
constexpr std::array<int, 6> kBasePieceValue = {3840, 2368, 1232, 844, 784, 256};

// One level's settings, converted from its .CMP file into the numbers the
// search and the evaluation actually use.
struct Weights {
    // What each piece is worth, by chess::PieceType. The engine and its
    // opponent can hold different tables, which is how Kamikaze comes to
    // give material away.
    std::array<int, 6> enginePiece{kBasePieceValue};
    std::array<int, 6> opponentPiece{kBasePieceValue};

    // What a draw is worth to the engine. Every shipped level scores it 0.
    int drawScore{0};

    // What share of the moves the engine throws away unsearched, from the
    // second ply down. This is the whole of the difference between levels.
    int skipPercent{0};

    // How many plies of opening book the level allows.
    int bookPlies{0};
};

// Turns one .CMP personality into the numbers above, the way FUN_1000_1fc0
// does: scale each base value by the level's percentage of the default, take
// the piece-against-pawn discount off the four pieces, then apply the
// material weight to everything but the king.
Weights weightsFor(const Personality& personality);

// What one piece is worth to the side holding it.
int pieceValue(const Weights& weights, chess::Color engineColor, chess::Color owner,
               chess::PieceType type);

// Scores the position for the side to move. A positive score means the side
// to move stands better. `engineColor` says which side reads the engine's own
// piece values, and `ply` selects the terms the original splits on ply
// parity.
int evaluate(const chess::Position& position, const Weights& weights,
             chess::Color engineColor, int ply);

}  // namespace swchess::engine::original
