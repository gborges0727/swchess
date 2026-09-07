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

// The settings EXPERT.CMP works out to. The search tests use them because
// that level throws no moves away, so its search repeats exactly.
swchess::engine::original::Weights expertWeights() {
    return swchess::engine::original::weightsFor(
        Personality::load(cdFile("EXPERT.CMP")));
}

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
    // What each level's fields work out to once converted.
    const int wantSkip[] = {60, 40, 30, 0, 0};
    const int wantBookPlies[] = {6, 12, 24, 70, 70};
    int which = 0;
    for (const Expected& want : expected) {
        check(std::string(swchess::engine::levelFileName(want.level)) == want.fileName,
              std::string("the level names ") + want.fileName);
        const Personality got = Personality::load(cdFile(want.fileName));
        const std::string where = std::string(want.fileName) + " ";
        check(got.title == want.name, where + "holds the title " + want.name);
        check(got.name == want.name, where + "repeats the name at 0x44");
        check(got.magic == 0x201a, where + "carries the 0x201A magic");
        check(got.contempt == 2, where + "scores a draw at nothing");
        check(got.bookMoves == want.searchDepth, where + "sets the book depth");
        check(got.accuracy == want.secondsPerMove, where + "sets the search accuracy");
        check(got.pieceVersusPawn == want.bookBreadth, where + "sets piece against pawn");
        check(got.materialWeight == want.aggression, where + "sets the material weight");
        check(got.playerType == 2, where + "says the computer plays it");
        check(got.ponder == want.flagB, where + "sets the ponder flag");
        check(got.own.centerPawn == 10, where + "holds 10 for the centre pawn");
        check(got.skipPercent() == wantSkip[which], where + "drops the right share of moves");
        check(got.bookPlies() == wantBookPlies[which], where + "allows the right book depth");
        ++which;
        // Only HARD.CMP holds two different piece value blocks.
        const bool same = got.own == got.opponent;
        check(want.level == Level::Hard ? !same : same,
              where + "matches its two piece value blocks");
    }

    // Every level thinks on the same clock, and it comes from CMWIN.DAT.
    const swchess::engine::original::TimeControl control =
        swchess::engine::original::TimeControl::load(cdFile("CMWIN.DAT"));
    check(control.mode == 501, "CMWIN.DAT asks for a fixed number of seconds a move");
    check(control.secondsPerMove == 5, "CMWIN.DAT gives the engine five seconds a move");
    check(control.fixedDepth == 4, "CMWIN.DAT holds four plies for its other mode");

    // Kamikaze is the level that values the other side's pieces above its own,
    // which is how it comes to give material away.
    const Personality kamikaze = Personality::load(cdFile("HARD.CMP"));
    const auto hard = swchess::engine::original::weightsFor(kamikaze);
    const int q = static_cast<int>(swchess::chess::PieceType::Queen);
    const int pawn = static_cast<int>(swchess::chess::PieceType::Pawn);
    check(hard.opponentPiece[q] > hard.enginePiece[q],
          "Kamikaze rates the enemy queen above its own");
    check(hard.opponentPiece[pawn] > hard.enginePiece[pawn],
          "Kamikaze rates the enemy pawn above its own");

    // Expert runs the engine's own factory values untouched.
    const Personality master = Personality::load(cdFile("EXPERT.CMP"));
    const auto expert = swchess::engine::original::weightsFor(master);
    check(expert.enginePiece == expert.opponentPiece,
          "Chessmaster rates both sides the same");
    check(expert.enginePiece[pawn] == 256, "Chessmaster keeps the factory pawn at 256");
    check(expert.enginePiece[q] == 2368, "Chessmaster keeps the factory queen at 2368");
    check(expert.skipPercent == 0, "Chessmaster searches every move");
    check(expert.drawScore == 0, "Chessmaster scores a draw at nothing");

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
    swchess::engine::original::Searcher searcher(expertWeights());
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

void testSearchDepthChangesThePlay() {
    // The depth cap from the .CMP file is what separates the levels, so a
    // deeper cap has to reach a deeper iteration and change the move.
    const Position position = *Position::fromFen(
        "5rk1/1ppb3p/p1pb4/6q1/3P1p1r/2P1R2P/PP1BQ1P1/5RKN w - - 0 1");
    std::atomic<bool> stop{false};

    swchess::engine::original::Searcher shallow(expertWeights());
    const auto atOne =
        shallow.run(position, {1, std::chrono::milliseconds(20000)}, stop);
    check(atOne.depth == 1, "a cap of one ply reaches one ply");
    check(position.san(atOne.move) == "Qc4+", "one ply grabs the check");

    swchess::engine::original::Searcher deeper(expertWeights());
    const auto atThree =
        deeper.run(position, {3, std::chrono::milliseconds(20000)}, stop);
    check(atThree.depth == 3, "a cap of three plies reaches three plies");
    check(position.san(atThree.move) == "Rg3", "three plies find Rg3");
    check(atThree.nodes > atOne.nodes, "the deeper search visits more nodes");
}

void testSearchStops() {
    const Position position = Position::start();
    swchess::engine::original::Searcher searcher(expertWeights());
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
    // No .CMP file carries a clock. Every level thinks for the five seconds
    // CMWIN.DAT gives it. From a position the book never reaches, the answer
    // must land inside that.
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
    const long took = waitForAnswer(*engine, got, 12000);
    check(took >= 0, "the engine answers off the book");
    check(took >= 4000, "the engine uses the five seconds CMWIN.DAT gives it");
    check(took < 8000, "the engine answers inside the CMWIN.DAT budget");
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
        // Every level thinks for five seconds, so the test presses FORCE
        // rather than sit through twenty of them.
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        engine->forceMove();
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

    // In book the hint takes the most played line, so it repeats.
    const Book book = Book::load(cdFile("BOOK.DAT"));
    const Position start = Position::start();
    Move first{};
    bool same = true;
    for (int i = 0; i < 3; ++i) {
        Move hint{};
        bool answered = false;
        engine->requestHint(start, static_cast<RequestId>(200 + i),
                            [&](RequestId, Move m) {
                                hint = m;
                                answered = true;
                            });
        if (waitForAnswer(*engine, answered, 4000) < 0) {
            check(false, "the hint from the start position comes back");
            return;
        }
        if (i == 0) {
            first = hint;
        } else if (!(hint == first)) {
            same = false;
        }
    }
    check(same, "the hint repeats itself in the book");
    check(first == book.probe(start).front(), "the hint names the most played book move");
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
        testSearchDepthChangesThePlay();
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
