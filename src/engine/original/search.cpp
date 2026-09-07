#include "engine/original/search.h"

#include <algorithm>
#include <random>

namespace swchess::engine::original {
namespace {

// How many entries the transposition table holds. The original sizes its own
// through xHashMalloc and masks the key with `hashmask`, so the count is a
// power of two here too.
constexpr std::size_t kTableEntries = 1u << 18;  // 262144

// How many nodes pass between two reads of the clock.
constexpr std::uint64_t kClockInterval = 2048;

// How far the capture search may run past the main search.
constexpr int kQuiescenceMax = 12;

// The window treecn opens its first iteration with. FUN_1000_8a7e sets the
// low edge at the standing evaluation minus 0x2E0 and leaves the high edge
// open.
constexpr int kFirstWindowBelow = 0x2e0;
// The window every later iteration opens around the last backed-up score.
constexpr int kWindow = 0xc0;
// How far the root narrows beta once a move beats alpha.
constexpr int kRootNarrow = 0x10;

// What a null move has to beat, by how far the game has run down. The
// original picks one of these five margins by game stage.
constexpr std::array<int, 5> kNullMargin = {0, 0x30, 0x50, 0x80, 0x180};
// The plies a null move is allowed at.
constexpr int kNullMinDepth = 1;
constexpr int kNullMaxDepth = 7;

// The Zobrist keys.
struct Zobrist {
    std::uint64_t piece[2][6][64]{};
    std::uint64_t sideToMove{0};
    std::uint64_t castling[16]{};
    std::uint64_t enPassantFile[8]{};

    Zobrist() {
        std::mt19937_64 rng(0x5761724368657373ull);  // "WarChess"
        for (auto& color : piece)
            for (auto& type : color)
                for (auto& square : type) square = rng();
        sideToMove = rng();
        for (auto& c : castling) c = rng();
        for (auto& f : enPassantFile) f = rng();
    }
};

const Zobrist& zobrist() {
    static const Zobrist z;
    return z;
}

std::uint64_t hashOf(const chess::Position& position) {
    const Zobrist& z = zobrist();
    std::uint64_t key = 0;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const auto piece = position.at(chess::Square{file, rank});
            if (!piece) continue;
            key ^= z.piece[static_cast<int>(piece->color)][static_cast<int>(piece->type)]
                          [rank * 8 + file];
        }
    }
    if (position.sideToMove() == chess::Color::Black) key ^= z.sideToMove;
    key ^= z.castling[position.castlingRights() & 0x0f];
    if (const auto ep = position.enPassantSquare()) key ^= z.enPassantFile[ep->file];
    return key;
}

bool isQuiet(const chess::Move& move) {
    return !move.capture && !move.enPassant && !move.promotion;
}

// How far the game has run down, 0 with a full board and 4 with almost
// nothing left. It picks the null move margin.
int gameStage(const chess::Position& position) {
    int pieces = 0;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const auto piece = position.at(chess::Square{file, rank});
            if (!piece) continue;
            if (piece->type != chess::PieceType::Pawn && piece->type != chess::PieceType::King) {
                ++pieces;
            }
        }
    }
    if (pieces >= 12) return 0;
    if (pieces >= 9) return 1;
    if (pieces >= 6) return 2;
    if (pieces >= 3) return 3;
    return 4;
}

}  // namespace

Searcher::Searcher(const Weights& weights) : weights_(weights) {
    table_.assign(kTableEntries, TableEntry{});
}

int Searcher::nextRandom() {
    // The original reads a rolling table its startup fills from the Microsoft
    // C library's generator, seeded 1 and never reseeded. This is that same
    // generator, which keeps the stream deterministic.
    random_ = random_ * 0x015a4e35u + 1u;
    return static_cast<int>((random_ >> 16) & 0x7fff) & 0xff;
}

bool Searcher::outOfTime() {
    if (aborted_) return true;
    if (stop_ != nullptr && stop_->load(std::memory_order_relaxed)) {
        aborted_ = true;
        return true;
    }
    if (std::chrono::steady_clock::now() >= deadline_) {
        aborted_ = true;
        return true;
    }
    return false;
}

