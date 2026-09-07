// Tests for the computer opponent.
// The test takes the CD directory as its first argument.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "engine/engine.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

using swchess::chess::Move;
using swchess::chess::Position;
using swchess::chess::RequestId;

void testRandomEngine() {
    swchess::engine::Config config;
    config.level = swchess::engine::Level::Newcomer;
    auto engine = swchess::engine::makeRandomEngine(config, 7);

    Move answer{};
    bool got = false;
    engine->requestMove(Position::start(), 42, [&](RequestId id, Move m) {
        check(id == 42, "the random engine echoes the request id");
        answer = m;
        got = true;
    });

    engine->poll();
    check(!got, "the random engine holds the answer back at first");

    const auto start = std::chrono::steady_clock::now();
    while (!got && std::chrono::steady_clock::now() - start <
                       std::chrono::seconds(3)) {
        engine->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    check(got, "the random engine answers");
    check(elapsed.count() >= 250, "the random engine waits about 300 ms");

    bool legal = false;
    for (const Move& m : Position::start().legalMoves()) {
        if (m == answer) legal = true;
    }
    check(legal, "the random engine returns a legal move");
}

void testRandomEngineForce() {
    swchess::engine::Config config;
    auto engine = swchess::engine::makeRandomEngine(config, 3);
    bool got = false;
    engine->requestMove(Position::start(), 1, [&](RequestId, Move) { got = true; });
    engine->forceMove();
    engine->poll();
    check(got, "forceMove makes the random engine answer at once");
}

void testRandomEngineCancel() {
    swchess::engine::Config config;
    auto engine = swchess::engine::makeRandomEngine(config, 3);
    bool got = false;
    engine->requestMove(Position::start(), 9, [&](RequestId, Move) { got = true; });
    engine->cancel(9);
    engine->forceMove();
    engine->poll();
    check(!got, "cancel drops the open request");
}

void testLevelFileNames() {
    using swchess::engine::Level;
    check(std::string(swchess::engine::levelFileName(Level::Newcomer)) ==
              "NEWCOMER.CMP",
          "Newcomer names NEWCOMER.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Novice)) ==
              "NOVICE.CMP",
          "Novice names NOVICE.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Moderate)) ==
              "MODERATE.CMP",
          "Moderate names MODERATE.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Hard)) == "HARD.CMP",
          "Hard names HARD.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Expert)) ==
              "EXPERT.CMP",
          "Expert names EXPERT.CMP");
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    testLevelFileNames();
    testRandomEngine();
    testRandomEngineForce();
    testRandomEngineCancel();
    if (failures == 0) std::printf("engine_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
