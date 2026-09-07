// A stand-in opponent that plays a random legal move.
//
// It exists so the game shell can run before the port of CHESSAPP.EXE is
// ready, and so the tests have an engine that never thinks for long. The
// move is picked as soon as the request arrives, but poll() withholds it
// until 300 ms have passed, which gives the board time to settle after the
// player's own move.

#include <chrono>
#include <random>
#include <stdexcept>

#include "engine/engine.h"

namespace swchess::engine {
namespace {

using Clock = std::chrono::steady_clock;

// How long the stand-in pretends to think.
constexpr auto kThinkTime = std::chrono::milliseconds(300);

class RandomEngine final : public Engine {
public:
    RandomEngine(const Config& config, std::uint32_t seed)
        : level_(config.level), rng_(seed) {}

    void requestMove(const chess::Position& position, chess::RequestId id,
                     chess::MoveCallback done) override {
        start(position, id, std::move(done));
    }

    void requestHint(const chess::Position& position, chess::RequestId id,
                     chess::MoveCallback done) override {
        start(position, id, std::move(done));
    }

    void cancel(chess::RequestId id) override {
        if (pending_ && id_ == id) clear();
    }

    void setLevel(Level level) override { level_ = level; }
    Level level() const override { return level_; }

    // Answers the open request on the next poll() instead of waiting out
    // the rest of the delay.
    void forceMove() override {
        if (pending_) ready_ = Clock::now();
    }

    void poll() override {
        if (!pending_ || Clock::now() < ready_) return;
        auto done = std::move(done_);
        auto id = id_;
        auto move = move_;
        clear();
        if (done) done(id, move);
    }

private:
    void start(const chess::Position& position, chess::RequestId id,
               chess::MoveCallback done) {
        auto moves = position.legalMoves();
        if (moves.empty()) return;  // No move exists, so the caller gets none.
        std::uniform_int_distribution<std::size_t> pick(0, moves.size() - 1);
        move_ = moves[pick(rng_)];
        id_ = id;
        done_ = std::move(done);
        ready_ = Clock::now() + kThinkTime;
        pending_ = true;
    }

    void clear() {
        pending_ = false;
        done_ = {};
        id_ = 0;
    }

    Level level_;
    std::mt19937 rng_;
    bool pending_{false};
    chess::RequestId id_{0};
    chess::Move move_{};
    chess::MoveCallback done_{};
    Clock::time_point ready_{};
};

}  // namespace

const char* levelFileName(Level level) {
    switch (level) {
        case Level::Newcomer: return "NEWCOMER.CMP";
        case Level::Novice: return "NOVICE.CMP";
        case Level::Moderate: return "MODERATE.CMP";
        case Level::Hard: return "HARD.CMP";
        case Level::Expert: return "EXPERT.CMP";
    }
    return "NEWCOMER.CMP";
}

std::unique_ptr<Engine> makeRandomEngine(const Config& config, std::uint32_t seed) {
    return std::make_unique<RandomEngine>(config, seed);
}

}  // namespace swchess::engine
