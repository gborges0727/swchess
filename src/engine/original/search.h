// The move search ported from CHESSAPP.EXE.
//
// The original's three layers are treecn, the outer control at Ghidra
// 1000:813c, treef, the node routine at 1000:8c71, and movegn, the move
// generator at 1000:4b7c. treecn deepens one ply at a time and stops on the
// test FUN_1000_849d, which the binary calls itrtim.
//
// The algorithm is fail-soft alpha-beta negamax. Interior nodes get a full
// window, so it is not principal variation search. The root alone narrows the
// window once a move beats alpha, and re-searches a move that breaks it.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include "chess.h"
#include "engine/original/eval.h"

namespace swchess::engine::original {

// The deepest the original searches. Its assertion calls the bound PLYMAX and
// tests `iterct >= (PLYMAX - 2)` against 30.
constexpr int kPlyMax = 32;
// The cap treecn puts on the iteration counter.
constexpr int kMaxIteration = 30;

struct SearchLimits {
    int maxDepth{kMaxIteration};
    std::chrono::milliseconds maxTime{5000};
};

struct SearchResult {
    chess::Move move{};
    int score{0};
    int depth{0};           // the deepest iteration that finished
    std::uint64_t nodes{0};
    std::vector<chess::Move> principalVariation;
    bool hasMove{false};
};

class Searcher {
public:
    explicit Searcher(const Weights& weights);

    // Swaps in another level's settings. The caller keeps one Searcher for
    // the life of the engine, because the transposition table it owns is
    // several megabytes and rebuilding it per move is wasted work.
    void setWeights(const Weights& weights) { weights_ = weights; }

    // Fixes the random stream the move skipping draws on, so a test can
    // repeat a search exactly.
    void setRandomSeed(std::uint32_t seed) { seed_ = seed; }

    // Searches until the depth or the clock runs out, or until `stop` turns
    // true. Answers with the best move from the deepest finished iteration.
    // Returns hasMove == false only when the position has no legal move.
    SearchResult run(const chess::Position& position, const SearchLimits& limits,
                     const std::atomic<bool>& stop);

private:
    enum class Bound : std::uint8_t { None = 0, Exact, Lower, Upper };

    struct TableEntry {
        std::uint64_t key{0};
        std::int16_t score{0};
        std::int16_t depth{-1};
        Bound bound{Bound::None};
        chess::Move move{};
    };

    int alphaBeta(const chess::Position& position, int depth, int ply, int alpha, int beta,
                  bool allowNullMove, std::vector<chess::Move>& line);
    int quiescence(const chess::Position& position, int ply, int alpha, int beta);
    void order(const chess::Position& position, std::vector<chess::Move>& moves, int ply,
               const chess::Move& first) const;
    bool outOfTime();
    // The next byte of the engine's own random stream.
    int nextRandom();

    Weights weights_;
    chess::Color engineColor_{chess::Color::White};
    std::vector<TableEntry> table_;
    // Two killer moves per ply, the way the original keeps them.
    std::array<std::array<chess::Move, 2>, kPlyMax + 16> killers_{};
    const std::atomic<bool>* stop_{nullptr};
    std::chrono::steady_clock::time_point deadline_{};
    std::uint64_t nodes_{0};
    std::uint32_t seed_{1};
    std::uint32_t random_{1};
    bool aborted_{false};
};

}  // namespace swchess::engine::original
