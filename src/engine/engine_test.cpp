// Tests for the computer opponent.
// The test takes the CD directory as its first argument. It skips the tests
// that read original files when that directory is missing.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "engine/engine.h"
#include "engine/original/board.h"
#include "engine/original/book.h"
#include "engine/original/cmp.h"
#include "engine/original/eval.h"
#include "engine/original/search.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

using swchess::chess::Move;
using swchess::chess::Position;
using swchess::chess::RequestId;
using swchess::engine::Level;
using swchess::engine::original::Book;
using swchess::engine::original::Personality;

std::string cdDir;

std::string cdFile(const std::string& name) { return cdDir + "/" + name; }

bool isLegal(const Position& position, Move move) {
    for (const Move& m : position.legalMoves()) {
        if (m == move) return true;
    }
    return false;
}

// Drives poll() until the engine answers or the budget runs out. Returns the
// milliseconds it took, or -1 when nothing came back.
long waitForAnswer(swchess::engine::Engine& engine, const bool& got, int budgetMs) {
    const auto start = std::chrono::steady_clock::now();
    while (!got) {
        engine.poll();
        if (got) break;
        const auto waited = std::chrono::steady_clock::now() - start;
        if (waited > std::chrono::milliseconds(budgetMs)) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

// ---------------------------------------------------------------- stand-in

void testLevelFileNames() {
    check(std::string(swchess::engine::levelFileName(Level::Newcomer)) == "NEWCOMER.CMP",
          "Newcomer names NEWCOMER.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Novice)) == "NOVICE.CMP",
          "Novice names NOVICE.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Moderate)) == "MODERATE.CMP",
          "Moderate names MODERATE.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Hard)) == "HARD.CMP",
          "Hard names HARD.CMP");
    check(std::string(swchess::engine::levelFileName(Level::Expert)) == "EXPERT.CMP",
          "Expert names EXPERT.CMP");
}

void testRandomEngine() {
    swchess::engine::Config config;
    config.level = Level::Newcomer;
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

    const long took = waitForAnswer(*engine, got, 3000);
    check(took >= 250, "the random engine waits about 300 ms");
    check(isLegal(Position::start(), answer), "the random engine returns a legal move");
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

// ------------------------------------------------------------ square codes

void testSquareCodes() {
    using swchess::engine::original::decodeSquare;
    using swchess::engine::original::encodeSquare;
    // BOOK.DAT writes c2 as 0x62 and c4 as 0x42.
    check(decodeSquare(0x62) == swchess::chess::Square(2, 1), "0x62 decodes to c2");
    check(decodeSquare(0x42) == swchess::chess::Square(2, 3), "0x42 decodes to c4");
    check(decodeSquare(0x00) == swchess::chess::Square(0, 7), "0x00 decodes to a8");
    check(decodeSquare(0x77) == swchess::chess::Square(7, 0), "0x77 decodes to h1");
    check(!decodeSquare(0x88).has_value(), "0x88 is off the board");
    for (int code = 0; code < 0x78; ++code) {
        const auto square = decodeSquare(static_cast<std::uint8_t>(code));
        if (!square) continue;
        if (encodeSquare(*square) != code) {
            check(false, "encodeSquare undoes decodeSquare");
            return;
        }
    }
    check(true, "encodeSquare undoes decodeSquare");
}

// -------------------------------------------------------- the level files

void testPersonalities() {
    // Every shipped level, its file name, the name inside it, and the four
    // settings that differ between levels.
    struct Expected {
        Level level;
        const char* fileName;
        const char* name;
        int searchDepth;
        int secondsPerMove;
        int bookBreadth;
        int aggression;
        int flagB;
    };
    const Expected expected[] = {
        {Level::Newcomer, "NEWCOMER.CMP", "Newcomer", 3, 0, 58, 0, 0},
        {Level::Novice, "NOVICE.CMP", "Novice", 6, 20, 98, 0, 0},
        {Level::Moderate, "MODERATE.CMP", "Woodpusher", 12, 30, 98, 31, 0},
        {Level::Hard, "HARD.CMP", "Kamikaze", 35, 60, 0, 100, 1},
        {Level::Expert, "EXPERT.CMP", "Chessmaster", 35, 60, 100, 100, 1},
    };
    for (const Expected& want : expected) {
        check(std::string(swchess::engine::levelFileName(want.level)) == want.fileName,
              std::string("the level names ") + want.fileName);
        const Personality got = Personality::load(cdFile(want.fileName));
        const std::string where = std::string(want.fileName) + " ";
        check(got.name == want.name, where + "holds the name " + want.name);
        check(got.echoName == want.name, where + "repeats the name at 0x44");
        check(got.signature == 0x201a, where + "carries the 0x201A signature");
        check(got.contempt == 2, where + "holds 2 at offset 0x3A");
        check(got.searchDepth == want.searchDepth, where + "sets the search depth");
        check(got.secondsPerMove == want.secondsPerMove, where + "sets the seconds per move");
        check(got.bookBreadth == want.bookBreadth, where + "sets the book breadth");
        check(got.aggression == want.aggression, where + "sets the aggression");
        check(got.flagA == 2, where + "holds 2 at offset 0x64");
        check(got.flagB == want.flagB, where + "sets the flag at offset 0x65");
        check(got.primary.randomness == 10, where + "sets the randomness weight to 10");
        // Only HARD.CMP holds two different weight blocks.
        const bool same = got.primary == got.secondary;
        check(want.level == Level::Hard ? !same : same,
              where + "matches its two weight blocks");
    }

    bool threw = false;
    try {
        Personality::load(cdFile("NOSUCHLEVEL.CMP"));
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a missing level file throws");

    threw = false;
    try {
        Personality::load(cdFile("BOOK.DAT"));
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a file that is not 102 bytes throws");
}

// ------------------------------------------------------------- the book

void testBook() {
    const Book book = Book::load(cdFile("BOOK.DAT"));
    check(book.banner().find("Chessmaster 3000 Opening Book") != std::string::npos,
          "the book banner names the Chessmaster 3000 book");
    check(book.lines().size() == 169, "the book holds 169 named lines");

    // The list index is the place the front end shows each name, so the 169
    // indexes cover 0 to 168 once each.
    std::vector<bool> seen(book.lines().size(), false);
    for (const auto& line : book.lines()) {
        check(line.listIndex >= 0 && line.listIndex < 169, "a list index stays in range");
        if (line.listIndex >= 0 && line.listIndex < static_cast<int>(seen.size())) {
            check(!seen[line.listIndex], "no two lines share a list index");
            seen[line.listIndex] = true;
        }
    }

    const Book::BookLineList& lines = book.lines();
    bool foundPolish = false;
    bool foundEnglish = false;
    for (const auto& line : lines) {
        if (line.name == "Polish Opening") {
            foundPolish = true;
            check(!line.moves.empty(), "Polish Opening holds moves");
            check(Position::start().san(line.moves.front()) == "b4",
                  "Polish Opening starts with 1.b4");
            check(line.displayName == "Polish Opening",
                  "a plain name displays as it is written");
        }
        if (line.name == "K - Ultra-Symmetrical Variation") {
            foundEnglish = true;
            // "K=English Opening" defines the letter K earlier in the file.
            check(line.displayName == "English Opening - Ultra-Symmetrical Variation",
                  "a shorthand name spells its family out");
        }
        if (line.name == "K=English Opening") {
            check(line.displayName == "English Opening",
                  "the line that defines a letter displays the family name");
        }
    }
    check(foundPolish, "the book holds Polish Opening");
    check(foundEnglish, "the book holds K - Ultra-Symmetrical Variation");

    // Naming the opening is the job BOOK.DAT did for the front end.
    const Position afterC4C5 = Position::start()
                                   .apply(*Position::start().parseSan("c4"));
    check(book.openingName(afterC4C5).find("English Opening") != std::string::npos,
          "1.c4 names the English Opening");
    check(book.openingName(Position::start()).empty(),
          "the start position names no opening");

    // A move from the start position.
    const Position start = Position::start();
    const std::vector<Move> opening = book.probe(start);
    check(!opening.empty(), "the book answers from the start position");
    for (const Move& m : opening) {
        check(isLegal(start, m), "every book move from the start is legal");
    }
    check(start.san(opening.front()) == "e4",
          "the book's most played first move is 1.e4");

    // A move after 1.e4.
    const Move e4 = *start.parseSan("e4");
    const Position afterE4 = start.apply(e4);
    const std::vector<Move> replies = book.probe(afterE4);
    check(!replies.empty(), "the book answers after 1.e4");
    for (const Move& m : replies) {
        check(isLegal(afterE4, m), "every book reply to 1.e4 is legal");
    }

    // pick() must stay inside the candidates it was given.
    std::uint32_t random = 1;
    for (int i = 0; i < 50; ++i) {
        const auto chosen = book.pick(afterE4, random);
        check(chosen.has_value(), "pick answers where probe answers");
        if (chosen) check(isLegal(afterE4, *chosen), "pick returns a legal move");
    }

    // A position the book never reaches.
    const Position odd = *Position::fromFen("8/8/8/3k4/8/3K4/8/7R w - - 0 1");
    check(book.probe(odd).empty(), "the book stays quiet off its lines");
    std::uint32_t unused = 1;
    check(!book.pick(odd, unused).has_value(), "pick stays quiet off the book");
}

// ------------------------------------------------------------ the search

void testSearchFindsMateInOne() {
    // Black's king sits on g8 behind its own pawns, so Ra8 is mate.
    const Position position = *Position::fromFen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    swchess::engine::original::StyleWeights style;
    style.attack = 100;
    style.defense = 100;
    style.material = 25;
    style.mobility = 20;
    swchess::engine::original::Searcher searcher(style);
    swchess::engine::original::SearchLimits limits;
    limits.maxDepth = 3;
    limits.maxTime = std::chrono::milliseconds(5000);
    std::atomic<bool> stop{false};
    const auto found = searcher.run(position, limits, stop);
    check(found.hasMove, "the search returns a move in a mate position");
    check(position.san(found.move) == "Ra8#", "the search finds the mate in one");
    check(swchess::engine::original::isMateScore(found.score),
          "the search scores the mate as a mate");
}

void testSearchStops() {
    const Position position = Position::start();
    swchess::engine::original::StyleWeights style;
    style.material = 25;
    swchess::engine::original::Searcher searcher(style);
    swchess::engine::original::SearchLimits limits;
    limits.maxDepth = 30;
    limits.maxTime = std::chrono::milliseconds(150);
    std::atomic<bool> stop{false};
    const auto start = std::chrono::steady_clock::now();
    const auto found = searcher.run(position, limits, stop);
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    check(found.hasMove, "the search returns a move when the clock runs out");
    check(took.count() < 1500, "the search honours its clock");
    check(isLegal(position, found.move), "the search returns a legal move");
}

// ------------------------------------------------------------ the engine

void testOriginalEngineAnswersInBudget() {
    // NEWCOMER.CMP asks for depth 3 and no clock, so the answer must be quick.
    swchess::engine::Config config;
    config.cdDir = cdDir;
    config.level = Level::Newcomer;
    auto engine = swchess::engine::makeOriginalEngine(config);
    check(engine->level() == Level::Newcomer, "the engine reports its level");

    Move answer{};
    bool got = false;
    engine->requestMove(Position::start(), 5, [&](RequestId id, Move m) {
        check(id == 5, "the engine echoes the request id");
        answer = m;
        got = true;
    });
    const long took = waitForAnswer(*engine, got, 4000);
    check(took >= 0, "the engine answers on the start position");
    check(isLegal(Position::start(), answer), "the engine returns a legal move");

    // The start position is in the book, so the answer must be a book move
    // and must arrive far inside NEWCOMER.CMP's budget.
    const Book book = Book::load(cdFile("BOOK.DAT"));
    const std::vector<Move> fromBook = book.probe(Position::start());
    bool inBook = false;
    for (const Move& m : fromBook) {
        if (m == answer) inBook = true;
    }
    check(inBook, "the engine plays the book on the first move");
    check(took < 2000, "the engine answers inside the level's budget");
}

void testOriginalEngineHonoursTheBudget() {
    // NEWCOMER.CMP asks for depth 3 and holds 0 in its seconds-per-move
    // field, so the port gives that level a one second budget. From a
    // position the book never reaches, the answer must land inside it.
    swchess::engine::Config config;
    config.cdDir = cdDir;
    config.level = Level::Newcomer;
    auto engine = swchess::engine::makeOriginalEngine(config);

    const Position position = *Position::fromFen(
        "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1B3/PPP2PPP/R2Q1RK1 w - - 0 1");
    Move answer{};
    bool got = false;
    engine->requestMove(position, 21, [&](RequestId, Move m) {
        answer = m;
        got = true;
    });
    const long took = waitForAnswer(*engine, got, 5000);
    check(took >= 0, "the engine answers off the book");
    check(took < 2500, "the engine answers inside the Newcomer budget");
    check(isLegal(position, answer), "the off-book answer is legal");
}

void testOriginalEngineEveryMoveIsLegal() {
    // Plays both sides of a middlegame the book never reaches, so every
    // answer comes out of the search, and checks each one against the rules
    // module.
    swchess::engine::Config config;
    config.cdDir = cdDir;
    config.level = Level::Newcomer;
    auto engine = swchess::engine::makeOriginalEngine(config);

    Position position = *Position::fromFen(
        "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1B3/PPP2PPP/R2Q1RK1 b - - 0 1");
    const Book book = Book::load(cdFile("BOOK.DAT"));
    check(book.probe(position).empty(), "the middlegame test position is off the book");
    for (int half = 0; half < 20; ++half) {
        if (position.legalMoves().empty()) break;
        Move answer{};
        bool got = false;
        engine->requestMove(position, static_cast<RequestId>(half),
                            [&](RequestId, Move m) {
                                answer = m;
                                got = true;
                            });
        const long took = waitForAnswer(*engine, got, 8000);
        if (took < 0) {
            check(false, "the engine answers every request in the game");
            return;
        }
        if (!isLegal(position, answer)) {
            check(false, "every move the engine plays is legal");
            return;
        }
        position = position.apply(answer);
    }
    check(true, "every move the engine plays is legal");
}

void testOriginalEngineForceIsPrompt() {
    // EXPERT.CMP asks for a minute a move, so an unforced search would run
    // far past this test's patience.
    swchess::engine::Config config;
    config.cdDir = cdDir;
    config.level = Level::Expert;
    auto engine = swchess::engine::makeOriginalEngine(config);

    // A position off the book, so the engine has to search for it.
    const Position position = *Position::fromFen(
        "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1B3/PPP2PPP/R2Q1RK1 w - - 0 1");
    const Book book = Book::load(cdFile("BOOK.DAT"));
    check(book.probe(position).empty(), "the force test position is off the book");
    Move answer{};
    bool got = false;
    engine->requestMove(position, 77, [&](RequestId, Move m) {
        answer = m;
        got = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto start = std::chrono::steady_clock::now();
    engine->forceMove();
    const long took = waitForAnswer(*engine, got, 3000);
    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    check(took >= 0, "forceMove makes the engine answer");
    check(since.count() < 2000, "forceMove answers promptly");
    check(isLegal(position, answer), "forceMove returns a legal move");
}

void testOriginalEngineForceBeforeThePickup() {
    // A force that lands before the worker has taken the job must still stop
    // the search, not be cleared when the worker starts.
    swchess::engine::Config config;
    config.cdDir = cdDir;
    config.level = Level::Expert;
    auto engine = swchess::engine::makeOriginalEngine(config);

    const Position position = *Position::fromFen(
        "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1B3/PPP2PPP/R2Q1RK1 w - - 0 1");
    Move answer{};
    bool got = false;
    const auto start = std::chrono::steady_clock::now();
    engine->requestMove(position, 78, [&](RequestId, Move m) {
        answer = m;
        got = true;
    });
    engine->forceMove();
    const long took = waitForAnswer(*engine, got, 4000);
    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    check(took >= 0, "an immediate forceMove still makes the engine answer");
    check(since.count() < 2000, "an immediate forceMove answers promptly");
    check(isLegal(position, answer), "an immediate forceMove returns a legal move");
}

void testOriginalEngineNoLegalMove() {
    // Black is stalemated, so there is no move to answer with. The engine
    // must drop the request instead of calling back with nothing.
    swchess::engine::Config config;
    config.cdDir = cdDir;
    auto engine = swchess::engine::makeOriginalEngine(config);
    const Position position = *Position::fromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    check(position.legalMoves().empty(), "the stalemate position has no legal move");
    bool got = false;
    engine->requestMove(position, 99, [&](RequestId, Move) { got = true; });
    const long took = waitForAnswer(*engine, got, 800);
    check(took < 0, "the engine answers nothing when no move is legal");
}

void testOriginalEngineHint() {
    swchess::engine::Config config;
    config.cdDir = cdDir;
    config.level = Level::Newcomer;
    auto engine = swchess::engine::makeOriginalEngine(config);

    const Position position = *Position::fromFen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    Move answer{};
    bool got = false;
    engine->requestHint(position, 11, [&](RequestId id, Move m) {
        check(id == 11, "the hint echoes the request id");
        answer = m;
        got = true;
    });
    const long took = waitForAnswer(*engine, got, 8000);
    check(took >= 0, "the hint comes back");
    check(position.san(answer) == "Ra8#", "the hint names the mate in one");
    // The hint must not change the engine's own idea of the position.
    check(position.sideToMove() == swchess::chess::Color::White,
          "asking for a hint plays nothing");
}

void testOriginalEngineLevels() {
    swchess::engine::Config config;
    config.cdDir = cdDir;
    config.level = Level::Newcomer;
    auto engine = swchess::engine::makeOriginalEngine(config);
    for (const Level level : {Level::Newcomer, Level::Novice, Level::Moderate, Level::Hard,
                              Level::Expert}) {
        engine->setLevel(level);
        check(engine->level() == level, "setLevel takes");
    }

    bool threw = false;
    try {
        swchess::engine::Config bad;
        bad.cdDir = cdDir + "/no-such-directory";
        auto ignored = swchess::engine::makeOriginalEngine(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a missing CD directory throws");
}

}  // namespace

int main(int argc, char** argv) {
    testLevelFileNames();
    testRandomEngine();
    testRandomEngineForce();
    testRandomEngineCancel();
    testSquareCodes();

    if (argc > 1) {
        cdDir = argv[1];
        testPersonalities();
        testBook();
        testSearchFindsMateInOne();
        testSearchStops();
        testOriginalEngineAnswersInBudget();
        testOriginalEngineHonoursTheBudget();
        testOriginalEngineEveryMoveIsLegal();
        testOriginalEngineForceIsPrompt();
        testOriginalEngineForceBeforeThePickup();
        testOriginalEngineNoLegalMove();
        testOriginalEngineHint();
        testOriginalEngineLevels();
    } else {
        std::printf("engine_test: no CD directory given, skipping the file tests\n");
    }

    std::printf("engine_test: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
