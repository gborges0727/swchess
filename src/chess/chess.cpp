#include "chess.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace swchess::chess {
namespace {

// A board byte is 0 for an empty square. Otherwise the low three bits hold the
// piece type plus one and bit three is set for Black.
constexpr std::uint8_t kEmpty = 0;

constexpr std::uint8_t encode(Color c, PieceType t) {
    return static_cast<std::uint8_t>((static_cast<std::uint8_t>(t) + 1) |
                                     (c == Color::Black ? 8u : 0u));
}

constexpr Color colorOf(std::uint8_t code) {
    return (code & 8u) ? Color::Black : Color::White;
}

constexpr PieceType typeOf(std::uint8_t code) {
    return static_cast<PieceType>((code & 7u) - 1u);
}

constexpr bool onBoard(int sq) { return (sq & 0x88) == 0; }
constexpr int indexOf(Square s) { return s.rank * 16 + s.file; }
constexpr Square squareOf(int sq) { return Square{sq & 7, sq >> 4}; }

constexpr int kKnightSteps[8] = {33, 31, 18, 14, -14, -18, -31, -33};
constexpr int kKingSteps[8] = {16, -16, 1, -1, 17, 15, -15, -17};
constexpr int kRookSteps[4] = {16, -16, 1, -1};
constexpr int kBishopSteps[4] = {17, 15, -15, -17};

const PieceType kPromotionChoices[4] = {PieceType::Queen, PieceType::Rook,
                                        PieceType::Bishop, PieceType::Knight};

char typeLetter(PieceType t) {
    switch (t) {
        case PieceType::King: return 'K';
        case PieceType::Queen: return 'Q';
        case PieceType::Rook: return 'R';
        case PieceType::Bishop: return 'B';
        case PieceType::Knight: return 'N';
        case PieceType::Pawn: return 'P';
    }
    return '?';
}

}  // namespace

bool isDraw(GameResult r) {
    return r == GameResult::Stalemate || r == GameResult::DrawFiftyMove ||
           r == GameResult::DrawInsufficientMaterial ||
           r == GameResult::DrawThreefoldRepetition;
}

std::string_view resultName(GameResult r) {
    switch (r) {
        case GameResult::Ongoing: return "ongoing";
        case GameResult::Checkmate: return "checkmate";
        case GameResult::Stalemate: return "stalemate";
        case GameResult::DrawFiftyMove: return "draw by the fifty move rule";
        case GameResult::DrawInsufficientMaterial: return "draw by insufficient material";
        case GameResult::DrawThreefoldRepetition: return "draw by threefold repetition";
    }
    return "unknown";
}

std::string squareName(Square s) {
    if (!s.valid()) return "-";
    std::string out;
    out += static_cast<char>('a' + s.file);
    out += static_cast<char>('1' + s.rank);
    return out;
}

std::optional<Square> parseSquare(std::string_view text) {
    if (text.size() != 2) return std::nullopt;
    int file = text[0] - 'a';
    int rank = text[1] - '1';
    Square s{file, rank};
    if (!s.valid()) return std::nullopt;
    return s;
}

char pieceLetter(Piece p) {
    char c = typeLetter(p.type);
    return p.color == Color::White ? c : static_cast<char>(std::tolower(c));
}

std::optional<PieceType> pieceTypeFromLetter(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
        case 'K': return PieceType::King;
        case 'Q': return PieceType::Queen;
        case 'R': return PieceType::Rook;
        case 'B': return PieceType::Bishop;
        case 'N': return PieceType::Knight;
        case 'P': return PieceType::Pawn;
        default: return std::nullopt;
    }
}

Position::Position() { *this = start(); }

Position Position::start() {
    auto p = fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    return *p;
}

std::optional<Square> Position::enPassantSquare() const {
    if (epSquare_ < 0) return std::nullopt;
    return squareOf(epSquare_);
}

std::optional<Piece> Position::at(Square s) const {
    if (!s.valid()) return std::nullopt;
    std::uint8_t code = board_[indexOf(s)];
    if (code == kEmpty) return std::nullopt;
    return Piece{colorOf(code), typeOf(code)};
}