void Searcher::order(const chess::Position& position, std::vector<chess::Move>& moves, int ply,
                     const chess::Move& first) const {
    // movegn hands its caller one move at a time, in this order: the hash
    // move, then pawn captures walking the enemy piece list from the most
    // valuable piece down, then piece captures of that same victim, then
    // capture-promotions, en passant, promotion pushes, castling, the two
    // killers, and last every quiet move.
    std::vector<std::pair<int, chess::Move>> scored;
    scored.reserve(moves.size());
    for (const chess::Move& move : moves) {
        int rank = 0;
        if (first.from != first.to && move == first) {
            rank = 1'000'000;
        } else if (!isQuiet(move)) {
            const auto victim = position.at(move.to);
            const auto attacker = position.at(move.from);
            const int victimValue =
                victim ? pieceValue(weights_, engineColor_, victim->color, victim->type)
                       : (move.enPassant ? weights_.opponentPiece[static_cast<int>(
                                               chess::PieceType::Pawn)]
                                         : 0);
            const int attackerValue =
                attacker ? pieceValue(weights_, engineColor_, attacker->color, attacker->type) : 0;
            // The most valuable victim first, and among equal victims the
            // cheapest attacker.
            rank = 500'000 + victimValue * 8 - attackerValue;
            if (move.promotion) {
                rank += pieceValue(weights_, engineColor_, position.sideToMove(), *move.promotion);
            }
        } else if (move.castling) {
            rank = 100'000;
        } else if (ply < static_cast<int>(killers_.size()) &&
                   (move == killers_[ply][0] || move == killers_[ply][1])) {
            rank = move == killers_[ply][0] ? 90'000 : 89'000;
        }
        scored.emplace_back(rank, move);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 0; i < moves.size(); ++i) moves[i] = scored[i].second;
}

int Searcher::quiescence(const chess::Position& position, int ply, int alpha, int beta) {
    ++nodes_;
    if ((nodes_ % kClockInterval) == 0 && outOfTime()) return 0;

    std::vector<chess::Move> moves = position.legalMoves();
    if (moves.empty()) {
        return position.inCheck() ? matedScore(ply) : weights_.drawScore;
    }

    const int standPat = evaluate(position, weights_, engineColor_, ply);
    if (ply >= kPlyMax + kQuiescenceMax) return standPat;
    if (standPat >= beta) return standPat;
    if (standPat > alpha) alpha = standPat;

    // Captures at any depth, the way the original's quiescence works. It also
    // looks at quiet checks for the first two plies past the main search.
    moves.erase(std::remove_if(moves.begin(), moves.end(),
                               [](const chess::Move& m) { return isQuiet(m); }),
                moves.end());
    order(position, moves, ply, chess::Move{});

    int best = standPat;
    for (const chess::Move& move : moves) {
        const int score = -quiescence(position.apply(move), ply + 1, -beta, -alpha);
        if (aborted_) return best;
        if (score > best) best = score;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;
    }
    return best;
}

