// The square and piece codes the original engine and its data files use.
//
// BOOK.DAT and the engine's own board array both number a square as
// (row << 4) | column, where row 0 is rank 8 and column 0 is file a. So c2
// is 0x62 and c4 is 0x42. The rules module in src/chess counts ranks the
// other way, from White's first rank, so every square crossing the boundary
// goes through the two functions here.
#pragma once

#include <cstdint>
#include <optional>

#include "chess.h"

namespace swchess::engine::original {

// Turns an original square byte into a board square. Returns nothing when
// either half of the byte runs past the board.
constexpr std::optional<chess::Square> decodeSquare(std::uint8_t code) {
    const int row = code >> 4;
    const int col = code & 0x0f;
    if (row > 7 || col > 7) return std::nullopt;
    return chess::Square{col, 7 - row};
}

// Turns a board square back into an original square byte.
constexpr std::uint8_t encodeSquare(chess::Square s) {
    return static_cast<std::uint8_t>(((7 - s.rank) << 4) | s.file);
}

}  // namespace swchess::engine::original