std::optional<Position> Position::fromFen(std::string_view fen) {
    std::istringstream in{std::string(fen)};
    std::string placement, side, castling, ep;
    if (!(in >> placement >> side >> castling >> ep)) return std::nullopt;
    int halfmove = 0;
    int fullmove = 1;
    if (!(in >> halfmove)) halfmove = 0;
    if (!(in >> fullmove)) fullmove = 1;

    Position p{Blank{}};

    int rank = 7;
    int file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8) return std::nullopt;
            --rank;
            file = 0;
            if (rank < 0) return std::nullopt;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return std::nullopt;
            continue;
        }
        auto type = pieceTypeFromLetter(c);
        if (!type || file > 7 || rank < 0) return std::nullopt;
        Color color = std::isupper(static_cast<unsigned char>(c)) ? Color::White : Color::Black;
        p.board_[rank * 16 + file] = encode(color, *type);
        ++file;
    }
    if (rank != 0 || file != 8) return std::nullopt;

    if (side == "w") {
        p.sideToMove_ = Color::White;
    } else if (side == "b") {
        p.sideToMove_ = Color::Black;
    } else {
        return std::nullopt;
    }

    if (castling != "-") {
        for (char c : castling) {
            switch (c) {
                case 'K': p.castling_ |= WhiteKingSide; break;
                case 'Q': p.castling_ |= WhiteQueenSide; break;
                case 'k': p.castling_ |= BlackKingSide; break;
                case 'q': p.castling_ |= BlackQueenSide; break;
                default: return std::nullopt;
            }
        }
    }

    if (ep != "-") {
        auto s = parseSquare(ep);
        if (!s) return std::nullopt;
        p.epSquare_ = indexOf(*s);
    }

    p.halfmove_ = halfmove;
    p.fullmove_ = fullmove;
    p.kingSq_[0] = p.findKing(Color::White);
    p.kingSq_[1] = p.findKing(Color::Black);

    // Both sides need exactly one king, and the side that just moved must not
    // stand in check.
    if (p.kingSq_[0] < 0 || p.kingSq_[1] < 0) return std::nullopt;
    if (p.attacked(p.kingSquare(opposite(p.sideToMove_)), p.sideToMove_)) return std::nullopt;
    return p;
}

std::string Position::fen() const {
    std::string out;
    for (int rank = 7; rank >= 0; --rank) {
        int run = 0;
        for (int file = 0; file < 8; ++file) {
            std::uint8_t code = board_[rank * 16 + file];
            if (code == kEmpty) {
                ++run;
                continue;
            }
            if (run > 0) {
                out += static_cast<char>('0' + run);
                run = 0;
            }
            out += pieceLetter(Piece{colorOf(code), typeOf(code)});
        }
        if (run > 0) out += static_cast<char>('0' + run);
        if (rank > 0) out += '/';
    }
    out += sideToMove_ == Color::White ? " w " : " b ";
    if (castling_ == 0) {
        out += '-';
    } else {
        if (castling_ & WhiteKingSide) out += 'K';
        if (castling_ & WhiteQueenSide) out += 'Q';
        if (castling_ & BlackKingSide) out += 'k';
        if (castling_ & BlackQueenSide) out += 'q';
    }
    out += ' ';
    out += epSquare_ >= 0 ? squareName(squareOf(epSquare_)) : "-";
    out += ' ';
    out += std::to_string(halfmove_);
    out += ' ';
    out += std::to_string(fullmove_);
    return out;
}

std::string Position::repetitionKey() const {
    std::string full = fen();
    // Drop the halfmove clock and the move number, which do not affect
    // whether two positions repeat.
    std::size_t last = full.rfind(' ');
    std::size_t prev = full.rfind(' ', last - 1);
    return full.substr(0, prev);
}

int Position::findKing(Color c) const {
    std::uint8_t want = encode(c, PieceType::King);
    for (int sq = 0; sq < 128; ++sq) {
        if (!onBoard(sq)) continue;
        if (board_[sq] == want) return sq;
    }
    return -1;
}

