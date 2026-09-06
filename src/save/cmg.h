// Reading and writing the original Star Wars Chess saved game, a `.CMG` file.
//
// docs/research/saved-games.md describes the format. A file is a 101 byte
// header holding the title, the two player names and their types, followed by
// the engine block holding the starting position, the move count, the current
// ply and one four byte record per move.
//
// The module depends on the chess rules module and the standard library. It
// opens no files. The caller reads the bytes and passes them in.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "chess.h"

namespace swchess::save {

// Either a value or the reason there is none. The project builds as C++20, so
// std::expected is not available yet.
template <class T>
struct Result {
    std::optional<T> value{};
    std::string error{};

    explicit operator bool() const { return value.has_value(); }
    const T& operator*() const { return *value; }
    const T* operator->() const { return &*value; }
};

// Who plays a side. The header stores 1 for a person and 2 for the program.
enum class PlayerType : std::uint8_t { Human = 1, Computer = 2 };

const char* playerTypeName(PlayerType t);
std::optional<PlayerType> playerTypeFromName(std::string_view name);

// The position flag at the front of the engine block.
inline constexpr std::uint8_t kFlagStandardOpening = 0xFF;
inline constexpr std::uint8_t kFlagBoardWhiteFirst = 0x10;
inline constexpr std::uint8_t kFlagBoardBlackFirst = 0x20;

// The three game over codes the engine writes into a move record. This build
// of the game shows a blank string for all of them.
inline constexpr std::uint16_t kGameOverA = 0x4000;
inline constexpr std::uint16_t kGameOverB = 0x5000;
inline constexpr std::uint16_t kGameOverC = 0x6000;

// The title slot and both name slots hold 32 bytes including the terminator.
inline constexpr std::size_t kNameSlot = 32;
inline constexpr std::size_t kHeaderSize = 101;
// The loader in XCHESS.EXE refuses a file this large or larger.
inline constexpr std::size_t kMaxFileSize = 32000;

// Everything in a `.CMG` file except the moves themselves.
struct CmgMetadata {
    // The Delete Game dialog lists this string, so a saved file should carry
    // it. The original always writes "Starwars Chess Game".
    std::string title = "Starwars Chess Game";
    std::string whiteName = "White";
    PlayerType whiteType = PlayerType::Human;
    std::string blackName = "Black";
    PlayerType blackType = PlayerType::Human;

    // How many moves are played when the file opens. A smaller number than
    // the move list means the player had taken moves back. -1 tells the writer
    // to use the whole move list.
    int currentPly = -1;

    // One entry per move, in timer ticks of a tenth of a second. It says what
    // that side spent on that one move. A short vector means zero for the
    // rest.
    std::vector<std::uint16_t> moveTimes{};
    // One entry per move. An empty string writes no annotation.
    std::vector<std::string> annotations{};
    // The engine appends this marker record after the last move.
    std::optional<std::uint16_t> gameOverCode{};
};

struct CmgGame {
    CmgMetadata meta{};
    // 0xFF for the standard opening, 0x10 or 0x20 for a board that the file
    // spells out.
    std::uint8_t positionFlag = kFlagStandardOpening;
    // The start position with every recorded move replayed onto it. Each move
    // was checked against the rules module while it was read.
    chess::Game game{};
};

// Decodes one file. Rejects a wrong format byte, a truncated file, a board
// byte that names no piece, a variation tree, and any move the rules module
// calls illegal.
Result<CmgGame> readCmg(std::span<const std::byte> bytes);

// Encodes a game the original program can open again. Fails when a name is
// too long for its 32 byte slot, when the move times or annotations do not
// line up with the move list, or when the result would reach 32000 bytes.
//
// The file keeps no castling rights, no en passant square and no halfmove
// clock. The original engine derives castling from where the kings and rooks
// stand and starts the other two over, so a game that starts from a custom
// position loses them.
Result<std::vector<std::byte>> writeCmg(const chess::Game& game, const CmgMetadata& meta);

// Replays the first `ply` moves of `game` onto its start position. A ply
// outside the move list clamps to the nearest end.
chess::Game gameAtPly(const chess::Game& game, int ply);

}  // namespace swchess::save
