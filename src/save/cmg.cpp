#include "save/cmg.h"

#include <algorithm>
#include <array>

namespace swchess::save {
namespace {

using chess::Color;
using chess::Move;
using chess::PieceType;
using chess::Position;
using chess::Square;

// Field offsets inside the 101 byte header.
constexpr std::size_t kOffTitle = 0x00;
constexpr std::size_t kOffFormat = 0x21;
constexpr std::size_t kOffWhiteName = 0x22;
constexpr std::size_t kOffWhiteType = 0x42;
constexpr std::size_t kOffBlackName = 0x43;
constexpr std::size_t kOffBlackType = 0x63;

constexpr std::byte kEofMark{0x1A};
constexpr std::byte kFormatByte{0x20};

// No promotion. The writer uses 7 and the reader also accepts 6.
constexpr int kNoPromotion = 7;

std::uint8_t byteAt(std::span<const std::byte> bytes, std::size_t i) {
    return static_cast<std::uint8_t>(bytes[i]);
}

// Reads a NUL terminated string out of a fixed slot. The original allocates
// the header without zeroing it, so the bytes after the terminator are stale
// heap and mean nothing.
std::string readSlot(std::span<const std::byte> bytes, std::size_t offset, std::size_t size) {
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        char c = static_cast<char>(byteAt(bytes, offset + i));
        if (c == '\0') break;
        out += c;
    }
    return out;
}

void writeSlot(std::vector<std::byte>& out, const std::string& text, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        char c = i < text.size() ? text[i] : '\0';
        out.push_back(static_cast<std::byte>(c));
    }
}

void writeWord(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

std::uint16_t readWord(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(byteAt(bytes, offset) |
                                      (byteAt(bytes, offset + 1) << 8));
}

std::optional<PlayerType> decodeType(std::uint8_t value) {
    if (value == 1) return PlayerType::Human;
    if (value == 2) return PlayerType::Computer;
    return std::nullopt;
}

// The engine numbers ranks from the top, so rank index 0 is rank 8.
int rankIndexOf(int rank) { return 7 - rank; }
int rankFromIndex(int index) { return 7 - index; }

// One board byte. Bit 0x10 marks a white piece and 0x20 a black one, and the
// low three bits name the piece in the order King, Queen, Rook, Bishop,
// Knight, Pawn.
std::uint8_t encodePiece(chess::Piece p) {
    std::uint8_t color = p.color == Color::White ? 0x10 : 0x20;
    return static_cast<std::uint8_t>(color | static_cast<std::uint8_t>(p.type));
}

std::optional<chess::Piece> decodePiece(std::uint8_t code) {
    bool white = (code & 0x10) != 0;
    bool black = (code & 0x20) != 0;
    if (white == black) return std::nullopt;
    if ((code & ~0x37) != 0) return std::nullopt;
    int type = code & 0x07;
    if (type > 5) return std::nullopt;
    chess::Piece p;
    p.color = white ? Color::White : Color::Black;
    p.type = static_cast<PieceType>(type);
    return p;
}

// Builds a position out of the 64 board bytes. The file stores no castling
// rights, so this derives them the way FUN_11a0_00f8 does: a side keeps a
// right when its king stands on its home square and the matching rook stands
// in its corner. The file stores no en passant square either, so there is
// none.
Result<Position> positionFromBoard(std::span<const std::byte> board, Color sideToMove) {
    std::array<std::optional<chess::Piece>, 64> squares{};
    for (std::size_t i = 0; i < 64; ++i) {
        std::uint8_t code = static_cast<std::uint8_t>(board[i]);
        if (code == 0) continue;
        auto piece = decodePiece(code);
        if (!piece) {
            return {std::nullopt, "board byte " + std::to_string(i) + " names no piece"};
        }
        squares[i] = piece;
    }

    auto pieceAt = [&](int file, int rank) -> std::optional<chess::Piece> {
        return squares[static_cast<std::size_t>(rankIndexOf(rank) * 8 + file)];
    };
    auto isPiece = [&](int file, int rank, Color c, PieceType t) {
        auto p = pieceAt(file, rank);
        return p && p->color == c && p->type == t;
    };

    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int run = 0;
        for (int file = 0; file < 8; ++file) {
            auto p = pieceAt(file, rank);
            if (!p) {
                ++run;
                continue;
            }
            if (run > 0) {
                fen += static_cast<char>('0' + run);
                run = 0;
            }
            fen += chess::pieceLetter(*p);
        }
        if (run > 0) fen += static_cast<char>('0' + run);
        if (rank > 0) fen += '/';
    }
    fen += sideToMove == Color::White ? " w " : " b ";

    std::string rights;
    if (isPiece(4, 0, Color::White, PieceType::King)) {
        if (isPiece(7, 0, Color::White, PieceType::Rook)) rights += 'K';
        if (isPiece(0, 0, Color::White, PieceType::Rook)) rights += 'Q';
    }
    if (isPiece(4, 7, Color::Black, PieceType::King)) {
        if (isPiece(7, 7, Color::Black, PieceType::Rook)) rights += 'k';
        if (isPiece(0, 7, Color::Black, PieceType::Rook)) rights += 'q';
    }
    fen += rights.empty() ? "-" : rights;
    fen += " - 0 1";

    auto position = Position::fromFen(fen);
    if (!position) {
        return {std::nullopt, "the saved board is not a position the rules accept: " + fen};
    }
    return {*position, {}};
}