bool Position::attacked(int sq, Color by) const {
    if (sq < 0) return false;

    // Pawns. A White pawn on p attacks p + 15 and p + 17.
    std::uint8_t pawn = encode(by, PieceType::Pawn);
    int back = by == Color::White ? -16 : 16;
    for (int side : {-1, 1}) {
        int from = sq + back + side;
        if (onBoard(from) && board_[from] == pawn) return true;
    }

    std::uint8_t knight = encode(by, PieceType::Knight);
    for (int step : kKnightSteps) {
        int from = sq + step;
        if (onBoard(from) && board_[from] == knight) return true;
    }

    std::uint8_t king = encode(by, PieceType::King);
    for (int step : kKingSteps) {
        int from = sq + step;
        if (onBoard(from) && board_[from] == king) return true;
    }

    std::uint8_t rook = encode(by, PieceType::Rook);
    std::uint8_t queen = encode(by, PieceType::Queen);
    for (int step : kRookSteps) {
        for (int from = sq + step; onBoard(from); from += step) {
            std::uint8_t code = board_[from];
            if (code == kEmpty) continue;
            if (code == rook || code == queen) return true;
            break;
        }
    }

    std::uint8_t bishop = encode(by, PieceType::Bishop);
    for (int step : kBishopSteps) {
        for (int from = sq + step; onBoard(from); from += step) {
            std::uint8_t code = board_[from];
            if (code == kEmpty) continue;
            if (code == bishop || code == queen) return true;
            break;
        }
    }
    return false;
}

bool Position::inCheck() const {
    return attacked(kingSquare(sideToMove_), opposite(sideToMove_));
}

void Position::addPseudoLegal(std::vector<Move>& out) const {
    const Color us = sideToMove_;
    const Color them = opposite(us);
    const int forward = us == Color::White ? 16 : -16;
    const int startRank = us == Color::White ? 1 : 6;
    const int lastRank = us == Color::White ? 7 : 0;

    auto push = [&](int from, int to, bool capture) {
        Move m;
        m.from = squareOf(from);
        m.to = squareOf(to);
        m.capture = capture;
        out.push_back(m);
    };

    for (int from = 0; from < 128; ++from) {
        if (!onBoard(from)) continue;
        std::uint8_t code = board_[from];
        if (code == kEmpty || colorOf(code) != us) continue;
        PieceType type = typeOf(code);

        switch (type) {
            case PieceType::Pawn: {
                int one = from + forward;
                if (onBoard(one) && board_[one] == kEmpty) {
                    if (squareOf(one).rank == lastRank) {
                        for (PieceType promo : kPromotionChoices) {
                            Move m;
                            m.from = squareOf(from);
                            m.to = squareOf(one);
                            m.promotion = promo;
                            out.push_back(m);
                        }
                    } else {
                        push(from, one, false);
                        int two = one + forward;
                        if (squareOf(from).rank == startRank && board_[two] == kEmpty) {
                            Move m;
                            m.from = squareOf(from);
                            m.to = squareOf(two);
                            m.doublePawnPush = true;
                            out.push_back(m);
                        }
                    }
                }
                for (int side : {-1, 1}) {
                    int to = from + forward + side;
                    if (!onBoard(to)) continue;
                    std::uint8_t target = board_[to];
                    if (target != kEmpty && colorOf(target) == them) {
                        if (squareOf(to).rank == lastRank) {
                            for (PieceType promo : kPromotionChoices) {
                                Move m;
                                m.from = squareOf(from);
                                m.to = squareOf(to);
                                m.promotion = promo;
                                m.capture = true;
                                out.push_back(m);
                            }
                        } else {
                            push(from, to, true);
                        }
                    } else if (target == kEmpty && to == epSquare_) {
                        Move m;
                        m.from = squareOf(from);
                        m.to = squareOf(to);
                        m.capture = true;
                        m.enPassant = true;
                        out.push_back(m);
                    }
                }
                break;
            }
            case PieceType::Knight: {
                for (int step : kKnightSteps) {
                    int to = from + step;
                    if (!onBoard(to)) continue;
                    std::uint8_t target = board_[to];
                    if (target != kEmpty && colorOf(target) == us) continue;
                    push(from, to, target != kEmpty);
                }
                break;
            }
            case PieceType::King: {
                for (int step : kKingSteps) {
                    int to = from + step;
                    if (!onBoard(to)) continue;
                    std::uint8_t target = board_[to];
                    if (target != kEmpty && colorOf(target) == us) continue;
                    push(from, to, target != kEmpty);
                }
                break;
            }
            default: {
                const int* steps = kKingSteps;
                int count = 8;
                if (type == PieceType::Rook) {
                    steps = kRookSteps;
                    count = 4;
                } else if (type == PieceType::Bishop) {
                    steps = kBishopSteps;
                    count = 4;
                }
                for (int i = 0; i < count; ++i) {
                    for (int to = from + steps[i]; onBoard(to); to += steps[i]) {
                        std::uint8_t target = board_[to];
                        if (target == kEmpty) {
                            push(from, to, false);
                            continue;
                        }
                        if (colorOf(target) != us) push(from, to, true);
                        break;
                    }
                }
                break;
            }
        }
    }

    // Castling. The king may not start in check, cross an attacked square, or
    // land on one, and every square between king and rook must be empty.
    const int home = us == Color::White ? 4 : 116;
    const std::uint8_t kingCode = encode(us, PieceType::King);
    const std::uint8_t rookCode = encode(us, PieceType::Rook);
    const std::uint8_t kingSide = us == Color::White ? WhiteKingSide : BlackKingSide;
    const std::uint8_t queenSide = us == Color::White ? WhiteQueenSide : BlackQueenSide;

    if (board_[home] == kingCode && !attacked(home, them)) {
        if ((castling_ & kingSide) && board_[home + 3] == rookCode &&
            board_[home + 1] == kEmpty && board_[home + 2] == kEmpty &&
            !attacked(home + 1, them) && !attacked(home + 2, them)) {
            Move m;
            m.from = squareOf(home);
            m.to = squareOf(home + 2);
            m.castling = true;
            out.push_back(m);
        }
        if ((castling_ & queenSide) && board_[home - 4] == rookCode &&
            board_[home - 1] == kEmpty && board_[home - 2] == kEmpty &&
            board_[home - 3] == kEmpty && !attacked(home - 1, them) &&
            !attacked(home - 2, them)) {
            Move m;
            m.from = squareOf(home);
            m.to = squareOf(home - 2);
            m.castling = true;
            out.push_back(m);
        }
    }
}