int Searcher::alphaBeta(const chess::Position& position, int depth, int ply, int alpha, int beta,
                        bool allowNullMove, std::vector<chess::Move>& line) {
    line.clear();
    ++nodes_;
    if ((nodes_ % kClockInterval) == 0 && outOfTime()) return 0;

    // The fifty move rule ends the game before the search needs to look at it.
    if (ply > 0 && position.halfmoveClock() >= 100) return weights_.drawScore;

    const std::uint64_t key = hashOf(position);
    TableEntry& slot = table_[key & (kTableEntries - 1)];
    chess::Move hashMove{};
    if (slot.key == key) {
        hashMove = slot.move;
        if (ply > 0 && slot.depth >= depth) {
            if (slot.bound == Bound::Exact) return slot.score;
            if (slot.bound == Bound::Lower && slot.score >= beta) return slot.score;
            if (slot.bound == Bound::Upper && slot.score <= alpha) return slot.score;
        }
    }

    std::vector<chess::Move> moves = position.legalMoves();
    if (moves.empty()) {
        return position.inCheck() ? matedScore(ply) : weights_.drawScore;
    }

    const bool inCheck = position.inCheck();
    // A side in check does not spend a ply, so the line runs to its end. The
    // original does not decrement its depth counter at a checking node
    // either. The bound at PLYMAX is this port's, because a perpetual check
    // would otherwise extend without end.
    if (inCheck && ply < kPlyMax) ++depth;

    if (depth <= 0) return quiescence(position, ply, alpha, beta);

    // A null move, with the reduction of one ply the original uses. It is
    // allowed only in the shallow part of the tree, out of check, and only
    // when the side to move is already well past beta.
    if (allowNullMove && !inCheck && depth >= kNullMinDepth && depth <= kNullMaxDepth &&
        beta < kMateThreshold) {
        const int margin = kNullMargin[gameStage(position)];
        if (evaluate(position, weights_, engineColor_, ply) - margin > beta) {
            // Passing the move means flipping the side to move and clearing
            // the en passant square, which is what a null FEN round trip does.
            std::string fen = position.fen();
            std::vector<std::string> parts;
            std::size_t at = 0;
            while (at < fen.size()) {
                const std::size_t space = fen.find(' ', at);
                parts.push_back(fen.substr(at, space - at));
                if (space == std::string::npos) break;
                at = space + 1;
            }
            if (parts.size() == 6) {
                parts[1] = parts[1] == "w" ? "b" : "w";
                parts[3] = "-";
                std::string passed = parts[0];
                for (std::size_t i = 1; i < parts.size(); ++i) passed += " " + parts[i];
                if (const auto after = chess::Position::fromFen(passed)) {
                    std::vector<chess::Move> ignored;
                    const int score = -alphaBeta(*after, depth - 2, ply + 1, -beta, -beta + 1,
                                                 false, ignored);
                    if (aborted_) return 0;
                    if (score >= beta) return score;
                }
            }
        }
    }

    order(position, moves, ply, hashMove);

    const int originalAlpha = alpha;
    int best = -kInfinity;
    chess::Move bestMove = moves.front();
    std::vector<chess::Move> childLine;

    for (std::size_t i = 0; i < moves.size(); ++i) {
        const chess::Move& move = moves[i];
        // The level throws away this share of its moves from the second ply
        // down. It is the whole of what separates Newcomer from Expert, and
        // the original does it in the move loop of FUN_1000_8c71. The first
        // move always survives, so a node never runs out of moves.
        if (i > 0 && ply >= 2 && weights_.skipPercent > 0 &&
            nextRandom() % 100 < weights_.skipPercent) {
            continue;
        }

        const chess::Position next = position.apply(move);
        // Interior nodes get a full window. The original opens no narrow
        // window below the root.
        const int score =
            -alphaBeta(next, depth - 1, ply + 1, -beta, -std::max(alpha, best), true, childLine);
        if (aborted_) return best;

        if (score > best) {
            best = score;
            bestMove = move;
            line.clear();
            line.push_back(move);
            line.insert(line.end(), childLine.begin(), childLine.end());
        }
        if (best > alpha) alpha = best;
        if (alpha >= beta) {
            if (isQuiet(move) && ply < static_cast<int>(killers_.size()) &&
                !(killers_[ply][0] == move)) {
                killers_[ply][1] = killers_[ply][0];
                killers_[ply][0] = move;
            }
            break;
        }
    }

    slot.key = key;
    slot.score = static_cast<std::int16_t>(std::clamp(best, -kInfinity, kInfinity));
    slot.depth = static_cast<std::int16_t>(depth);
    slot.move = bestMove;
    slot.bound = best <= originalAlpha ? Bound::Upper : (best >= beta ? Bound::Lower : Bound::Exact);
    return best;
}