void writeBoard(std::vector<std::byte>& out, const Position& position) {
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            auto p = position.at(Square{file, rank});
            out.push_back(static_cast<std::byte>(p ? encodePiece(*p) : 0));
        }
    }
}

std::uint16_t encodeMove(const Move& m, bool hasAnnotation) {
    int promotion = m.promotion ? static_cast<int>(*m.promotion) : kNoPromotion;
    std::uint16_t word = static_cast<std::uint16_t>(
        m.to.file | (rankIndexOf(m.to.rank) << 3) | (m.from.file << 6) |
        (rankIndexOf(m.from.rank) << 9) | (promotion << 12));
    if (hasAnnotation) word |= 0x8000;
    return word;
}

}  // namespace

const char* playerTypeName(PlayerType t) {
    return t == PlayerType::Human ? "human" : "computer";
}

std::optional<PlayerType> playerTypeFromName(std::string_view name) {
    if (name == "human") return PlayerType::Human;
    if (name == "computer") return PlayerType::Computer;
    return std::nullopt;
}

chess::Game gameAtPly(const chess::Game& game, int ply) {
    chess::Game out(game.startPosition());
    int limit = std::clamp(ply, 0, static_cast<int>(game.moves().size()));
    for (int i = 0; i < limit; ++i) out.play(game.moves()[static_cast<std::size_t>(i)]);
    return out;
}

Result<CmgGame> readCmg(std::span<const std::byte> bytes) {
    if (bytes.size() >= kMaxFileSize) {
        return {std::nullopt, "the file is 32000 bytes or larger, which the original refuses"};
    }
    // The header plus the two markers, the position flag, the move count and
    // the current ply.
    constexpr std::size_t kSmallest = kHeaderSize + 7;
    if (bytes.size() < kSmallest) {
        return {std::nullopt, "the file is too short to hold a header and an engine block"};
    }
    if (bytes[kOffFormat] != kFormatByte) {
        return {std::nullopt, "unknown game file format: byte 0x21 is not 0x20"};
    }

    CmgGame result;
    CmgMetadata& meta = result.meta;
    meta.title = readSlot(bytes, kOffTitle, kNameSlot);
    meta.whiteName = readSlot(bytes, kOffWhiteName, kNameSlot);
    meta.blackName = readSlot(bytes, kOffBlackName, kNameSlot);

    auto whiteType = decodeType(byteAt(bytes, kOffWhiteType));
    auto blackType = decodeType(byteAt(bytes, kOffBlackType));
    if (!whiteType || !blackType) {
        return {std::nullopt, "a player type byte is neither 1 for human nor 2 for computer"};
    }
    meta.whiteType = *whiteType;
    meta.blackType = *blackType;

    std::size_t at = kHeaderSize;
    if (bytes[at] != kEofMark || bytes[at + 1] != kFormatByte) {
        return {std::nullopt, "the engine block does not start with 0x1A 0x20"};
    }
    at += 2;

    std::uint8_t flag = byteAt(bytes, at);
    ++at;
    result.positionFlag = flag;

    Position start;
    if (flag == kFlagStandardOpening) {
        start = Position::start();
    } else if (flag == kFlagBoardWhiteFirst || flag == kFlagBoardBlackFirst) {
        if (bytes.size() < at + 64 + 4) {
            return {std::nullopt, "the file ends inside the saved board"};
        }
        Color side = flag == kFlagBoardWhiteFirst ? Color::White : Color::Black;
        auto position = positionFromBoard(bytes.subspan(at, 64), side);
        if (!position) return {std::nullopt, position.error};
        start = *position;
        at += 64;
    } else {
        return {std::nullopt, "position flag 0x" + std::to_string(flag) + " is not 0xFF, 0x10 or 0x20"};
    }

    int moveCount = static_cast<std::int16_t>(readWord(bytes, at));
    int currentPly = static_cast<std::int16_t>(readWord(bytes, at + 2));
    at += 4;

    if (moveCount < 0) {
        return {std::nullopt,
                "this file holds a variation tree, which the port does not read yet"};
    }
    if (currentPly < 0 || currentPly > moveCount) {
        return {std::nullopt, "the current ply is outside the move list"};
    }
    meta.currentPly = currentPly;

    chess::Game game(start);
    meta.moveTimes.clear();
    meta.annotations.clear();

    for (int i = 0; i < moveCount; ++i) {
        if (at + 4 > bytes.size()) {
            return {std::nullopt, "the file ends inside move record " + std::to_string(i + 1)};
        }
        std::uint16_t word = readWord(bytes, at);
        std::uint16_t time = readWord(bytes, at + 2);
        at += 4;

        std::string annotation;
        if ((word & 0x8000) != 0) {
            std::size_t end = at;
            while (end < bytes.size() && bytes[end] != std::byte{0}) ++end;
            if (end >= bytes.size()) {
                return {std::nullopt,
                        "the annotation on move " + std::to_string(i + 1) + " has no terminator"};
            }
            annotation.assign(reinterpret_cast<const char*>(bytes.data() + at), end - at);
            at = end + 1;
        }

        Square from{static_cast<int>((word >> 6) & 7), rankFromIndex((word >> 9) & 7)};
        Square to{static_cast<int>(word & 7), rankFromIndex((word >> 3) & 7)};

        if (from == to) {
            std::uint16_t code = word & 0x7000;
            if (code != kGameOverA && code != kGameOverB && code != kGameOverC) {
                return {std::nullopt, "record " + std::to_string(i + 1) +
                                          " ends the game for an unknown reason"};
            }
            if (i != moveCount - 1) {
                return {std::nullopt, "a game over record sits before the last move"};
            }
            meta.gameOverCode = code;
            break;
        }

        Move wanted;
        wanted.from = from;
        wanted.to = to;
        int promotion = (word >> 12) & 7;
        if (promotion <= 5) wanted.promotion = static_cast<PieceType>(promotion);

        const auto legal = game.position().legalMoves();
        auto found = std::find(legal.begin(), legal.end(), wanted);
        if (found == legal.end()) {
            return {std::nullopt, "move " + std::to_string(i + 1) + ", " +
                                      chess::squareName(from) + chess::squareName(to) +
                                      ", is not legal in the position it reaches"};
        }
        game.play(*found);
        meta.moveTimes.push_back(time);
        meta.annotations.push_back(annotation);
    }

    result.game = std::move(game);
    return {std::move(result), {}};
}