std::vector<Move> Position::legalMoves() const {
    std::vector<Move> pseudo;
    pseudo.reserve(48);
    addPseudoLegal(pseudo);

    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    const Color us = sideToMove_;
    for (const Move& m : pseudo) {
        Position next = apply(m);
        if (!next.attacked(next.kingSquare(us), opposite(us))) legal.push_back(m);
    }
    return legal;
}

Move Position::complete(Move m) const {
    // Recompute the flags from the board so a hand built move behaves the same
    // as one that legalMoves() produced.
    int from = indexOf(m.from);
    int to = indexOf(m.to);
    std::uint8_t code = board_[from];
    m.capture = board_[to] != kEmpty;
    m.castling = false;
    m.enPassant = false;
    m.doublePawnPush = false;
    if (code != kEmpty) {
        PieceType type = typeOf(code);
        if (type == PieceType::King && std::abs(m.to.file - m.from.file) == 2) {
            m.castling = true;
        }
        if (type == PieceType::Pawn) {
            if (std::abs(m.to.rank - m.from.rank) == 2) m.doublePawnPush = true;
            if (m.from.file != m.to.file && board_[to] == kEmpty && to == epSquare_) {
                m.enPassant = true;
                m.capture = true;
            }
        }
    }
    return m;
}

Position Position::apply(Move m) const {
    Position next = *this;
    m = complete(m);

    const int from = indexOf(m.from);
    const int to = indexOf(m.to);
    const std::uint8_t code = board_[from];
    const Color us = sideToMove_;
    const PieceType type = typeOf(code);

    next.epSquare_ = -1;
    ++next.halfmove_;
    if (type == PieceType::Pawn || m.capture) next.halfmove_ = 0;

    next.board_[from] = kEmpty;
    next.board_[to] = code;

    if (m.enPassant) {
        int victim = us == Color::White ? to - 16 : to + 16;
        next.board_[victim] = kEmpty;
    }

    if (m.promotion) next.board_[to] = encode(us, *m.promotion);

    if (m.castling) {
        if (to > from) {
            next.board_[from + 1] = next.board_[from + 3];
            next.board_[from + 3] = kEmpty;
        } else {
            next.board_[from - 1] = next.board_[from - 4];
            next.board_[from - 4] = kEmpty;
        }
    }

    if (type == PieceType::King) next.kingSq_[static_cast<int>(us)] = to;

    if (m.doublePawnPush) next.epSquare_ = us == Color::White ? from + 16 : from - 16;

    // Castling rights fall away when a king or rook leaves home, and when a
    // rook is captured on its home square.
    auto clearFor = [&](int square) {
        switch (square) {
            case 0: next.castling_ &= ~WhiteQueenSide; break;
            case 7: next.castling_ &= ~WhiteKingSide; break;
            case 112: next.castling_ &= ~BlackQueenSide; break;
            case 119: next.castling_ &= ~BlackKingSide; break;
            default: break;
        }
    };
    if (type == PieceType::King) {
        next.castling_ &= us == Color::White ? ~(WhiteKingSide | WhiteQueenSide)
                                             : ~(BlackKingSide | BlackQueenSide);
    }
    clearFor(from);
    clearFor(to);

    next.sideToMove_ = opposite(us);
    if (us == Color::Black) ++next.fullmove_;
    return next;
}

