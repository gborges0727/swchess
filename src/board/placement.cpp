#include "board/placement.h"

#include <array>
#include <stdexcept>

namespace swchess::board {
namespace {

// The six words at file offset 0x34e16, indexed by the piece order the game
// uses: king, queen, rook, bishop, knight, pawn.
constexpr std::array<int, 6> kColumnMap = {5, 4, 1, 3, 2, 0};

constexpr std::array<const char*, 4> kSetKeys = {"WHTBTM_", "WHTTOP_", "2DSET_", "FACING_"};

// True when the horizontal ray from the point crosses the edge from `a` to
// `b`. The board quadrilaterals are convex, so the even-odd rule and the
// winding rule agree and one crossing count decides the test.
bool crosses(int px, int py, const ProjectedPoint& a, const ProjectedPoint& b) {
    if ((a.y > py) == (b.y > py)) {
        return false;
    }
    // The x where the edge meets the scanline through the point.
    double t = static_cast<double>(py - a.y) / static_cast<double>(b.y - a.y);
    double x = static_cast<double>(a.x) + t * static_cast<double>(b.x - a.x);
    return static_cast<double>(px) < x;
}

}  // namespace

const char* setKey(SetId set) { return kSetKeys[static_cast<int>(set)]; }

std::optional<SetId> parseSetName(const std::string& name) {
    if (name == "WHTBTM") return SetId::WhiteBottom;
    if (name == "WHTTOP") return SetId::WhiteTop;
    if (name == "2D" || name == "2DSET") return SetId::TwoD;
    if (name == "FACING") return SetId::Facing;
    return std::nullopt;
}

bool setIsFlat(SetId set) { return set == SetId::TwoD; }

Orientation setOrientation(SetId set) {
    return set == SetId::WhiteTop ? Orientation::WhiteTop : Orientation::WhiteBottom;
}

BoardGeometry geometryFor(SetId set, const BoardSettings& settings) {
    Orientation orientation = setOrientation(set);
    return setIsFlat(set) ? BoardGeometry::flat(settings, orientation)
                          : BoardGeometry::tilted(settings, orientation);
}

std::string backgroundName(SetId set, const BoardSettings& settings) {
    if (setIsFlat(set)) {
        BoardGeometry geometry = geometryFor(set, settings);
        return geometry.turn() % 360 == 0 ? "2DBDBTOP" : "2DBDWTOP";
    }
    return settings.background == 0 ? "THRON256" : "SPACE256";
}

SpriteRect spriteRect(ScreenPoint anchor, int cellW, int cellH, bool flat) {
    int anchorY = flat ? cellH / 2 : cellH - cellW / 4;
    SpriteRect rect;
    rect.left = anchor.x - cellW / 2;
    rect.top = anchor.y - anchorY;
    rect.right = rect.left + cellW + 1;
    rect.bottom = rect.top + cellH + 1;
    return rect;
}

int pieceColumn(chess::PieceType type) { return kColumnMap[static_cast<int>(type)]; }

SheetPosition pieceCell(SetId set, chess::PieceType type, chess::Color color, int cellWidth,
                        int cellHeight) {
    (void)set;
    SheetPosition position;
    position.row = color == chess::Color::White ? 0 : 1;
    position.column = pieceColumn(type);
    // Every cell sits behind a one pixel separator on both axes.
    position.x = (cellWidth + 1) * position.column + 1;
    position.y = (cellHeight + 1) * position.row + 1;
    return position;
}

SheetPosition pieceCell(SetId set, chess::PieceType type, chess::Color color) {
    SheetPosition position = pieceCell(set, type, color, 0, 0);
    position.x = 0;
    position.y = 0;
    return position;
}

std::optional<chess::Square> hitTest(const BoardGeometry& geometry, int px, int py) {
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            std::array<ProjectedPoint, 4> quad = {
                geometry.corner(row, col),
                geometry.corner(row, col + 1),
                geometry.corner(row + 1, col + 1),
                geometry.corner(row + 1, col),
            };
            int crossings = 0;
            for (int i = 0; i < 4; ++i) {
                if (crosses(px, py, quad[i], quad[(i + 1) % 4])) {
                    ++crossings;
                }
            }
            if (crossings % 2 == 1) {
                return chess::Square{col, rankForRow(row)};
            }
        }
    }
    return std::nullopt;
}

}  // namespace swchess::board
