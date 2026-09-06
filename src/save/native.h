// The native saved game of the port, a small JSON document.
//
// The original `.CMG` file stores only what its engine needed, so it loses the
// castling rights, the en passant square and the halfmove clock. The native
// save writes the start position as a FEN string and every move in long
// algebraic notation, so a game reloads exactly as it stood. It also keeps the
// settings the original kept in SWC.INI.
//
// The module reads and writes strings. The caller opens the file.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "chess.h"
#include "save/cmg.h"

namespace swchess::save {

// The current document version. A reader refuses a document from a later
// version, because it cannot know what changed.
inline constexpr int kNativeVersion = 1;

// What the player picked in the menus. The names are plain strings rather than
// the enums in src/game and src/ui, so this module compiles against the rules
// module alone.
struct NativeSettings {
    // Which piece set stands on which side, "white_bottom" or "black_bottom".
    std::string set = "white_bottom";
    // The language of the string tables, such as "english".
    std::string language = "english";
    // Which pictures a capture draws, "interpolated60" or "original120ms".
    std::string cadence = "interpolated60";
    // The three look and feel toggles the original wrote to SWC.INI.
    bool walking = true;
    bool captures = true;
    bool sounds = true;
};

struct NativeSave {
    int version = kNativeVersion;
    // The position the game started from.
    std::string startFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    // Every move played, each one like "e2e4" or "e7e8q".
    std::vector<std::string> moves{};
    // How many of those moves are played when the file opens.
    int currentPly = 0;
    std::string whiteName = "White";
    PlayerType whiteType = PlayerType::Human;
    std::string blackName = "Black";
    PlayerType blackType = PlayerType::Human;
    NativeSettings settings{};
};

// Turns a document into JSON text with two space indentation.
std::string writeNative(const NativeSave& save);

// Parses the JSON text. Rejects malformed JSON, a version this build does not
// know, a start position that is not a FEN, a move that is not legal where it
// stands, and a current ply outside the move list.
Result<NativeSave> readNative(std::string_view text);

// Builds a document out of a played game. `currentPly` below zero means every
// move is played.
NativeSave nativeFromGame(const chess::Game& game, const NativeSave& fields,
                          int currentPly = -1);

// Replays a document into a game holding all of its moves. Fails on the same
// grounds readNative does.
Result<chess::Game> gameFromNative(const NativeSave& save);

}  // namespace swchess::save
