#include "engine/original/eval.h"

#include <array>

namespace swchess::engine::original {
namespace {

// Material, in the unit where a pawn is 100. The king carries no material
// score because both sides always have one.
constexpr int kPawn = 100;
constexpr int kKnight = 325;
constexpr int kBishop = 335;
constexpr int kRook = 500;
constexpr int kQueen = 975;

// Where each piece wants to stand, read from White's side of the board.
// Index 0 is a1 and index 63 is h8.
constexpr std::array<int, 64> kPawnSquares = {
      0,   0,   0,   0,   0,   0,   0,   0,
      5,  10,  10, -20, -20,  10,  10,   5,
      5,  -5, -10,   0,   0, -10,  -5,   5,
      0,   0,   0,  20,  20,   0,   0,   0,
      5,   5,  10,  25,  25,  10,   5,   5,
     10,  10,  20,  30,  30,  20,  10,  10,
     50,  50,  50,  50,  50,  50,  50,  50,
      0,   0,   0,   0,   0,   0,   0,   0,
};

constexpr std::array<int, 64> kKnightSquares = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -30,   5,  10,  15,  15,  10,   5, -30,
    -30,   0,  15,  20,  20,  15,   0, -30,
    -30,   5,  15,  20,  20,  15,   5, -30,
    -30,   0,  10,  15,  15,  10,   0, -30,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50,
};

constexpr std::array<int, 64> kBishopSquares = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   5,   0,   0,   0,   0,   5, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   0,  10,  10,  10,  10,   0, -10,
    -10,   5,   5,  10,  10,   5,   5, -10,
    -10,   0,   5,  10,  10,   5,   0, -10,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -20, -10, -10, -10, -10, -10, -10, -20,
};

constexpr std::array<int, 64> kRookSquares = {
      0,   0,   0,   5,   5,   0,   0,   0,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
      5,  10,  10,  10,  10,  10,  10,   5,
      0,   0,   0,   0,   0,   0,   0,   0,
};

constexpr std::array<int, 64> kQueenSquares = {
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -10,   5,   5,   5,   5,   5,   0, -10,
      0,   0,   5,   5,   5,   5,   0,  -5,
     -5,   0,   5,   5,   5,   5,   0,  -5,
    -10,   0,   5,   5,   5,   5,   0, -10,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20,
};

// The king hides behind its pawns while the queens are on, and walks to the
// middle once they come off.
constexpr std::array<int, 64> kKingMiddlegameSquares = {
     20,  30,  10,   0,   0,  10,  30,  20,
     20,  20,   0,   0,   0,   0,  20,  20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
};

constexpr std::array<int, 64> kKingEndgameSquares = {
    -50, -30, -30, -30, -30, -30, -30, -50,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -20, -10,   0,   0, -10, -20, -30,
    -50, -40, -30, -20, -20, -30, -40, -50,
};

// What a passed pawn is worth, by how far it has run. Index 0 is the pawn's
// own second rank.
constexpr std::array<int, 8> kPassedPawn = {0, 5, 15, 30, 60, 100, 150, 0};

constexpr int kDoubledPawn = -12;
constexpr int kIsolatedPawn = -18;
constexpr int kBackwardPawn = -8;
constexpr int kBishopPair = 40;
constexpr int kRookOpenFile = 22;
constexpr int kRookHalfOpenFile = 10;
constexpr int kKingShieldPawn = 8;
constexpr int kMobilityStep = 2;
constexpr int kTempo = 8;

const std::array<int, 64>& squareTable(chess::PieceType type, bool endgame) {
    switch (type) {
        case chess::PieceType::Pawn: return kPawnSquares;
        case chess::PieceType::Knight: return kKnightSquares;
        case chess::PieceType::Bishop: return kBishopSquares;
        case chess::PieceType::Rook: return kRookSquares;
        case chess::PieceType::Queen: return kQueenSquares;
        case chess::PieceType::King: return endgame ? kKingEndgameSquares : kKingMiddlegameSquares;
    }
    return kPawnSquares;
}

// The board a side reads from its own end. White reads a1 as index 0, Black
// reads a8 as index 0, so one table serves both.
int tableIndex(chess::Square square, chess::Color color) {
    const int rank = color == chess::Color::White ? square.rank : 7 - square.rank;
    return rank * 8 + square.file;
}

// Everything the scan of the board collects, for one side.
struct SideCounts {
    int material{0};
    int placement{0};
    int bishops{0};
    int pieces{0};             // knights, bishops, rooks and queens
    std::array<int, 8> pawnsOnFile{};
    std::array<int, 8> mostAdvancedPawn{};  // the rank the side has reached
    std::array<int, 8> leastAdvancedPawn{};
    chess::Square king{4, 0};
    int rooksOnFile[8]{};
};

// Scales a term by a style weight from the .CMP file. The shipped levels use
// weights near 100, so a weight of 100 leaves the term alone.
int scaled(int term, int weight) {
    if (weight <= 0) return 0;
    return term * weight / 100;
}

}  // namespace

int pieceValue(chess::PieceType type) {
    switch (type) {
        case chess::PieceType::Pawn: return kPawn;
        case chess::PieceType::Knight: return kKnight;
        case chess::PieceType::Bishop: return kBishop;
        case chess::PieceType::Rook: return kRook;
        case chess::PieceType::Queen: return kQueen;
        case chess::PieceType::King: return 0;
    }
    return 0;
}