bool Position::insufficientMaterial() const {
    int knights = 0;
    int bishops = 0;
    int bishopSquareColors = 0;  // Bit 0 marks a light bishop, bit 1 a dark one.
    for (int sq = 0; sq < 128; ++sq) {
        if (!onBoard(sq)) continue;
        std::uint8_t code = board_[sq];
        if (code == kEmpty) continue;
        switch (typeOf(code)) {
            case PieceType::King: break;
            case PieceType::Knight: ++knights; break;
            case PieceType::Bishop: {
                ++bishops;
                Square s = squareOf(sq);
                bishopSquareColors |= ((s.file + s.rank) & 1) ? 1 : 2;
                break;
            }
            default: return false;  // A pawn, rook or queen can still mate.
        }
    }
    if (knights == 0 && bishops == 0) return true;              // Bare kings.
    if (knights == 1 && bishops == 0) return true;              // One knight.
    if (knights == 0 && bishops >= 1 && bishopSquareColors != 3) return true;
    return false;
}

GameResult Position::result() const {
    if (legalMoves().empty()) {
        return inCheck() ? GameResult::Checkmate : GameResult::Stalemate;
    }
    if (insufficientMaterial()) return GameResult::DrawInsufficientMaterial;
    if (halfmove_ >= 100) return GameResult::DrawFiftyMove;
    return GameResult::Ongoing;
}

std::uint64_t Position::perft(int depth) const {
    if (depth <= 0) return 1;
    std::vector<Move> moves = legalMoves();
    if (depth == 1) return moves.size();
    std::uint64_t nodes = 0;
    for (const Move& m : moves) nodes += apply(m).perft(depth - 1);
    return nodes;
}

std::string Position::san(Move m) const {
    m = complete(m);
    std::optional<Piece> moving = at(m.from);
    if (!moving) return {};

    std::string out;
    if (m.castling) {
        out = m.to.file > m.from.file ? "O-O" : "O-O-O";
    } else if (moving->type == PieceType::Pawn) {
        if (m.capture) {
            out += static_cast<char>('a' + m.from.file);
            out += 'x';
        }
        out += squareName(m.to);
        if (m.promotion) {
            out += '=';
            out += typeLetter(*m.promotion);
        }
    } else {
        out += typeLetter(moving->type);
        bool sameFile = false;
        bool sameRank = false;
        bool ambiguous = false;
        for (const Move& other : legalMoves()) {
            if (other.from == m.from || other.to != m.to) continue;
            std::optional<Piece> p = at(other.from);
            if (!p || p->type != moving->type) continue;
            ambiguous = true;
            if (other.from.file == m.from.file) sameFile = true;
            if (other.from.rank == m.from.rank) sameRank = true;
        }
        if (ambiguous) {
            if (!sameFile) {
                out += static_cast<char>('a' + m.from.file);
            } else if (!sameRank) {
                out += static_cast<char>('1' + m.from.rank);
            } else {
                out += squareName(m.from);
            }
        }
        if (m.capture) out += 'x';
        out += squareName(m.to);
    }

    Position next = apply(m);
    if (next.inCheck()) out += next.legalMoves().empty() ? '#' : '+';
    return out;
}

