#include "engine/original/eval.h"

#include <algorithm>
#include <array>

namespace swchess::engine::original {
namespace {

// ------------------------------------------------------------------ tables
//
// Every number below was read out of CHESSAPP.EXE's data segment. The comment
// on each gives the Ghidra address it came from. Data2 offset N sits at file
// offset 0xAE00 + N in the executable.

// 1008:738C, by file. The pawn table starts at minus this, so a rook pawn is
// worth less than a centre pawn wherever it stands.
constexpr std::array<int, 8> kPawnFilePenalty = {31, 26, 21, 16, 16, 21, 26, 31};

// 1008:735C, by file, added to the king table.
constexpr std::array<int, 8> kKingFile = {0, 0, -2, -4, -4, -4, 0, 0};

// 1008:734C, by the king's own rank minus one. The engine charges this while
// the other side still has its queen, so a king that leaves its back ranks
// with an enemy queen on the board pays for it.
constexpr std::array<int, 8> kKingRankVersusQueen = {0, 0, 272, 400, 512, 576, 576, 640};

// 1008:74C2, 0x88 indexed. The losing king is driven toward a corner by this.
// The 232 at 0x57 breaks the mirror everywhere else in the table. It is what
// the bytes say, so it stays.
constexpr std::array<int, 64> kKingDrive = {
      0,  16,  48, 144, 144,  48,  16,   0,
     32,  72, 128, 208, 208, 128,  72,  32,
     56, 144, 192, 224, 224, 192, 144,  56,
    144, 216, 232, 280, 280, 232, 216, 144,
    144, 216, 232, 280, 280, 232, 216, 144,
     56, 144, 192, 224, 224, 192, 144, 232,
     32,  72, 128, 208, 208, 128,  72,  32,
      0,  16,  48, 144, 144,  48,  16,   0,
};

// 1008:75CA. What a doubled pawn costs, by how well its neighbours hold it
// up. Only the four even entries are ever reached.
constexpr std::array<int, 8> kDoubledPawn = {47, 38, 16, 4, 12, 8, 8, 8};

// 1008:75DA, by the weighted count of pieces a knight forks. A rook or a
// queen counts twice.
constexpr std::array<int, 7> kKnightFork = {0, 11, 17, 45, 161, 255, 255};

// 1008:75E8, by the count of enemy kings, rooks and queens on a clear bishop
// ray.
constexpr std::array<int, 10> kBishopRay = {0, 8, 12, 54, 192, 255, 255, 255, 255, 255};

// 1008:75B0, four blocks of four by the queen's distance to the enemy king.
// The blocks run middlegame even ply, endgame even ply, middlegame odd ply,
// endgame odd ply.
constexpr std::array<int, 16> kQueenKingDistance = {0,  224, 80, 8,  96, 48, 8,  72,
                                                   34, 8,   48, 32, 8,  47, 38, 16};

// 1008:748A, three blocks of nine, by how far a passed pawn has run. The
// engine picks the first block when nothing stands in the pawn's way.
constexpr std::array<int, 27> kPasser = {
    1104, 1152, 832, 512, 304, 0, 0, 0, 0,
     112,  128,  64,  32,   0, 0, 0, 0, 0,
      64,   64,  48,  32,   0, 0, 0, 0, 0,
};

// 1008:7320, by the side's pawn count. The engine hands this to every one of
// its pieces while its material lead is under 0xD1.
constexpr std::array<int, 6> kPawnCountBonus = {0, 160, 96, 48, 16, 0};

// 1008:7334, by piece type in the original's order, pawn first. A side whose
// total falls under 3 cannot win, and the engine clamps the score to 0 in the
// other side's favour. In chess::PieceType order the weights read:
constexpr std::array<int, 6> kWinWeight = {0, 6, 5, 2, 1, 4};

// 1008:77DC, by file. How much an open file is worth to a rook.
constexpr std::array<int, 8> kOpenFile = {16, 24, 27, 28, 27, 25, 24, 23};

// 1008:69E2, by the king's own file. The bits mark the files that shelter it.
constexpr std::array<unsigned, 8> kShelterFiles = {0x03, 0x07, 0x07, 0x07,
                                                  0x60, 0xe0, 0xc0, 0xc0};

// 1008:77EC, the eight central squares the bad-bishop scan walks, as 0x88
// codes read from White's side.
constexpr std::array<int, 8> kCentreSquares = {0x33, 0x34, 0x44, 0x43,
                                              0x23, 0x24, 0x32, 0x35};

// What one ply costs the side to move. The leaf charges it at every node.
constexpr int kTempo = 0x10;

// A bad bishop pays this for each of its own pawns sitting on a central
// square of its own colour.
constexpr int kBadBishopPawn = 18;

// A rook that bears on the enemy king along a rank or a file collects this,
// so long as the enemy queen is still there to be hit.
constexpr int kRookOnTheKing = 56;

// What a knight or a bishop pays for standing in front of its own unmoved
// pawns on its own third rank.
constexpr int kBlockedPiece = 24;
constexpr int kBlockedPieceAndPawn = 169;

// A knight past the middle of the board that no enemy pawn can reach.
constexpr int kOutpostBest = 96;
constexpr int kOutpostShielded = 40;
constexpr int kOutpostPlain = 10;

// What the pawn terms are worth.
constexpr int kPawnDefended = 12;
constexpr int kPawnBeside = 15;
constexpr int kPawnDefendsPawn = 9;
constexpr int kPawnHitsBishop = 17;
constexpr int kPawnForksTwo = 128;
constexpr int kPawnSeventh = 48;
constexpr int kPawnSixth = 28;
constexpr int kPawnClearAhead = 40;
constexpr int kPawnUnsupported = 24;
constexpr int kPawnInEnemyShelter = 32;

// The endgame starts once either side's pieces are worth less than this. With
// a queen still on the board the bar is the lower number, for both sides.
constexpr int kEndgameMaterial = 0x1000;
constexpr int kEndgameMaterialWithQueen = 0xa00;
// A deep endgame has at most this many pieces left, kings and pawns aside.
constexpr int kDeepEndgamePieces = 6;
// The margin the losing king starts getting driven to a corner at.
constexpr int kKingDriveScore = 0x3c0;

// ------------------------------------------------------------------ helpers

int index(chess::Square s) { return s.rank * 8 + s.file; }

// The rank counted from the side's own back rank, so 0 is where its king
// starts and 7 is where its pawns promote.
int ownRank(chess::Square s, chess::Color c) {
    return c == chess::Color::White ? s.rank : 7 - s.rank;
}

// The square a side sees at (its own rank r, file f).
chess::Square ownSquare(int file, int rank, chess::Color c) {
    return chess::Square{file, c == chess::Color::White ? rank : 7 - rank};
}

// Which way this side's pawns walk.
int forward(chess::Color c) { return c == chess::Color::White ? 1 : -1; }

// How far apart two squares are, counting a diagonal step as one.
int chebyshev(chess::Square a, chess::Square b) {
    return std::max(std::abs(a.file - b.file), std::abs(a.rank - b.rank));
}

bool holds(const chess::Position& p, chess::Square s, chess::Color c, chess::PieceType t) {
    if (!s.valid()) return false;
    const auto piece = p.at(s);
    return piece && piece->color == c && piece->type == t;
}

// Everything one pass over the board collects for one side.
struct Side {
    int material{0};        // everything, kings included
    int pieceMaterial{0};   // knights, bishops, rooks and queens only
    int pawnCount{0};
    int pieceCount{0};
    int winWeight{0};
    bool hasQueen{false};
    unsigned pawnFiles{0};
    std::array<int, 8> pawnsOnFile{};
    // How far this side's furthest pawn on each file has run, in its own
    // ranks, and how far its least advanced one has. -1 and 8 mean none.
    std::array<int, 8> mostAdvanced{};
    std::array<int, 8> leastAdvanced{};
    chess::Square king{4, 0};
    // What a rook is worth on each file, built the way FUN_1000_795b builds
    // the array at Data2 0x6ED6.
    std::array<int, 8> rookFile{};
};

// ------------------------------------------------------------- the terms

int pawnScore(const chess::Position& position, chess::Square square, chess::Color color,
              const Side& me, const Side& them, bool endgame, bool deepEndgame,
              bool capture) {
    int score = -kPawnFilePenalty[square.file];
    const int rank = ownRank(square, color);
    const int step = forward(color);

    // Held up from behind by another pawn, or shoulder to shoulder with one.
    bool defended = false;
    for (const int side : {-1, 1}) {
        if (holds(position, chess::Square{square.file + side, square.rank - step}, color,
                  chess::PieceType::Pawn)) {
            defended = true;
        }
    }
    bool beside = false;
    for (const int side : {-1, 1}) {
        if (holds(position, chess::Square{square.file + side, square.rank}, color,
                  chess::PieceType::Pawn)) {
            beside = true;
        }
    }
    if (defended) score += kPawnDefended;
    if (beside) score += kPawnBeside;

    // What the pawn hits.
    bool defendsPawn = false;
    int enemiesHit = 0;
    for (const int side : {-1, 1}) {
        const chess::Square target{square.file + side, square.rank + step};
        if (!target.valid()) continue;
        const auto piece = position.at(target);
        if (!piece) continue;
        if (piece->color == color) {
            if (piece->type == chess::PieceType::Pawn) defendsPawn = true;
            continue;
        }
        ++enemiesHit;
        if (piece->type == chess::PieceType::Bishop) score += kPawnHitsBishop;
    }
    if (defendsPawn) score += kPawnDefendsPawn;
    if (enemiesHit >= 2) score += kPawnForksTwo;

    // Doubled. The penalty is charged twice at a node that captures.
    if (me.pawnsOnFile[square.file] > 1) {
        const int slot = (defended ? 2 : 0) + (beside ? 4 : 0);
        score -= kDoubledPawn[slot] * (capture ? 2 : 1);
    }

    // Close to promoting, and more so with a clear square in front.
    if (rank >= 5) {
        score += rank == 6 ? kPawnSeventh : kPawnSixth;
        const chess::Square ahead{square.file, square.rank + step};
        if (ahead.valid() && !position.at(ahead)) score += kPawnClearAhead;
    }

    if (!defended && !beside && !defendsPawn) {
        // A pawn nothing holds up costs, unless a friendly pawn two ranks
        // behind on a neighbouring file can still come to it.
        bool helpComing = false;
        for (const int side : {-1, 1}) {
            const chess::Square behind{square.file + side, square.rank - 2 * step};
            if (holds(position, behind, color, chess::PieceType::Pawn)) helpComing = true;
        }
        if (!helpComing || rank <= 2) {
            score -= kPawnUnsupported;
        } else if (!endgame &&
                   (kShelterFiles[them.king.file] & (1u << square.file)) != 0) {
            score -= kPawnInEnemyShelter;
        }
    }

    // The passed pawn race, which the original runs only once the board has
    // thinned right out.
    if (deepEndgame) {
        bool passed = true;
        for (int f = std::max(0, square.file - 1); f <= std::min(7, square.file + 1); ++f) {
            if (them.mostAdvanced[f] >= 0 && 7 - them.leastAdvanced[f] > rank) passed = false;
        }
        if (passed) {
            // The engine picks its first block when the pawn's path is clear
            // and a later one when a rook stands on it.
            bool clear = true;
            for (int r = square.rank + step; r >= 0 && r < 8; r += step) {
                if (position.at(chess::Square{square.file, r})) clear = false;
            }
            const int block = clear ? 0 : 9;
            const int run = std::clamp(6 - rank, 0, 8);
            score += kPasser[block + run];
        }
    }
    return score;
}

int knightScore(const chess::Position& position, chess::Square square, chess::Color color,
                const Side& them) {
    int score = 0;
    const int rank = ownRank(square, color);
    const int step = forward(color);

    // An outpost is a square past the middle that no enemy pawn can come to.
    if (rank >= 4) {
        bool safe = true;
        for (const int side : {-1, 1}) {
            for (int r = square.rank + step; r >= 0 && r < 8; r += step) {
                if (holds(position, chess::Square{square.file + side, r}, opposite(color),
                          chess::PieceType::Pawn)) {
                    safe = false;
                }
            }
        }
        if (safe) {
            const int code = (7 - square.rank) * 16 + square.file;
            bool shielded = false;
            for (const int side : {-1, 1}) {
                if (holds(position, chess::Square{square.file + side, square.rank - step}, color,
                          chess::PieceType::Pawn)) {
                    shielded = true;
                }
            }
            if (((code + 0x11) & 0x66) == 0x44) {
                score += kOutpostBest;
            } else {
                score += shielded ? kOutpostShielded : kOutpostPlain;
            }
        }
    }

    // Standing on its own third rank in front of an unmoved pawn.
    if (rank == 2 && (square.file == 3 || square.file == 4)) {
        const bool ownPawnAhead =
            holds(position, ownSquare(square.file, 1, color), color, chess::PieceType::Pawn);
        if (ownPawnAhead) {
            const bool gPawnHome =
                holds(position, ownSquare(6, 1, color), color, chess::PieceType::Pawn);
            if (square.file == 3) {
                score -= kBlockedPieceAndPawn;
            } else {
                score -= gPawnHome ? kBlockedPieceAndPawn : kBlockedPiece;
            }
        }
    }

    // Forks. A rook or a queen on a knight's move counts twice.
    static constexpr int kLeaps[8][2] = {{1, 2},   {2, 1},   {2, -1}, {1, -2},
                                         {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    int count = 0;
    for (const auto& leap : kLeaps) {
        const chess::Square target{square.file + leap[0], square.rank + leap[1]};
        if (!target.valid()) continue;
        const auto piece = position.at(target);
        if (!piece || piece->color == color) continue;
        if (piece->type == chess::PieceType::Rook || piece->type == chess::PieceType::Queen) {
            count += 2;
        } else if (piece->type == chess::PieceType::Bishop) {
            count += 1;
        }
    }
    score += kKnightFork[std::min<int>(count, kKnightFork.size() - 1)];
    (void)them;
    return score;
}

int bishopScore(const chess::Position& position, chess::Square square, chess::Color color,
                const Side& me, bool endgame) {
    int score = 0;
    const int rank = ownRank(square, color);

    static constexpr int kDiagonals[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int seen = 0;
    for (const auto& step : kDiagonals) {
        for (int f = square.file + step[0], r = square.rank + step[1];
             f >= 0 && f < 8 && r >= 0 && r < 8; f += step[0], r += step[1]) {
            const auto piece = position.at(chess::Square{f, r});
            if (!piece) continue;
            if (piece->color != color &&
                (piece->type == chess::PieceType::King || piece->type == chess::PieceType::Rook ||
                 piece->type == chess::PieceType::Queen)) {
                ++seen;
            }
            break;  // The ray stops at the first piece either way.
        }
    }
    score += kBishopRay[std::min<int>(seen, kBishopRay.size() - 1)];

    if (rank == 2 && (square.file == 3 || square.file == 4)) {
        const bool ownPawnAhead =
            holds(position, ownSquare(square.file, 1, color), color, chess::PieceType::Pawn);
        if (ownPawnAhead) score -= kBlockedPiece;
    }

    // A bishop hemmed in by its own pawns on central squares of its colour.
    const int myShade = (square.file + square.rank) & 1;
    int blocked = 0;
    for (const int code : kCentreSquares) {
        const chess::Square centre{code & 0x0f, color == chess::Color::White ? 7 - (code >> 4)
                                                                            : code >> 4};
        if (!centre.valid()) continue;
        if (((centre.file + centre.rank) & 1) != myShade) continue;
        if (holds(position, centre, color, chess::PieceType::Pawn)) ++blocked;
    }
    score -= kBadBishopPawn * blocked * (endgame ? 2 : 1);
    (void)me;
    return score;
}

int rookScore(const chess::Position& position, chess::Square square, chess::Color color,
              const Side& me, const Side& them) {
    int score = me.rookFile[square.file];
    if (!them.hasQueen) return score;

    // A rook bearing on the enemy king along its rank or its file.
    static constexpr int kLines[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& step : kLines) {
        for (int f = square.file + step[0], r = square.rank + step[1];
             f >= 0 && f < 8 && r >= 0 && r < 8; f += step[0], r += step[1]) {
            const auto piece = position.at(chess::Square{f, r});
            if (!piece) continue;
            if (piece->color != color && piece->type == chess::PieceType::King) {
                score += kRookOnTheKing;
            }
            break;
        }
    }
    return score;
}

int queenScore(chess::Square square, const Side& them, bool endgame, int ply) {
    const int distance = chebyshev(square, them.king);
    if (distance >= 4) return 0;
    int score = (ply & 1) != 0 ? 6 : 0;
    if (endgame) score += 3;
    const int block = ((ply & 1) != 0 ? 8 : 0) + (endgame ? 4 : 0);
    return score + kQueenKingDistance[block + distance];
}

int kingScore(chess::Square square, chess::Color color, const Side& them, bool driven) {
    int score = kKingFile[square.file];
    if (them.hasQueen) score -= kKingRankVersusQueen[ownRank(square, color)];
    if (driven) score += kKingDrive[index(square)];
    return score;
}

}  // namespace

// ------------------------------------------------------------- the settings

Weights weightsFor(const Personality& personality) {
    Weights w;
    w.drawScore = -(static_cast<int>(personality.contempt - 2) * 5000 * 64) / 25;
    w.skipPercent = personality.skipPercent();
    w.bookPlies = personality.bookPlies();

    // The front end turns each .CMP word into a percentage of the default it
    // ships with, and the engine multiplies its own base value by that.
    const auto scale = [](const PieceValues& values, std::array<int, 6>& out) {
        for (int i = 0; i < 6; ++i) {
            const auto type = static_cast<chess::PieceType>(i);
            const int fallback = kDefaultPieceValues.of(type);
            const int percent =
                fallback == 0 ? 100 : values.of(type) * 100 / fallback;
            out[i] = kBasePieceValue[i] * percent / 100;
        }
        // The king is never scaled.
        out[static_cast<int>(chess::PieceType::King)] =
            kBasePieceValue[static_cast<int>(chess::PieceType::King)];
    };
    scale(personality.own, w.enginePiece);
    scale(personality.opponent, w.opponentPiece);

    // Knock the piece-against-pawn discount off the four pieces, then weigh
    // the whole table by how much this level cares about material.
    const int discount = 100 - personality.pieceVersusPawn;
    const int material = 100 - personality.materialWeight;
    for (std::array<int, 6>* table : {&w.enginePiece, &w.opponentPiece}) {
        const int pawn = (*table)[static_cast<int>(chess::PieceType::Pawn)];
        for (const chess::PieceType type :
             {chess::PieceType::Queen, chess::PieceType::Rook, chess::PieceType::Bishop,
              chess::PieceType::Knight}) {
            (*table)[static_cast<int>(type)] -= pawn * discount / 400;
        }
        for (int i = 0; i < 6; ++i) {
            if (i == static_cast<int>(chess::PieceType::King)) continue;
            (*table)[i] += (*table)[i] * material / 150;
        }
    }
    return w;
}

int pieceValue(const Weights& weights, chess::Color engineColor, chess::Color owner,
               chess::PieceType type) {
    const std::array<int, 6>& table =
        owner == engineColor ? weights.enginePiece : weights.opponentPiece;
    return table[static_cast<int>(type)];
}

int evaluate(const chess::Position& position, const Weights& weights, chess::Color engineColor,
             int ply) {
    std::array<Side, 2> side{};
    for (Side& s : side) {
        s.mostAdvanced.fill(-1);
        s.leastAdvanced.fill(8);
    }

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const chess::Square square{file, rank};
            const auto piece = position.at(square);
            if (!piece) continue;
            Side& me = side[static_cast<int>(piece->color)];
            const int value = pieceValue(weights, engineColor, piece->color, piece->type);
            me.material += value;
            me.winWeight += kWinWeight[static_cast<int>(piece->type)];
            switch (piece->type) {
                case chess::PieceType::King:
                    me.king = square;
                    break;
                case chess::PieceType::Pawn: {
                    ++me.pawnCount;
                    ++me.pawnsOnFile[file];
                    me.pawnFiles |= 1u << file;
                    const int run = ownRank(square, piece->color);
                    me.mostAdvanced[file] = std::max(me.mostAdvanced[file], run);
                    me.leastAdvanced[file] = std::min(me.leastAdvanced[file], run);
                    break;
                }
                default:
                    ++me.pieceCount;
                    me.pieceMaterial += value;
                    if (piece->type == chess::PieceType::Queen) me.hasQueen = true;
                    break;
            }
        }
    }

    // The phase. FUN_1000_1fc0 sets all three flags before it scores anything.
    const bool anyQueen = side[0].hasQueen || side[1].hasQueen;
    const int bar = anyQueen ? kEndgameMaterialWithQueen : kEndgameMaterial;
    const bool endgame = anyQueen ? (side[0].pieceMaterial < bar && side[1].pieceMaterial < bar)
                                  : (side[0].pieceMaterial < bar || side[1].pieceMaterial < bar);
    const bool deepEndgame = side[0].pieceCount + side[1].pieceCount <= kDeepEndgamePieces;

    // What a rook is worth on each file, per side.
    for (int c = 0; c < 2; ++c) {
        Side& me = side[c];
        const Side& them = side[1 - c];
        for (int file = 0; file < 8; ++file) {
            int value = 10;
            if (me.pawnsOnFile[file] == 0) value += kOpenFile[file];
            if (them.pawnsOnFile[file] == 0 && file >= 2 && file <= 5) {
                value += kOpenFile[file] >> 1;
            }
            if (them.king.file == file) value += 3;
            me.rookFile[file] = value;
        }
        // A file whose own pawn cannot move is worth nothing to a rook, and
        // its two neighbours gain a little.
        for (int file = 0; file < 8; ++file) {
            if (me.mostAdvanced[file] < 0) continue;
            const chess::Square pawn =
                ownSquare(file, me.mostAdvanced[file], static_cast<chess::Color>(c));
            const chess::Square ahead{file, pawn.rank + forward(static_cast<chess::Color>(c))};
            if (!ahead.valid() || !position.at(ahead)) continue;
            me.rookFile[file] = 0;
            if (file > 0) me.rookFile[file - 1] += 7;
            if (file < 7) me.rookFile[file + 1] += 7;
        }
    }

    // Whose king gets pushed to a corner. The engine turns this on for the
    // side that is losing badly, in FUN_1000_8b4c.
    const int lead = side[0].material - side[1].material;
    const bool driveWhite = lead < -kKingDriveScore;
    const bool driveBlack = lead > kKingDriveScore;

    std::array<int, 2> total{};
    for (int c = 0; c < 2; ++c) {
        const auto color = static_cast<chess::Color>(c);
        Side& me = side[c];
        const Side& them = side[1 - c];
        int score = me.material;

        // Every piece collects this while the side's material lead is small.
        const int flat = std::abs(lead) >= 0xd1
                             ? 16
                             : kPawnCountBonus[std::min<int>(me.pawnCount, 5)];
        score += flat * (me.pieceCount + 1);

        for (int rank = 0; rank < 8; ++rank) {
            for (int file = 0; file < 8; ++file) {
                const chess::Square square{file, rank};
                const auto piece = position.at(square);
                if (!piece || piece->color != color) continue;
                switch (piece->type) {
                    case chess::PieceType::Pawn:
                        score += pawnScore(position, square, color, me, them, endgame,
                                           deepEndgame, false);
                        break;
                    case chess::PieceType::Knight:
                        score += knightScore(position, square, color, them);
                        break;
                    case chess::PieceType::Bishop:
                        score += bishopScore(position, square, color, me, endgame);
                        break;
                    case chess::PieceType::Rook:
                        score += rookScore(position, square, color, me, them);
                        break;
                    case chess::PieceType::Queen:
                        score += queenScore(square, them, endgame, ply);
                        break;
                    case chess::PieceType::King:
                        score += kingScore(square, color, them,
                                           color == chess::Color::White ? driveWhite : driveBlack);
                        break;
                }
            }
        }
        total[c] = score;
    }

    const int mover = static_cast<int>(position.sideToMove());
    int value = total[mover] - total[1 - mover] - kTempo;

    // A side with too little left cannot win, so the engine refuses to score
    // the position in its favour.
    if (side[mover].winWeight < 3 && value > 0) value = weights.drawScore;
    if (side[1 - mover].winWeight < 3 && value < 0) value = weights.drawScore;

    return std::clamp(value, -kMateThreshold, kMateThreshold);
}

}  // namespace swchess::engine::original
