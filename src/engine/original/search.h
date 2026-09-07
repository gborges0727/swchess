// The move search ported from CHESSAPP.EXE.
//
// The original runs iterative deepening alpha-beta from the driver it calls
// `treecn` (Ghidra 1000:813c) and stops on the test it calls `itrtim`
// (1000:849d). This port keeps that shape: it deepens one ply at a time,
// checks the clock and the depth cap between iterations, and keeps the best
// move from the deepest iteration that finished.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include "chess.h"
#include "engine/original/cmp.h"

namespace swchess::engine::original {

// The deepest the original searches. Its debug line calls the bound PLYMAX
// and tests `iterct >= (PLYMAX - 2)`.
constexpr int kPlyMax = 32;

struct SearchLimits {
    int maxDepth{kPlyMax - 2};
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
    explicit Searcher(const StyleWeights& style);

    // Swaps in another level's weights. The caller keeps one Searcher for
    // the life of the engine, because the transposition table it owns is
    // several megabytes and reallocating it per move is wasted work.
    void setStyle(const StyleWeights& style) { style_ = style; }

    // Searches until the depth or the clock runs out, or until `stop` turns
    // true. Answers with the best move from the deepest finished iteration.
    // Returns hasMove == false only when the position has no legal move.
    SearchResult run(const chess::Position& position, const SearchLimits& limits,
                     const std::atomic<bool>& stop);

private:
    struct TableEntry {
        std::uint64_t key{0};
        std::int16_t score{0};
        std::int16_t depth{-1};
        std::uint8_t bound{0};  // 0 none, 1 exact, 2 lower, 3 upper
        chess::Move move{};
    };

    int alphaBeta(const chess::Position& position, int depth, int ply, int alpha, int beta,
                  std::vector<chess::Move>& line);
    int quiescence(const chess::Position& position, int ply, int alpha, int beta);
    void order(const chess::Position& position, std::vector<chess::Move>& moves, int ply,
               const chess::Move& first);
    bool outOfTime();

    StyleWeights style_;
    std::vector<TableEntry> table_;
    // Two killer moves per ply, and one history score per side and per
    // from-to square pair.
    std::array<std::array<chess::Move, 2>, kPlyMax + 8> killers_{};
    std::array<std::array<int, 64 * 64>, 2> history_{};
    const std::atomic<bool>* stop_{nullptr};
    std::chrono::steady_clock::time_point deadline_{};
    std::uint64_t nodes_{0};
    bool aborted_{false};
};

}  // namespace swchess::engine::original