std::optional<Move> Position::parseSan(std::string_view text) const {
    std::string want;
    for (char c : text) {
        if (c == '!' || c == '?') continue;
        want += c;
    }
    // Accept the plain zero spelling of castling as well.
    if (want == "0-0") want = "O-O";
    if (want == "0-0-0") want = "O-O-O";

    auto strip = [](std::string s) {
        while (!s.empty() && (s.back() == '+' || s.back() == '#')) s.pop_back();
        return s;
    };
    std::string bare = strip(want);

    for (const Move& m : legalMoves()) {
        std::string produced = san(m);
        if (produced == want || strip(produced) == bare) return complete(m);
    }
    return std::nullopt;
}

std::string Position::longAlgebraic(Move m) const {
    std::string out = squareName(m.from) + squareName(m.to);
    if (m.promotion) {
        out += static_cast<char>(std::tolower(typeLetter(*m.promotion)));
    }
    return out;
}

std::optional<Move> Position::parseLongAlgebraic(std::string_view text) const {
    std::string cleaned;
    for (char c : text) {
        if (c == '-' || c == 'x' || c == '=') continue;
        cleaned += c;
    }
    if (cleaned.size() < 4 || cleaned.size() > 5) return std::nullopt;
    auto from = parseSquare(std::string_view(cleaned).substr(0, 2));
    auto to = parseSquare(std::string_view(cleaned).substr(2, 2));
    if (!from || !to) return std::nullopt;
    std::optional<PieceType> promo;
    if (cleaned.size() == 5) {
        promo = pieceTypeFromLetter(cleaned[4]);
        if (!promo || *promo == PieceType::King || *promo == PieceType::Pawn) {
            return std::nullopt;
        }
    }
    for (const Move& m : legalMoves()) {
        if (m.from == *from && m.to == *to && m.promotion == promo) return complete(m);
    }
    return std::nullopt;
}

Game::Game() : Game(Position::start()) {}

Game::Game(const Position& start) : start_(start) { positions_.push_back(start); }

bool Game::play(Move m) {
    for (const Move& legal : position().legalMoves()) {
        if (legal == m) {
            Move full = legal;
            positions_.push_back(position().apply(full));
            moves_.push_back(full);
            return true;
        }
    }
    return false;
}

bool Game::playSan(std::string_view text) {
    auto m = position().parseSan(text);
    if (!m) return false;
    return play(*m);
}

void Game::undo() {
    if (moves_.empty()) return;
    moves_.pop_back();
    positions_.pop_back();
}

void Game::reset() {
    moves_.clear();
    positions_.clear();
    positions_.push_back(start_);
}

int Game::repetitionCount() const {
    std::string key = position().repetitionKey();
    int count = 0;
    for (const Position& p : positions_) {
        if (p.repetitionKey() == key) ++count;
    }
    return count;
}

GameResult Game::result() const {
    GameResult direct = position().result();
    if (direct != GameResult::Ongoing) return direct;
    if (repetitionCount() >= 3) return GameResult::DrawThreefoldRepetition;
    return GameResult::Ongoing;
}

std::string Game::pgnMoves() const {
    std::string out;
    Position p = start_;
    int number = start_.fullmoveNumber();
    bool blackFirst = start_.sideToMove() == Color::Black;
    for (std::size_t i = 0; i < moves_.size(); ++i) {
        if (!out.empty()) out += ' ';
        if (p.sideToMove() == Color::White) {
            out += std::to_string(number);
            out += ". ";
        } else if (i == 0 && blackFirst) {
            out += std::to_string(number);
            out += "... ";
        }
        out += p.san(moves_[i]);
        if (p.sideToMove() == Color::Black) ++number;
        p = p.apply(moves_[i]);
    }
    return out;
}

void HumanMoveProvider::requestMove(const Position& position, RequestId id, MoveCallback done) {
    pending_ = true;
    id_ = id;
    position_ = position;
    done_ = std::move(done);
}

void HumanMoveProvider::cancel(RequestId id) {
    if (pending_ && id_ == id) {
        pending_ = false;
        done_ = nullptr;
    }
}

bool HumanMoveProvider::submit(Move m) {
    if (!pending_) return false;
    const auto legal = position_.legalMoves();
    auto it = std::find(legal.begin(), legal.end(), m);
    if (it == legal.end()) return false;

    pending_ = false;
    MoveCallback done = std::move(done_);
    done_ = nullptr;
    if (done) done(id_, *it);
    return true;
}

}  // namespace swchess::chess
