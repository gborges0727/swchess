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

std::string join(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/') return dir + file;
    return dir + "/" + file;
}

// Turns the settings in a .CMP file into the depth and the clock the search
// runs against. secondsPerMove is zero on NEWCOMER.CMP, which is the level
// the original runs in its own shallow "newcomer mode" instead of on a clock,
// so that level gets a short fixed budget here.
SearchLimits limitsFor(const Personality& personality) {
    SearchLimits limits;
    limits.maxDepth = personality.searchDepth > 0 ? personality.searchDepth
                                                  : original::kPlyMax - 2;
    const int seconds = personality.secondsPerMove;
    limits.maxTime = std::chrono::milliseconds(seconds > 0 ? seconds * 1000 : 1000);
    return limits;
}

class OriginalEngine final : public Engine {
public:
    explicit OriginalEngine(const Config& config)
        : cdDir_(config.cdDir),
          level_(config.level),
          book_(Book::load(join(config.cdDir, "BOOK.DAT"))),
          personality_(Personality::load(join(config.cdDir, levelFileName(config.level)))) {
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
        start(position, id, std::move(done), true);
    }

    void requestHint(const chess::Position& position, chess::RequestId id,
                     chess::MoveCallback done) override {
        start(position, id, std::move(done), false);
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
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
        personality_ = std::move(loaded);
    }

    Level level() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    void forceMove() override { stop_.store(true, std::memory_order_relaxed); }

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
               bool useBook) {
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
            useBook_ = useBook;
            hasJob_ = true;
        }
        wake_.notify_one();
    }

    void workerLoop() {
        while (true) {
            chess::Position position;
            Personality personality;
            bool useBook = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return hasJob_ || quitting_; });
                if (quitting_) return;
                hasJob_ = false;
                position = pending_;
                personality = personality_;
                useBook = useBook_;
                stop_.store(false, std::memory_order_relaxed);
            }

            std::optional<chess::Move> chosen;
            if (useBook) chosen = book_.pick(position, bookRandom_);
            if (!chosen) {
                Searcher searcher(personality.primary);
                const SearchResult found = searcher.run(position, limitsFor(personality), stop_);
                if (found.hasMove) chosen = found.move;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            // A cancel or a newer request while the search ran throws this
            // answer away.
            if (cancelled_ || hasJob_ || !done_) continue;
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
    Personality personality_;
    std::uint32_t bookRandom_{0x1993u};

    bool hasJob_{false};
    bool quitting_{false};
    bool cancelled_{false};
    bool useBook_{true};
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