int evaluate(const chess::Position& position, const StyleWeights& style) {
    std::array<SideCounts, 2> side{};
    for (int c = 0; c < 2; ++c) {
        side[c].mostAdvancedPawn.fill(-1);
        side[c].leastAdvancedPawn.fill(8);
    }

    // One pass over the board collects material, placement and the pawn
    // skeleton both sides are judged on.
    int majorMaterial = 0;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const chess::Square square{file, rank};
            const auto piece = position.at(square);
            if (!piece) continue;
            SideCounts& me = side[static_cast<int>(piece->color)];
            me.material += pieceValue(piece->type);
            if (piece->type != chess::PieceType::Pawn && piece->type != chess::PieceType::King) {
                ++me.pieces;
                majorMaterial += pieceValue(piece->type);
            }
            switch (piece->type) {
                case chess::PieceType::Pawn: {
                    ++me.pawnsOnFile[file];
                    const int forward =
                        piece->color == chess::Color::White ? rank : 7 - rank;
                    me.mostAdvancedPawn[file] = std::max(me.mostAdvancedPawn[file], forward);
                    me.leastAdvancedPawn[file] = std::min(me.leastAdvancedPawn[file], forward);
                    break;
                }
                case chess::PieceType::Bishop: ++me.bishops; break;
                case chess::PieceType::Rook: ++me.rooksOnFile[file]; break;
                case chess::PieceType::King: me.king = square; break;
                default: break;
            }
        }
    }

    // The kings come out once most of the pieces are gone. The original picks
    // its king table the same way, off the material still on the board.
    const bool endgame = majorMaterial <= 2 * (kRook + kKnight);

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const auto piece = position.at(chess::Square{file, rank});
            if (!piece) continue;
            SideCounts& me = side[static_cast<int>(piece->color)];
            me.placement += squareTable(piece->type, endgame)
                                [tableIndex(chess::Square{file, rank}, piece->color)];
        }
    }

    std::array<int, 2> score{};
    for (int c = 0; c < 2; ++c) {
        SideCounts& me = side[c];
        const SideCounts& them = side[1 - c];
        int value = scaled(me.material, style.material == 0 ? 100 : style.material * 4);
        value += me.placement;

        for (int file = 0; file < 8; ++file) {
            const int count = me.pawnsOnFile[file];
            if (count == 0) continue;
            if (count > 1) value += kDoubledPawn * (count - 1);

            const bool neighbourLeft = file > 0 && me.pawnsOnFile[file - 1] > 0;
            const bool neighbourRight = file < 7 && me.pawnsOnFile[file + 1] > 0;
            if (!neighbourLeft && !neighbourRight) value += kIsolatedPawn;

            // A pawn no neighbour can catch up to is backward.
            const int least = me.leastAdvancedPawn[file];
            const int leftLeast = neighbourLeft ? me.leastAdvancedPawn[file - 1] : 8;
            const int rightLeast = neighbourRight ? me.leastAdvancedPawn[file + 1] : 8;
            if ((neighbourLeft || neighbourRight) && least < leftLeast && least < rightLeast) {
                value += kBackwardPawn;
            }

            // A passed pawn has no enemy pawn ahead of it on its own file or
            // on either neighbour.
            const int reach = me.mostAdvancedPawn[file];
            bool blocked = false;
            for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f) {
                if (them.mostAdvancedPawn[f] >= 0 && 7 - them.leastAdvancedPawn[f] > reach) {
                    blocked = true;
                }
            }
            if (!blocked && reach >= 1) value += kPassedPawn[std::min(reach, 7)];
        }

        if (me.bishops >= 2) value += kBishopPair;

        for (int file = 0; file < 8; ++file) {
            if (me.rooksOnFile[file] == 0) continue;
            if (me.pawnsOnFile[file] == 0 && them.pawnsOnFile[file] == 0) {
                value += kRookOpenFile * me.rooksOnFile[file];
            } else if (me.pawnsOnFile[file] == 0) {
                value += kRookHalfOpenFile * me.rooksOnFile[file];
            }
        }

        // The pawns standing in front of the king shelter it while the board
        // is still full.
        if (!endgame) {
            int shield = 0;
            for (int f = std::max(0, me.king.file - 1); f <= std::min(7, me.king.file + 1); ++f) {
                if (me.pawnsOnFile[f] > 0) ++shield;
            }
            value += scaled(shield * kKingShieldPawn, style.defense);
        }

        score[c] = value;
    }

    const int stm = static_cast<int>(position.sideToMove());
    int total = score[stm] - score[1 - stm];

    // Mobility counts the moves the side to move has. Counting it for both
    // sides would cost a second move generation at every node, and the
    // original does not pay for that either.
    total += scaled(static_cast<int>(position.legalMoves().size()) * kMobilityStep,
                    style.mobility * 5);
    total += kTempo;
    // The attack weight leans the whole score toward whoever is moving.
    total += scaled(total / 32, style.attack);

    return std::clamp(total, -(kInfinity - 64), kInfinity - 64);
}

}  // namespace swchess::engine::original
