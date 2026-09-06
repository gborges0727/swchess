// Draws a whole position: the background bitmap first, then the twelve
// characters of one piece sheet standing on their squares.
#pragma once

#include <string>

#include "assets/bmp.h"
#include "assets/sheet.h"
#include "board/geometry.h"
#include "board/placement.h"
#include "chess.h"

namespace swchess::board {

// Everything one rendered board needs: which set, the layout its pieces stand
// on, the twelve cut cells, and the bitmap behind them.
struct BoardScene {
    SetId set = SetId::WhiteBottom;
    BoardSettings settings{};
    BoardGeometry geometry{};
    PieceSheet sheet{};
    Image background{};
    std::string backgroundSource;  // "SPACE256.BMP"
};

// Reads CMWIN.DAT, the sheet and the background out of the CD directory.
BoardScene loadBoardScene(const std::string& cdDir, SetId set);

// One piece about to be drawn.
struct PieceSprite {
    chess::Square square{};
    chess::Piece piece{};
    SpriteRect rect{};
    int depth = 0;
};

// The sprites of one position in the order the original blits them.
// FUN_1018_0ae3 inserts each new sprite after every entry whose depth is at
// or above its own, so the list runs from the largest depth to the smallest.
// Larger depth is farther away, so the far ranks draw first and a nearer
// piece overdraws a farther one.
std::vector<PieceSprite> drawOrder(const chess::Position& position, const BoardScene& scene);

// Composites the background and then every piece into `out`.
void renderPosition(const chess::Position& position, const BoardScene& scene, Image& out);

}  // namespace swchess::board