SearchResult Searcher::run(const chess::Position& position, const SearchLimits& limits,
                           const std::atomic<bool>& stop) {
    SearchResult result;
    std::vector<chess::Move> rootMoves = position.legalMoves();
    if (rootMoves.empty()) return result;

    result.move = rootMoves.front();
    result.hasMove = true;

    stop_ = &stop;
    engineColor_ = position.sideToMove();
    aborted_ = false;
    nodes_ = 0;
    random_ = seed_;
    deadline_ = std::chrono::steady_clock::now() + limits.maxTime;
    for (auto& pair : killers_) pair = {};
    std::fill(table_.begin(), table_.end(), TableEntry{});

    if (rootMoves.size() == 1) {
        result.depth = 1;
        result.nodes = 1;
        result.principalVariation = {result.move};
        stop_ = nullptr;
        return result;
    }

    const int maxDepth = std::clamp(limits.maxDepth, 1, kMaxIteration);
    const int standing = evaluate(position, weights_, engineColor_, 0);
    int backedUp = standing;

    std::vector<chess::Move> childLine;
    std::vector<chess::Move> bestLine;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        // The first iteration opens its window at the standing evaluation
        // minus 0x2E0 and leaves the top open. Every later one opens a window
        // of 0xC0 either side of the last backed-up score.
        int alpha = depth == 1 ? standing - kFirstWindowBelow : backedUp - kWindow;
        int beta = depth == 1 ? kInfinity : backedUp + kWindow;

        int best = -kInfinity;
        chess::Move bestMove = rootMoves.front();
        bestLine.clear();
        bool failedLow = true;

        // The root is the one place the original narrows the window. Once a
        // move beats alpha, beta drops to that score plus 0x10, and a later
        // move that breaks the narrow window is searched again with the top
        // opened right up.
        for (std::size_t i = 0; i < rootMoves.size(); ++i) {
            const chess::Move& move = rootMoves[i];
            const chess::Position next = position.apply(move);
            int score = -alphaBeta(next, depth - 1, 1, -beta, -std::max(alpha, best), true,
                                   childLine);
            if (aborted_) break;
            if (score >= beta && beta < kInfinity) {
                score = -alphaBeta(next, depth - 1, 1, -kInfinity, -(best - 1), true, childLine);
                if (aborted_) break;
            }
            if (score > best) {
                best = score;
                bestMove = move;
                bestLine.clear();
                bestLine.push_back(move);
                bestLine.insert(bestLine.end(), childLine.begin(), childLine.end());
                if (best > alpha) {
                    alpha = best;
                    failedLow = false;
                    beta = best + kRootNarrow;
                }
            }
        }
        if (aborted_) break;

        // A first iteration that fell through the bottom of its window is
        // searched again with the bottom opened right up.
        if (failedLow && best <= alpha) {
            alpha = -kInfinity;
            beta = kInfinity;
            best = -kInfinity;
            for (const chess::Move& move : rootMoves) {
                const int score = -alphaBeta(position.apply(move), depth - 1, 1, -beta,
                                             -std::max(alpha, best), true, childLine);
                if (aborted_) break;
                if (score > best) {
                    best = score;
                    bestMove = move;
                    bestLine.clear();
                    bestLine.push_back(move);
                    bestLine.insert(bestLine.end(), childLine.begin(), childLine.end());
                    if (best > alpha) alpha = best;
                }
            }
            if (aborted_) break;
        }

        backedUp = best;
        result.score = best;
        result.depth = depth;
        result.move = bestMove;
        result.principalVariation = bestLine;

        // The next iteration searches the best move first.
        const auto found = std::find(rootMoves.begin(), rootMoves.end(), bestMove);
        if (found != rootMoves.end()) std::rotate(rootMoves.begin(), found, found + 1);

        // itrtim stops the iteration the moment the backed-up score shows a
        // forced mate.
        if (best > kMateThreshold) break;
    }

    result.nodes = nodes_;
    stop_ = nullptr;
    return result;
}

}  // namespace swchess::engine::original
