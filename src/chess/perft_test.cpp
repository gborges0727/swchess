// Tests for the chess rules module. The file runs without a test framework so
// the module keeps its standard library only dependency.
#include "chess.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace swchess::chess;

namespace {

int gFailures = 0;

void report(bool ok, const std::string& what, const std::string& detail) {
    if (ok) {
        std::printf("ok    %s\n", what.c_str());
        return;
    }
    ++gFailures;
    std::printf("FAIL  %s\n      %s\n", what.c_str(), detail.c_str());
}

void check(bool condition, const std::string& what) {
    report(condition, what, "condition was false");
}

template <typename T>
void checkEqual(const T& got, const T& want, const std::string& what) {
    std::string detail;
    if constexpr (std::is_same_v<T, std::string>) {
        detail = "got \"" + got + "\", wanted \"" + want + "\"";
    } else {
        detail = "got " + std::to_string(got) + ", wanted " + std::to_string(want);
    }
    report(got == want, what, detail);
}

Position mustParse(const std::string& fen) {
    auto p = Position::fromFen(fen);
    if (!p) {
        ++gFailures;
        std::printf("FAIL  the FEN %s did not parse\n", fen.c_str());
        return Position::start();
    }
    return *p;
}

void perftCase(const std::string& name, const std::string& fen, int depth,
               std::uint64_t want) {
    Position p = mustParse(fen);
    auto begin = std::chrono::steady_clock::now();
    std::uint64_t got = p.perft(depth);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::string what = name + " perft(" + std::to_string(depth) + ") = " +
                       std::to_string(want) + " in " + std::to_string(seconds) + " s";
    report(got == want, what, "got " + std::to_string(got));
}

void testPerft() {
    const std::string start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    perftCase("start", start, 1, 20);
    perftCase("start", start, 2, 400);
    perftCase("start", start, 3, 8902);
    perftCase("start", start, 4, 197281);
    perftCase("start", start, 5, 4865609);

    const std::string kiwipete =
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    perftCase("kiwipete", kiwipete, 1, 48);
    perftCase("kiwipete", kiwipete, 2, 2039);
    perftCase("kiwipete", kiwipete, 3, 97862);
    perftCase("kiwipete", kiwipete, 4, 4085603);

    const std::string third = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    perftCase("position 3", third, 5, 674624);
}

void testFenRoundTrip() {
    const std::vector<std::string> fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        "8/8/8/4k3/8/8/4K3/8 b - - 17 42",
    };
    for (const std::string& fen : fens) {
        checkEqual(mustParse(fen).fen(), fen, "the FEN " + fen + " survives a round trip");
    }
    check(!Position::fromFen("this is not a position").has_value(),
          "nonsense text does not parse as a FEN");
    check(!Position::fromFen("8/8/8/8/8/8/8/8 w - - 0 1").has_value(),
          "a board with no kings does not parse as a FEN");
}

