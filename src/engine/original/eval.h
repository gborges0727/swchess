// The position scoring ported from CHESSAPP.EXE.
//
// The original keeps its scores in the same unit a pawn is worth 100 of, and
// caps them at INF, which is 0x6400. This file keeps both.
#pragma once

#include "chess.h"
#include "engine/original/cmp.h"

namespace swchess::engine::original {

// The cap the original calls INF. A score never reaches it.
constexpr int kInfinity = 0x6400;  // 25600

// A forced mate found `ply` moves ahead scores this. The original stops its
// iteration once the backed-up score passes kInfinity - 32, which is the
// `itrtim: bkscr[1] > INF - 0x20` test in FUN_1000_849d.
constexpr int mateScore(int ply) { return kInfinity - ply; }
constexpr bool isMateScore(int score) {
    return score > kInfinity - 32 || score < -(kInfinity - 32);
}

// What one piece is worth on its own.
int pieceValue(chess::PieceType type);

// Scores the position for the side to move. A positive score means the side
// to move stands better. The style weights come from the .CMP level file and
// scale the terms the original scales.
int evaluate(const chess::Position& position, const StyleWeights& style);

}  // namespace swchess::engine::original
