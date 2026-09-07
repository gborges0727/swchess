#include "engine/original/search.h"

#include <algorithm>
#include <random>

#include "engine/original/eval.h"

namespace swchess::engine::original {
namespace {

// How many entries the transposition table holds. The original sizes its own
// table at run time through xHashMalloc and masks the key with `hashmask`,
// so the size is a power of two here too.
constexpr std::size_t kTableEntries = 1u << 18;  // 262144

// How many nodes pass between two reads of the clock. The original checks the
// clock in the routine its debug strings call chk_time.
constexpr std::uint64_t kClockInterval = 2048;

// The deepest the capture-only search goes past the main search.
constexpr int kQuiescenceMax = 8;

int squareIndex(chess::Square s) { return s.rank * 8 + s.file; }

// The Zobrist keys. One per piece, colour and square, one for the side to
// move, four for the castling flags and eight for the en passant file.
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

// Ranks a capture by what it takes and what it takes it with, so the search
// tries winning captures before losing ones.
int captureRank(const chess::Position& position, const chess::Move& move) {
    const auto victim = position.at(move.to);
    const auto attacker = position.at(move.from);
    const int victimValue = victim ? pieceValue(victim->type)
                                   : (move.enPassant ? pieceValue(chess::PieceType::Pawn) : 0);
    const int attackerValue = attacker ? pieceValue(attacker->type) : 0;
    return victimValue * 16 - attackerValue;
}

bool isQuiet(const chess::Move& move) {
    return !move.capture && !move.enPassant && !move.promotion;
}

}  // namespace

Searcher::Searcher(const StyleWeights& style) : style_(style) {
    table_.assign(kTableEntries, TableEntry{});
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
                     const chess::Move& first) {
    const int side = static_cast<int>(position.sideToMove());
    std::vector<std::pair<int, chess::Move>> scored;
    scored.reserve(moves.size());
    for (const chess::Move& move : moves) {
        int score = 0;
        if (first.from != first.to && move == first) {
            score = 1'000'000;
        } else if (!isQuiet(move)) {
            score = 100'000 + captureRank(position, move);
            if (move.promotion) score += pieceValue(*move.promotion);
        } else if (ply < static_cast<int>(killers_.size()) &&
                   (move == killers_[ply][0] || move == killers_[ply][1])) {
            score = 90'000;
        } else {
            score = history_[side][squareIndex(move.from) * 64 + squareIndex(move.to)];
        }
        scored.emplace_back(score, move);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 0; i < moves.size(); ++i) moves[i] = scored[i].second;
}

int Searcher::quiescence(const chess::Position& position, int ply, int alpha, int beta) {
    ++nodes_;
    if ((nodes_ % kClockInterval) == 0 && outOfTime()) return 0;

    const int standPat = evaluate(position, style_);
    if (ply >= kPlyMax + kQuiescenceMax) return standPat;
    if (standPat >= beta) return standPat;
    if (standPat > alpha) alpha = standPat;

    std::vector<chess::Move> moves = position.legalMoves();
    if (moves.empty()) {
        return position.inCheck() ? -mateScore(ply) : 0;
    }
    // Only captures and promotions past the main search, the way the
    // original's own capture search works.
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
                        std::vector<chess::Move>& line) {
    line.clear();
    ++nodes_;
    if ((nodes_ % kClockInterval) == 0 && outOfTime()) return 0;

    // The fifty move rule ends the game before the search needs to look at it.
    if (ply > 0 && position.halfmoveClock() >= 100) return 0;

    const std::uint64_t key = hashOf(position);
    TableEntry& slot = table_[key & (kTableEntries - 1)];
    chess::Move hashMove{};
    if (slot.key == key) {
        hashMove = slot.move;
        if (ply > 0 && slot.depth >= depth) {
            if (slot.bound == 1) return slot.score;
            if (slot.bound == 2 && slot.score >= beta) return slot.score;
            if (slot.bound == 3 && slot.score <= alpha) return slot.score;
        }
    }

    std::vector<chess::Move> moves = position.legalMoves();
    if (moves.empty()) {
        return position.inCheck() ? -mateScore(ply) : 0;
    }

    // A side in check gets one more ply, so the search does not stop in the
    // middle of a forcing line.
    const bool inCheck = position.inCheck();
    if (inCheck) ++depth;

    if (depth <= 0) return quiescence(position, ply, alpha, beta);

    order(position, moves, ply, hashMove);

    const int originalAlpha = alpha;
    int best = -kInfinity;
    chess::Move bestMove = moves.front();
    std::vector<chess::Move> childLine;

    for (std::size_t i = 0; i < moves.size(); ++i) {
        const chess::Move& move = moves[i];
        const chess::Position next = position.apply(move);
        int score;
        if (i == 0) {
            score = -alphaBeta(next, depth - 1, ply + 1, -beta, -alpha, childLine);
        } else {
            // A narrow window first. Only a move that beats alpha is worth a
            // full window.
            score = -alphaBeta(next, depth - 1, ply + 1, -alpha - 1, -alpha, childLine);
            if (score > alpha && score < beta) {
                score = -alphaBeta(next, depth - 1, ply + 1, -beta, -alpha, childLine);
            }
        }
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
            if (isQuiet(move)) {
                if (ply < static_cast<int>(killers_.size()) && !(killers_[ply][0] == move)) {
                    killers_[ply][1] = killers_[ply][0];
                    killers_[ply][0] = move;
                }
                const int side = static_cast<int>(position.sideToMove());
                history_[side][squareIndex(move.from) * 64 + squareIndex(move.to)] +=
                    depth * depth;
            }
            break;
        }
    }

    slot.key = key;
    slot.score = static_cast<std::int16_t>(std::clamp(best, -kInfinity, kInfinity));
    slot.depth = static_cast<std::int16_t>(depth);
    slot.move = bestMove;
    slot.bound = best <= originalAlpha ? 3 : (best >= beta ? 2 : 1);
    return best;
}

SearchResult Searcher::run(const chess::Position& position, const SearchLimits& limits,
                           const std::atomic<bool>& stop) {
    SearchResult result;
    const std::vector<chess::Move> rootMoves = position.legalMoves();
    if (rootMoves.empty()) return result;

    // A single legal move needs no search at all.
    result.move = rootMoves.front();
    result.hasMove = true;

    stop_ = &stop;
    aborted_ = false;
    nodes_ = 0;
    deadline_ = std::chrono::steady_clock::now() + limits.maxTime;
    for (auto& pair : killers_) pair = {};
    for (auto& side : history_) side.fill(0);
    std::fill(table_.begin(), table_.end(), TableEntry{});

    if (rootMoves.size() == 1) {
        result.depth = 1;
        result.nodes = 1;
        result.principalVariation = {result.move};
        return result;
    }

    const int maxDepth = std::clamp(limits.maxDepth, 1, kPlyMax - 2);
    std::vector<chess::Move> line;
    for (int depth = 1; depth <= maxDepth; ++depth) {
        const int score = alphaBeta(position, depth, 0, -kInfinity, kInfinity, line);
        if (aborted_) break;

        result.score = score;
        result.depth = depth;
        result.principalVariation = line;
        if (!line.empty()) result.move = line.front();

        // The original stops its iteration as soon as the backed-up score
        // shows a forced mate. Its test reads `bkscr[1] > INF - 0x20`.
        if (isMateScore(score)) break;
    }
    result.nodes = nodes_;
    stop_ = nullptr;
    return result;
}

}  // namespace swchess::engine::original
