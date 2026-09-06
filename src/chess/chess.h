// Chess rules for the Star Wars Chess native port.
// The module owns legal moves, turn order, castling, en passant, promotion,
// checkmate and draws. It depends on the standard library only.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace swchess::chess {

enum class Color : std::uint8_t { White, Black };

constexpr Color opposite(Color c) {
    return c == Color::White ? Color::Black : Color::White;
}

enum class PieceType : std::uint8_t { King, Queen, Rook, Bishop, Knight, Pawn };

struct Piece {
    Color color{Color::White};
    PieceType type{PieceType::Pawn};

    friend bool operator==(const Piece&, const Piece&) = default;
};

// A board square. Both coordinates run 0 to 7.
// File 0 is the a file and rank 0 is White's first rank.
struct Square {
    int file{0};
    int rank{0};

    constexpr Square() = default;
    constexpr Square(int f, int r) : file(f), rank(r) {}

    constexpr bool valid() const {
        return file >= 0 && file < 8 && rank >= 0 && rank < 8;
    }

    friend bool operator==(const Square&, const Square&) = default;
};

// Castling rights are four independent flags.
enum CastlingRight : std::uint8_t {
    WhiteKingSide = 1,
    WhiteQueenSide = 2,
    BlackKingSide = 4,
    BlackQueenSide = 8,
};

struct Move {
    Square from{};
    Square to{};
    std::optional<PieceType> promotion{};
    bool capture{false};
    bool castling{false};
    bool enPassant{false};
    bool doublePawnPush{false};

    friend bool operator==(const Move& a, const Move& b) {
        return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
    }
};

enum class GameResult : std::uint8_t {
    Ongoing,
    Checkmate,
    Stalemate,
    DrawFiftyMove,
    DrawInsufficientMaterial,
    DrawThreefoldRepetition,
};

bool isDraw(GameResult r);
std::string_view resultName(GameResult r);

// Writes a square as "e4". Returns "-" when the square is off the board.
std::string squareName(Square s);
std::optional<Square> parseSquare(std::string_view text);

char pieceLetter(Piece p);
std::optional<PieceType> pieceTypeFromLetter(char c);

// An immutable chess position. Every operation returns a new value.
class Position {
public:
    Position();

    static Position start();
    static std::optional<Position> fromFen(std::string_view fen);
    std::string fen() const;

    Color sideToMove() const { return sideToMove_; }
    std::uint8_t castlingRights() const { return castling_; }
    std::optional<Square> enPassantSquare() const;
    int halfmoveClock() const { return halfmove_; }
    int fullmoveNumber() const { return fullmove_; }

    std::optional<Piece> at(Square s) const;

    std::vector<Move> legalMoves() const;
    // The caller must pass a move that legalMoves() returned for this position.
    Position apply(Move m) const;

    bool inCheck() const;
    GameResult result() const;

    // Identifies the position for repetition counting.
    // It covers placement, side to move, castling rights and the en passant
    // square, and it ignores both move counters.
    std::string repetitionKey() const;

    friend bool operator==(const Position& a, const Position& b) {
        return a.board_ == b.board_ && a.sideToMove_ == b.sideToMove_ &&
               a.castling_ == b.castling_ && a.epSquare_ == b.epSquare_;
    }

    // Move text. San() and longAlgebraic() need a move that is legal here.
    std::string san(Move m) const;
    std::optional<Move> parseSan(std::string_view text) const;
    std::string longAlgebraic(Move m) const;
    std::optional<Move> parseLongAlgebraic(std::string_view text) const;

    // Counts the leaf nodes of the move tree at the given depth.
    std::uint64_t perft(int depth) const;

private:
    // Builds a position with an empty board. The public default constructor
    // sets up the start position, so it cannot serve as the blank starting
    // point that fromFen() fills in.
    struct Blank {};
    explicit Position(Blank) {}

    // The board uses the 0x88 layout, so index = rank * 16 + file.
    // A square is on the board when (index & 0x88) == 0.
    std::array<std::uint8_t, 128> board_{};
    Color sideToMove_{Color::White};
    std::uint8_t castling_{0};
    int epSquare_{-1};
    int halfmove_{0};
    int fullmove_{1};
    // Where each king stands, indexed by color. Scanning the board for the
    // king on every legality test would dominate the move generator.
    std::array<int, 2> kingSq_{-1, -1};

    void addPseudoLegal(std::vector<Move>& out) const;
    bool attacked(int sq, Color by) const;
    int kingSquare(Color c) const { return kingSq_[static_cast<int>(c)]; }
    int findKing(Color c) const;
    bool insufficientMaterial() const;
    Move complete(Move m) const;
};

// A played game. It keeps the start position, the move list and the position
// after every move, so it can report threefold repetition.
class Game {
public:
    Game();
    explicit Game(const Position& start);

    const Position& startPosition() const { return start_; }
    const Position& position() const { return positions_.back(); }
    const std::vector<Move>& moves() const { return moves_; }

    // Plays the move when it is legal. Returns false and changes nothing
    // otherwise.
    bool play(Move m);
    bool playSan(std::string_view text);
    void undo();
    void reset();

    GameResult result() const;
    int repetitionCount() const;

    // Writes the moves in standard algebraic notation with move numbers.
    std::string pgnMoves() const;

private:
    Position start_;
    std::vector<Position> positions_;
    std::vector<Move> moves_;
};

using RequestId = std::uint64_t;
using MoveCallback = std::function<void(RequestId, Move)>;

// Asks some source for a move in a position. The request carries an id so a
// late answer can be matched or thrown away.
struct MoveProvider {
    virtual ~MoveProvider() = default;
    virtual void requestMove(const Position& position, RequestId id, MoveCallback done) = 0;
    virtual void cancel(RequestId id) = 0;
};

// Holds one open request until the user picks a move.
class HumanMoveProvider final : public MoveProvider {
public:
    void requestMove(const Position& position, RequestId id, MoveCallback done) override;
    void cancel(RequestId id) override;

    bool hasPending() const { return pending_; }
    RequestId pendingId() const { return id_; }
    const Position& pendingPosition() const { return position_; }

    // Answers the open request. Returns false when no request is open or the
    // move is not legal in the requested position.
    bool submit(Move m);

private:
    bool pending_{false};
    RequestId id_{0};
    Position position_{};
    MoveCallback done_{};
};

}  // namespace swchess::chess