void testCastling() {
    Position p = mustParse("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    checkEqual(static_cast<int>(p.castlingRights()), 15, "the four castling rights are set");

    // White castles short, so White loses both of its rights.
    auto shortCastle = p.parseSan("O-O");
    check(shortCastle.has_value(), "White can castle short");
    Position after = p.apply(*shortCastle);
    checkEqual(after.fen(), std::string("r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1"),
               "castling short moves the king and the rook");

    // Moving the h1 rook costs only the short right.
    Position rookMoved = p.apply(*p.parseLongAlgebraic("h1g1"));
    checkEqual(static_cast<int>(rookMoved.castlingRights()),
               static_cast<int>(WhiteQueenSide | BlackKingSide | BlackQueenSide),
               "moving the h1 rook clears only the White short right");

    // Moving the king costs both White rights.
    Position kingMoved = p.apply(*p.parseLongAlgebraic("e1e2"));
    checkEqual(static_cast<int>(kingMoved.castlingRights()),
               static_cast<int>(BlackKingSide | BlackQueenSide),
               "moving the king clears both White rights");

    // Capturing a rook on its home square costs the matching right.
    Position q = mustParse("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    Position captured = q.apply(*q.parseLongAlgebraic("a1a8"));
    // The rook leaves a1 and lands on a8, so both long rights fall away.
    checkEqual(static_cast<int>(captured.castlingRights()),
               static_cast<int>(WhiteKingSide | BlackKingSide),
               "capturing the a8 rook clears the Black long right");

    // The king may not castle out of, through, or into check.
    // The bishop on a6 attacks f1, which the king would cross when castling
    // short. It leaves d1 and c1 alone, so the long castle stands.
    Position attacked = mustParse("4k3/8/b7/8/8/8/8/R3K2R w KQ - 0 1");
    check(!attacked.parseSan("O-O").has_value(), "the king may not castle through an attacked square");
    check(attacked.parseSan("O-O-O").has_value(), "the king may still castle long");
}

void testEnPassant() {
    Position p = mustParse("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
    check(p.parseSan("exf6").has_value(), "the pawn may take en passant right away");

    // A quiet move in between removes the chance for good.
    Position later = p.apply(*p.parseLongAlgebraic("a2a3"));
    later = later.apply(*later.parseLongAlgebraic("a7a6"));
    check(!later.enPassantSquare().has_value(), "the en passant square clears after another move");
    check(!later.parseLongAlgebraic("e5f6").has_value(),
          "the pawn may not take en passant one move later");

    // The capture removes the pawn that stands beside the capturing pawn.
    Position taken = p.apply(*p.parseSan("exf6"));
    checkEqual(taken.fen(), std::string("rnbqkbnr/ppp1p1pp/5P2/3p4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3"),
               "the en passant capture removes the pawn on f5");

    // A double push that lands beside an enemy pawn sets the square.
    Position pushed = mustParse("4k3/8/8/8/5p2/8/4P3/4K3 w - - 0 1");
    Position afterPush = pushed.apply(*pushed.parseLongAlgebraic("e2e4"));
    checkEqual(squareName(*afterPush.enPassantSquare()), std::string("e3"),
               "a double push sets the en passant square");
    check(afterPush.parseSan("fxe3").has_value(), "Black answers the double push en passant");
}

void testPromotion() {
    Position p = mustParse("8/4P3/8/8/8/8/6k1/4K3 w - - 0 1");
    int promotions = 0;
    for (const Move& m : p.legalMoves()) {
        if (m.promotion) ++promotions;
    }
    checkEqual(promotions, 4, "the pawn promotes to four different pieces");

    const std::vector<std::pair<std::string, std::string>> wanted = {
        {"e8=Q", "4Q3/8/8/8/8/8/6k1/4K3 b - - 0 1"},
        {"e8=R", "4R3/8/8/8/8/8/6k1/4K3 b - - 0 1"},
        {"e8=B", "4B3/8/8/8/8/8/6k1/4K3 b - - 0 1"},
        {"e8=N", "4N3/8/8/8/8/8/6k1/4K3 b - - 0 1"},
    };
    for (const auto& [san, fen] : wanted) {
        auto m = p.parseSan(san);
        check(m.has_value(), "the module reads the move " + san);
        if (m) checkEqual(p.apply(*m).fen(), fen, "the move " + san + " places the new piece");
    }

    // Promotion also works while capturing.
    Position cap = mustParse("5r2/4P3/8/8/8/8/6k1/4K3 w - - 0 1");
    auto capture = cap.parseSan("exf8=N");
    check(capture.has_value(), "the pawn captures and promotes in one move");
}

void testCheckmateAndStalemate() {
    Position fools = mustParse("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    check(fools.inCheck(), "White stands in check after the fool's mate");
    check(fools.legalMoves().empty(), "White has no legal move after the fool's mate");
    check(fools.result() == GameResult::Checkmate, "the fool's mate ends in checkmate");

    Position stale = mustParse("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    check(!stale.inCheck(), "the stalemated king does not stand in check");
    check(stale.legalMoves().empty(), "the stalemated side has no legal move");
    check(stale.result() == GameResult::Stalemate, "the position is a stalemate");

    Position bare = mustParse("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    check(bare.result() == GameResult::DrawInsufficientMaterial,
          "two bare kings cannot mate");
    Position knight = mustParse("4k3/8/8/8/8/8/8/3NK3 w - - 0 1");
    check(knight.result() == GameResult::DrawInsufficientMaterial,
          "a lone knight cannot mate");
    Position rook = mustParse("4k3/8/8/8/8/8/8/3RK3 w - - 0 1");
    check(rook.result() == GameResult::Ongoing, "a rook still allows a mate");

    Position fifty = mustParse("4k3/8/4r3/8/8/4R3/8/4K3 w - - 100 80");
    check(fifty.result() == GameResult::DrawFiftyMove, "a halfmove clock of 100 draws");
}

void testGameAndRepetition() {
    Game game;
    check(game.playSan("e4"), "the game plays e4");
    check(game.playSan("e5"), "the game plays e5");
    check(game.playSan("Nf3"), "the game plays Nf3");
    checkEqual(game.pgnMoves(), std::string("1. e4 e5 2. Nf3"), "the move text reads as PGN");
    check(!game.playSan("Nf3"), "the game rejects an illegal move");

    game.undo();
    checkEqual(game.pgnMoves(), std::string("1. e4 e5"), "undo removes the last move");

    // The knights walk out and back twice, which repeats the position a third
    // time on the last move.
    Game shuffle;
    const std::vector<std::string> moves = {"Nf3", "Nf6", "Ng1", "Ng8",
                                            "Nf3", "Nf6", "Ng1", "Ng8"};
    for (const std::string& san : moves) {
        check(shuffle.playSan(san), "the shuffle plays " + san);
    }
    checkEqual(shuffle.repetitionCount(), 3, "the start position appears three times");
    check(shuffle.result() == GameResult::DrawThreefoldRepetition,
          "the shuffle draws by threefold repetition");

    shuffle.undo();
    check(shuffle.result() == GameResult::Ongoing,
          "undoing the last move takes the draw away again");
}

void testMoveText() {
    Position p = mustParse("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    auto castle = p.parseSan("O-O-O");
    check(castle.has_value(), "the module reads O-O-O");
    if (castle) checkEqual(p.san(*castle), std::string("O-O-O"), "the module writes O-O-O");

    // Two knights reach the same square, so the move text names the file.
    Position knights = mustParse("4k3/8/8/8/8/3N1N2/8/4K3 w - - 0 1");
    auto m = knights.parseLongAlgebraic("d3e5");
    check(m.has_value(), "the knight moves to e5");
    if (m) checkEqual(knights.san(*m), std::string("Nde5"), "the move text separates the two knights");

    Position mate = mustParse("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
    auto mating = mate.parseLongAlgebraic("f7g7");
    check(mating.has_value(), "the queen moves to g7");
    if (mating) checkEqual(mate.san(*mating), std::string("Qg7#"), "the mating move ends in a hash");
}

void testHumanMoveProvider() {
    HumanMoveProvider provider;
    Position p = Position::start();
    Move answer;
    bool called = false;
    RequestId seen = 0;
    provider.requestMove(p, 7, [&](RequestId id, Move m) {
        called = true;
        seen = id;
        answer = m;
    });
    check(provider.hasPending(), "the provider holds the open request");

    Move illegal;
    illegal.from = Square{4, 0};
    illegal.to = Square{4, 4};
    check(!provider.submit(illegal), "the provider rejects an illegal move");

    check(provider.submit(*p.parseSan("e4")), "the provider accepts e4");
    check(called && seen == 7, "the callback ran with the request id");
    checkEqual(p.longAlgebraic(answer), std::string("e2e4"), "the callback carried the move");
    check(!provider.hasPending(), "the request closes after the answer");

    provider.requestMove(p, 8, [&](RequestId, Move) { called = false; });
    provider.cancel(8);
    check(!provider.hasPending(), "cancel closes the open request");
    check(!provider.submit(*p.parseSan("e4")), "a cancelled request takes no answer");
}

}  // namespace

int main() {
    testFenRoundTrip();
    testCastling();
    testEnPassant();
    testPromotion();
    testCheckmateAndStalemate();
    testGameAndRepetition();
    testMoveText();
    testHumanMoveProvider();
    testPerft();

    if (gFailures == 0) {
        std::printf("\nevery check passed\n");
        return 0;
    }
    std::printf("\n%d checks failed\n", gFailures);
    return 1;
}
