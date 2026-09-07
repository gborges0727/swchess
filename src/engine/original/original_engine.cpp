// The engine ported from CHESSAPP.EXE, behind the contract in engine.h.
//
// The original was a second program. The front end started it with WinExec
// and asked it for a move over DDE, so the front end kept drawing while the
// engine thought. This port keeps that split by running the search on a
// worker thread. requestMove hands the worker a position, poll() picks up the
// answer on the caller's thread, and forceMove tells the worker to stop and
// report the best move it has.

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "engine/engine.h"
#include "engine/original/book.h"
#include "engine/original/cmp.h"
#include "engine/original/eval.h"
#include "engine/original/search.h"

namespace swchess::engine {
namespace {

using original::Book;
using original::Personality;
using original::SearchLimits;
using original::Searcher;
using original::SearchResult;
using original::TimeControl;
using original::Weights;

std::string join(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/') return dir + file;
    return dir + "/" + file;
}

// The clock and the depth the search runs against. No .CMP file carries
// either one. They come from CMWIN.DAT, so every level thinks for the same
// five seconds and none of them caps its depth. What separates the levels is
// how many moves the engine throws away unsearched and what it thinks its
// pieces are worth.
SearchLimits limitsFor(const TimeControl& control) {
    SearchLimits limits;
    if (control.mode == TimeControl::kFixedDepth && control.fixedDepth > 0) {
        limits.maxDepth = control.fixedDepth;
        limits.maxTime = std::chrono::hours(1);
        return limits;
    }
    limits.maxDepth = original::kMaxIteration;
    limits.maxTime =
        std::chrono::milliseconds(std::max<int>(control.secondsPerMove, 1) * 1000);
    return limits;
}

class OriginalEngine final : public Engine {
public:
    explicit OriginalEngine(const Config& config)
        : cdDir_(config.cdDir),
          level_(config.level),
          book_(Book::load(join(config.cdDir, "BOOK.DAT"))),
          control_(TimeControl::load(join(config.cdDir, "CMWIN.DAT"))),
          personality_(Personality::load(join(config.cdDir, levelFileName(config.level)))) {
        weights_ = original::weightsFor(personality_);
        worker_ = std::thread([this] { workerLoop(); });
    }

    ~OriginalEngine() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quitting_ = true;
            stop_.store(true, std::memory_order_relaxed);
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void requestMove(const chess::Position& position, chess::RequestId id,
                     chess::MoveCallback done) override {
        start(position, id, std::move(done), false);
    }

    void requestHint(const chess::Position& position, chess::RequestId id,
                     chess::MoveCallback done) override {
        start(position, id, std::move(done), true);
    }

    void cancel(chess::RequestId id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (jobId_ != id) return;
        cancelled_ = true;
        stop_.store(true, std::memory_order_relaxed);
        answer_.reset();
        done_ = {};
    }

    void setLevel(Level level) override {
        Personality loaded = Personality::load(join(cdDir_, levelFileName(level)));
        Weights weights = original::weightsFor(loaded);
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
        personality_ = std::move(loaded);
        weights_ = weights;
    }

    Level level() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    void forceMove() override {
        std::lock_guard<std::mutex> lock(mutex_);
        // The flag also covers a force that lands before the worker has
        // picked the job up, which would otherwise be cleared by the worker.
        forced_ = true;
        stop_.store(true, std::memory_order_relaxed);
    }

    void poll() override {
        chess::MoveCallback done;
        chess::RequestId id = 0;
        chess::Move move{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!answer_ || !done_) return;
            done = std::move(done_);
            done_ = {};
            id = jobId_;
            move = *answer_;
            answer_.reset();
        }
        done(id, move);
    }

private:
    void start(const chess::Position& position, chess::RequestId id, chess::MoveCallback done,
               bool asHint) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // A second request replaces the first, so the worker never holds
            // two jobs at once.
            stop_.store(true, std::memory_order_relaxed);
            pending_ = position;
            jobId_ = id;
            done_ = std::move(done);
            answer_.reset();
            cancelled_ = false;
            forced_ = false;
            asHint_ = asHint;
            hasJob_ = true;
        }
        wake_.notify_one();
    }

    void workerLoop() {
        while (true) {
            chess::Position position;
            Weights weights;
            bool asHint = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return hasJob_ || quitting_; });
                if (quitting_) return;
                hasJob_ = false;
                position = pending_;
                weights = weights_;
                asHint = asHint_;
                stop_.store(forced_, std::memory_order_relaxed);
            }

            // In book, the move is a book move. A move to play is spread over
            // the lines that reach the position, the way the original varied
            // its openings. A hint takes the most played line instead, so
            // asking twice gives the same answer.
            std::optional<chess::Move> chosen;
            if (book_.reaches(position, weights.bookPlies)) {
                if (asHint) {
                    const std::vector<chess::Move> fromBook = book_.probe(position);
                    if (!fromBook.empty()) chosen = fromBook.front();
                } else {
                    chosen = book_.pick(position, bookRandom_);
                }
            }
            if (!chosen) {
                searcher_.setWeights(weights);
                const SearchResult found = searcher_.run(position, limitsFor(control_), stop_);
                if (found.hasMove) chosen = found.move;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            // A cancel or a newer request while the search ran throws this
            // answer away.
            if (cancelled_ || hasJob_ || !done_) continue;
            if (!chosen) {
                // The position has no legal move, so there is nothing to
                // answer with. Drop the request rather than hold it open.
                done_ = {};
                continue;
            }
            answer_ = chosen;
        }
    }

    std::string cdDir_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::atomic<bool> stop_{false};

    Level level_;
    Book book_;
    TimeControl control_;
    Personality personality_;
    Weights weights_;
    // The worker thread owns this. Nothing else touches it.
    Searcher searcher_{Weights{}};
    std::uint32_t bookRandom_{0x1993u};

    bool hasJob_{false};
    bool quitting_{false};
    bool cancelled_{false};
    bool forced_{false};
    bool asHint_{false};
    chess::Position pending_{};
    chess::RequestId jobId_{0};
    chess::MoveCallback done_{};
    std::optional<chess::Move> answer_{};
};

}  // namespace

std::unique_ptr<Engine> makeOriginalEngine(const Config& config) {
    return std::make_unique<OriginalEngine>(config);
}

}  // namespace swchess::engine