Result<std::vector<std::byte>> writeCmg(const chess::Game& game, const CmgMetadata& meta) {
    auto tooLong = [](const std::string& s) { return s.size() >= kNameSlot; };
    if (tooLong(meta.title)) {
        return {std::nullopt, "the title does not fit in 31 characters"};
    }
    if (tooLong(meta.whiteName) || tooLong(meta.blackName)) {
        return {std::nullopt, "a player name does not fit in 31 characters"};
    }

    const auto& moves = game.moves();
    if (moves.size() > 0x7FFF) {
        return {std::nullopt, "the game has more moves than the file can count"};
    }
    if (!meta.moveTimes.empty() && meta.moveTimes.size() != moves.size()) {
        return {std::nullopt, "the move times do not line up with the move list"};
    }
    if (!meta.annotations.empty() && meta.annotations.size() != moves.size()) {
        return {std::nullopt, "the annotations do not line up with the move list"};
    }

    int currentPly = meta.currentPly < 0 ? static_cast<int>(moves.size()) : meta.currentPly;
    if (currentPly > static_cast<int>(moves.size())) {
        return {std::nullopt, "the current ply is past the end of the move list"};
    }

    std::vector<std::byte> out;
    writeSlot(out, meta.title, kNameSlot);
    out.push_back(kEofMark);
    out.push_back(kFormatByte);
    writeSlot(out, meta.whiteName, kNameSlot);
    out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(meta.whiteType)));
    writeSlot(out, meta.blackName, kNameSlot);
    out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(meta.blackType)));
    out.push_back(std::byte{0});
    out.push_back(kEofMark);
    out.push_back(kFormatByte);

    const Position& start = game.startPosition();
    if (start == Position::start()) {
        out.push_back(static_cast<std::byte>(kFlagStandardOpening));
    } else {
        out.push_back(static_cast<std::byte>(start.sideToMove() == Color::White
                                                 ? kFlagBoardWhiteFirst
                                                 : kFlagBoardBlackFirst));
        writeBoard(out, start);
    }

    int recordCount = static_cast<int>(moves.size()) + (meta.gameOverCode ? 1 : 0);
    writeWord(out, static_cast<std::uint16_t>(recordCount));
    writeWord(out, static_cast<std::uint16_t>(currentPly));

    for (std::size_t i = 0; i < moves.size(); ++i) {
        const std::string annotation =
            i < meta.annotations.size() ? meta.annotations[i] : std::string{};
        if (annotation.find('\0') != std::string::npos) {
            return {std::nullopt, "an annotation holds a NUL, which would end it early"};
        }
        writeWord(out, encodeMove(moves[i], !annotation.empty()));
        writeWord(out, i < meta.moveTimes.size() ? meta.moveTimes[i] : std::uint16_t{0});
        if (!annotation.empty()) {
            for (char c : annotation) out.push_back(static_cast<std::byte>(c));
            out.push_back(std::byte{0});
        }
    }

    if (meta.gameOverCode) {
        std::uint16_t code = *meta.gameOverCode;
        if (code != kGameOverA && code != kGameOverB && code != kGameOverC) {
            return {std::nullopt, "the game over code is none the engine writes"};
        }
        writeWord(out, code);
        writeWord(out, 0);
    }

    if (out.size() >= kMaxFileSize) {
        return {std::nullopt, "the game is too long for a file the original will open"};
    }
    return {std::move(out), {}};
}

}  // namespace swchess::save
