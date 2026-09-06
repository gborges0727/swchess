// Where a piece sprite lands, which sheet cell holds it, and which square a
// click falls on. docs/research/board-geometry.md records the functions this
// file reproduces.
#pragma once

#include <optional>
#include <string>

#include "board/geometry.h"
#include "chess.h"

namespace swchess::board {

// The four CM.INI [chesssets] records, in file order. The index doubles as
// the value stored at 11d8:6841.
enum class SetId : std::uint8_t { WhiteBottom = 0, WhiteTop = 1, TwoD = 2, Facing = 3 };

// The CM.INI key of one set, such as "WHTBTM_".
const char* setKey(SetId set);

// Parses "WHTBTM", "WHTTOP", "2D" or "FACING", the names the viewer accepts.
std::optional<SetId> parseSetName(const std::string& name);

// Only the 2D set stands on a flat board.
bool setIsFlat(SetId set);

// The orientation each set draws in. Both 3D sets that hold a full army pick
// their orientation by name, and the setup and 2D sheets keep white at the
// bottom.
Orientation setOrientation(SetId set);

// Builds the layout one set stands on.
BoardGeometry geometryFor(SetId set, const BoardSettings& settings);

// The background bitmap name without its extension, following
// FUN_1008_1311. The 2D board takes one of the two flat bitmaps, and every
// tilted board takes the throne room or space by the CMWIN.DAT choice.
std::string backgroundName(SetId set, const BoardSettings& settings);

// A sprite rectangle. `right` and `bottom` sit one pixel past the far edge
// plus one, the way FUN_1018_09f3 fills the RECT in.
struct SpriteRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    friend bool operator==(const SpriteRect&, const SpriteRect&) = default;
};

// Places a cell of `cellW` by `cellH` on the anchor pixel. A tilted board
// anchors the sprite at bottom center raised by a quarter of the cell width,
// and a flat board anchors it at its exact center.
SpriteRect spriteRect(ScreenPoint anchor, int cellW, int cellH, bool flat);

// One cell of a piece sheet.
struct SheetPosition {
    int row = 0;     // 0 is white and 1 is black
    int column = 0;  // the column the table at file offset 0x34e16 gives
    int x = 0;       // left edge inside the sheet
    int y = 0;       // top edge inside the sheet
};

// The sheet column of one piece. The table holds 5, 4, 1, 3, 2, 0 indexed by
// king, queen, rook, bishop, knight, pawn.
int pieceColumn(chess::PieceType type);

// The sheet cell holding one piece. Every set uses the same table, so the set
// only supplies the cell size.
SheetPosition pieceCell(SetId set, chess::PieceType type, chess::Color color, int cellWidth,
                        int cellHeight);

// The same cell without the pixel offsets, for callers that index a
// PieceSheet by row and column.
SheetPosition pieceCell(SetId set, chess::PieceType type, chess::Color color);

// The square under a screen pixel, or nothing when the pixel misses the
// board. This is the second step of SUB_1028_08b2: a point in polygon test
// against each of the 64 projected quadrilaterals, rows 0 to 7 and columns 0
// to 7, first match winning. The first step, which picks the nearest piece by
// reading its mask, needs the drawn sprites and lives with the renderer.
std::optional<chess::Square> hitTest(const BoardGeometry& geometry, int px, int py);

}  // namespace swchess::board
