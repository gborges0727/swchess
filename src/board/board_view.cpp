#include "board/board_view.h"

#include <stdexcept>

#include "assets/cdfs.h"
#include "render/compositor.h"

namespace swchess::board {
namespace {

const SheetCell& cellAt(const PieceSheet& sheet, int row, int column) {
    for (const SheetCell& cell : sheet.cells) {
        if (cell.row == row && cell.column == column) {
            return cell;
        }
    }
    throw std::runtime_error("the sheet has no cell at that row and column");
}

}  // namespace

BoardScene loadBoardScene(const std::string& cdDir, SetId set) {
    BoardScene scene;
    scene.set = set;
    scene.settings = loadBoardSettingsFromCd(cdDir);
    scene.geometry = geometryFor(set, scene.settings);
    scene.sheet = loadPieceSheet(cdDir, setKey(set));
    scene.backgroundSource = backgroundName(set, scene.settings) + ".BMP";
    scene.background = loadBmp(resolveCdFile(cdDir, scene.backgroundSource).string());
    return scene;
}

std::vector<PieceSprite> drawOrder(const chess::Position& position, const BoardScene& scene) {
    bool flat = setIsFlat(scene.set);
    std::vector<PieceSprite> sprites;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            chess::Square square{file, rank};
            std::optional<chess::Piece> piece = position.at(square);
            if (!piece.has_value()) {
                continue;
            }
            ProjectedPoint anchor = scene.geometry.squareCenter(file, rank);
            PieceSprite sprite;
            sprite.square = square;
            sprite.piece = *piece;
            sprite.depth = anchor.depth;
            sprite.rect = spriteRect(ScreenPoint{anchor.x, anchor.y}, scene.sheet.cellWidth,
                                     scene.sheet.cellHeight, flat);

            // Insert the way FUN_1018_0ae3 does: walk past every entry whose
            // depth is at or above this one, then drop the sprite in.
            std::size_t at = 0;
            while (at < sprites.size() && sprites[at].depth >= sprite.depth) {
                ++at;
            }
            sprites.insert(sprites.begin() + static_cast<std::ptrdiff_t>(at), sprite);
        }
    }
    return sprites;
}

void renderPosition(const chess::Position& position, const BoardScene& scene, Image& out) {
    out = scene.background;
    for (const PieceSprite& sprite : drawOrder(position, scene)) {
        SheetPosition cell = pieceCell(scene.set, sprite.piece.type, sprite.piece.color);
        const SheetCell& source = cellAt(scene.sheet, cell.row, cell.column);
        blitRGBA(out, source.rgba.data(), source.width, source.height, sprite.rect.left,
                 sprite.rect.top);
    }
}

}  // namespace swchess::board
