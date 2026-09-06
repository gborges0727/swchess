// Checks the board module against docs/research/board-geometry.md.
//
//   board_test <cd directory>
//
// The square centers, the grid corners and the depths are typed in from the
// doc, so a change in the projection arithmetic fails here.

#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "board/board_view.h"
#include "board/geometry.h"
#include "board/placement.h"
#include "chess.h"
#include "render/compositor.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
        ++failures;
    }
}

void checkInt(int actual, int expected, const std::string& what) {
    if (actual != expected) {
        std::fprintf(stderr, "FAIL %s: got %d, want %d\n", what.c_str(), actual, expected);
        ++failures;
    }
}

// The square centers of the doc, rank 8 first and file a first, as x, y
// pairs. Row 0 is rank 8.
constexpr int kCenters[8][8][2] = {
    {{108, 130}, {168, 130}, {228, 130}, {288, 130}, {347, 130}, {408, 130}, {468, 130}, {528, 130}},
    {{102, 163}, {164, 163}, {225, 163}, {287, 163}, {348, 163}, {410, 163}, {472, 163}, {534, 163}},
    {{95, 199}, {159, 199}, {223, 199}, {286, 199}, {349, 199}, {413, 199}, {476, 199}, {540, 199}},
    {{89, 237}, {154, 237}, {220, 237}, {285, 237}, {350, 237}, {416, 237}, {481, 237}, {547, 237}},
    {{82, 275}, {150, 275}, {217, 275}, {285, 275}, {351, 275}, {418, 275}, {486, 275}, {553, 275}},
    {{75, 318}, {144, 318}, {214, 318}, {283, 318}, {352, 318}, {422, 318}, {491, 318}, {561, 318}},
    {{67, 363}, {139, 363}, {210, 363}, {282, 363}, {353, 363}, {425, 363}, {497, 363}, {569, 363}},
    {{58, 411}, {133, 411}, {207, 411}, {281, 411}, {354, 411}, {429, 411}, {503, 411}, {577, 411}},
};

// The depth of each rank, rank 8 first.
constexpr int kDepths[8] = {2154, 2047, 1941, 1835, 1730, 1624, 1517, 1411};

// The nine by nine grid corners, corner row 0 first.
constexpr int kCorners[9][9][2] = {
    {{81, 114}, {140, 114}, {200, 114}, {259, 114}, {318, 114}, {377, 114}, {436, 114}, {496, 114}, {555, 114}},
    {{74, 146}, {135, 146}, {196, 146}, {257, 146}, {318, 146}, {379, 146}, {440, 146}, {501, 146}, {562, 146}},
    {{67, 181}, {130, 181}, {193, 181}, {256, 181}, {318, 181}, {380, 181}, {443, 181}, {506, 181}, {569, 181}},
    {{60, 218}, {125, 218}, {189, 218}, {254, 218}, {318, 218}, {382, 218}, {447, 218}, {511, 218}, {576, 218}},
    {{52, 256}, {119, 256}, {185, 256}, {252, 256}, {318, 256}, {384, 256}, {451, 256}, {517, 256}, {584, 256}},
    {{44, 297}, {113, 297}, {181, 297}, {250, 297}, {318, 297}, {386, 297}, {455, 297}, {523, 297}, {592, 297}},
    {{36, 341}, {106, 341}, {177, 341}, {248, 341}, {318, 341}, {388, 341}, {459, 341}, {530, 341}, {600, 341}},
    {{26, 387}, {99, 387}, {172, 387}, {245, 387}, {318, 387}, {391, 387}, {464, 387}, {537, 387}, {610, 387}},
    {{17, 437}, {92, 437}, {168, 437}, {243, 437}, {318, 437}, {393, 437}, {468, 437}, {544, 437}, {619, 437}},
};

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    std::fclose(file);
    return bytes;
}

std::int32_t readLong(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t value = static_cast<std::uint32_t>(bytes[offset]) |
                          (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
                          (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
                          (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    return static_cast<std::int32_t>(value);
}

// The shipped CMWIN.DAT holds these numbers, and CMWIND.DAT repeats them.
void testSettings(const std::string& cdDir) {
    swchess::board::BoardSettings settings = swchess::board::loadBoardSettingsFromCd(cdDir);
    checkInt(settings.bitmapStyle, 2, "bitmap_style");
    checkInt(settings.vanish3D, 335, "vanishpt_3D");
    checkInt(settings.size3D, 535, "size_3D");
    checkInt(settings.turn3D, 360, "turn_3D");
    checkInt(settings.tilt3D, 307, "tilt_3D");
    checkInt(settings.wood3DCenterX, 319, "3D wood center x");
    checkInt(settings.wood3DCenterY, 226, "3D wood center y");
    checkInt(settings.wood2DCenterX, 275, "2D wood center x");
    checkInt(settings.wood2DCenterY, 219, "2D wood center y");
    checkInt(settings.marble3DCenterX, 318, "3D marble center x");
    checkInt(settings.marble3DCenterY, 256, "3D marble center y");
    checkInt(settings.marble2DCenterX, 275, "2D marble center x");
    checkInt(settings.marble2DCenterY, 217, "2D marble center y");
    checkInt(settings.size2D, 394, "size_2D");
    checkInt(settings.turn2D, 360, "turn_2D");
    checkInt(settings.vanish, 335, "live vanishing point");
    checkInt(settings.size, 535, "live board size");
    checkInt(settings.turn, 360, "live turn angle");
    checkInt(settings.tilt, 307, "live tilt angle");
    checkInt(settings.chessSet, 0, "selected chess set");
    checkInt(settings.background, 1, "background choice");

    std::vector<std::uint8_t> live = readFile(cdDir + "/CMWIN.DAT");
    std::vector<std::uint8_t> spare = readFile(cdDir + "/CMWIND.DAT");
    checkInt(static_cast<int>(live.size()), 144, "CMWIN.DAT length");
    check(live == spare, "CMWIN.DAT and CMWIND.DAT hold the same bytes");
}

// The tables the projection turns on are copied out of the executable.
void testTrigTables(const std::string& cdDir) {
    std::vector<std::uint8_t> exe = readFile(cdDir + "/XCHESS.EXE");
    check(exe.size() > 0x35704 + 1440, "XCHESS.EXE reaches past the cosine table");
    if (exe.size() <= 0x35704 + 1440) {
        return;
    }
    int sineMismatches = 0;
    int cosineMismatches = 0;
    for (int i = 0; i < 360; ++i) {
        if (swchess::board::sineTable()[i] !=
            readLong(exe, 0x35164 + static_cast<std::size_t>(i) * 4)) {
            ++sineMismatches;
        }
        if (swchess::board::cosineTable()[i] !=
            readLong(exe, 0x35704 + static_cast<std::size_t>(i) * 4)) {
            ++cosineMismatches;
        }
    }
    checkInt(sineMismatches, 0, "sine table entries that differ from XCHESS.EXE");
    checkInt(cosineMismatches, 0, "cosine table entries that differ from XCHESS.EXE");
}

void testGeometry(const swchess::board::BoardSettings& settings) {
    swchess::board::BoardGeometry geometry =
        swchess::board::BoardGeometry::tilted(settings, swchess::board::Orientation::WhiteBottom);
    check(!geometry.isFlat(), "the shipped board is tilted");
    checkInt(geometry.step(), 133, "one square is 133 model units wide");
    checkInt(geometry.grid(0), -532, "grid[0]");
    checkInt(geometry.grid(8), 532, "grid[8]");
    checkInt(geometry.center().x, 318, "board center x");
    checkInt(geometry.center().y, 256, "board center y");

    for (int row = 0; row < 8; ++row) {
        int rank = swchess::board::rankForRow(row);
        for (int file = 0; file < 8; ++file) {
            swchess::board::ProjectedPoint point = geometry.squareCenter(file, rank);
            std::string name = "center of " + swchess::chess::squareName({file, rank});
            checkInt(point.x, kCenters[row][file][0], name + " x");
            checkInt(point.y, kCenters[row][file][1], name + " y");
            checkInt(point.depth, kDepths[row], name + " depth");
        }
    }
    for (int row = 0; row < 9; ++row) {
        for (int col = 0; col < 9; ++col) {
            swchess::board::ProjectedPoint point = geometry.corner(row, col);
            std::string name =
                "corner r" + std::to_string(row) + " c" + std::to_string(col);
            checkInt(point.x, kCorners[row][col][0], name + " x");
            checkInt(point.y, kCorners[row][col][1], name + " y");
        }
    }

    // Turning the board half a circle puts a1 where h8 stood. The two
    // pixels differ by one because the square centers sit half a square
    // inside the grid, which the half turn does not mirror exactly.
    swchess::board::ProjectedPoint a1 =
        geometry.squareCenter(0, 0, swchess::board::Orientation::WhiteTop);
    checkInt(a1.x, 529, "a1 with white on top x");
    checkInt(a1.y, 130, "a1 with white on top y");
    checkInt(a1.depth, 2153, "a1 with white on top is the farthest rank");

    // The flat board halves its 98 unit squares and sits on the 2D center.
    swchess::board::BoardGeometry flat =
        swchess::board::BoardGeometry::flat(settings, swchess::board::Orientation::WhiteBottom);
    check(flat.isFlat(), "the 2D board is flat");
    checkInt(flat.step(), 98, "one 2D square is 98 model units wide");
    checkInt(flat.center().x, 275, "2D board center x");
    checkInt(flat.center().y, 217, "2D board center y");
    swchess::board::ProjectedPoint flatA1 = flat.squareCenter(0, 0);
    swchess::board::ProjectedPoint flatA8 = flat.squareCenter(0, 7);
    checkInt(flatA1.x, 104, "2D a1 x");
    checkInt(flatA1.y, 388, "2D a1 y");
    checkInt(flatA8.x, 104, "2D a8 x");
    checkInt(flatA8.y, 46, "2D a8 y");
    checkInt(flatA1.depth, flatA8.depth, "every square of a flat board sits at one depth");
}

void testHitTest(const swchess::board::BoardGeometry& geometry) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            swchess::board::ProjectedPoint point = geometry.squareCenter(file, rank);
            std::optional<swchess::chess::Square> hit =
                swchess::board::hitTest(geometry, point.x, point.y);
            std::string name = swchess::chess::squareName({file, rank});
            if (!hit.has_value()) {
                std::fprintf(stderr, "FAIL the center of %s hits no square\n", name.c_str());
                ++failures;
                continue;
            }
            check(*hit == swchess::chess::Square(file, rank),
                  "the center of " + name + " hits " + swchess::chess::squareName(*hit));
        }
    }
    const int corners[4][2] = {{0, 0}, {639, 0}, {0, 479}, {639, 479}};
    for (const auto& corner : corners) {
        std::optional<swchess::chess::Square> hit =
            swchess::board::hitTest(geometry, corner[0], corner[1]);
        check(!hit.has_value(), "the screen corner " + std::to_string(corner[0]) + "," +
                                    std::to_string(corner[1]) + " hits no square");
    }
}

void testSpriteRects(const swchess::board::BoardGeometry& geometry) {
    // A 3D cell is 68 by 142, so the anchor sits 17 pixels above the bottom
    // edge and halfway across.
    swchess::board::ProjectedPoint a1 = geometry.squareCenter(0, 0);
    swchess::board::SpriteRect rect =
        swchess::board::spriteRect(swchess::board::ScreenPoint{a1.x, a1.y}, 68, 142, false);
    checkInt(rect.left, 24, "a1 sprite left");
    checkInt(rect.top, 286, "a1 sprite top");
    checkInt(rect.right, 93, "a1 sprite right");
    checkInt(rect.bottom, 429, "a1 sprite bottom");

    swchess::board::ProjectedPoint h8 = geometry.squareCenter(7, 7);
    rect = swchess::board::spriteRect(swchess::board::ScreenPoint{h8.x, h8.y}, 68, 142, false);
    checkInt(rect.left, 494, "h8 sprite left");
    checkInt(rect.top, 5, "h8 sprite top");
    checkInt(rect.right, 563, "h8 sprite right");
    checkInt(rect.bottom, 148, "h8 sprite bottom");

    // A flat set anchors the sprite at its exact center.
    rect = swchess::board::spriteRect(swchess::board::ScreenPoint{100, 100}, 37, 45, true);
    checkInt(rect.left, 82, "2D sprite left");
    checkInt(rect.top, 78, "2D sprite top");
}

void testPieceCells() {
    using swchess::chess::Color;
    using swchess::chess::PieceType;
    const struct {
        PieceType type;
        int column;
    } expected[6] = {
        {PieceType::King, 5}, {PieceType::Queen, 4},  {PieceType::Rook, 1},
        {PieceType::Bishop, 3}, {PieceType::Knight, 2}, {PieceType::Pawn, 0},
    };
    for (const auto& entry : expected) {
        for (int colorIndex = 0; colorIndex < 2; ++colorIndex) {
            Color color = colorIndex == 0 ? Color::White : Color::Black;
            swchess::board::SheetPosition cell = swchess::board::pieceCell(
                swchess::board::SetId::WhiteBottom, entry.type, color, 68, 142);
            std::string name = std::string(colorIndex == 0 ? "white " : "black ") +
                               swchess::chess::pieceLetter({color, entry.type});
            checkInt(cell.column, entry.column, name + " column");
            checkInt(cell.row, colorIndex, name + " row");
            checkInt(cell.x, 69 * entry.column + 1, name + " sheet x");
            checkInt(cell.y, 143 * colorIndex + 1, name + " sheet y");
        }
    }
}

// Draws the start position with every set and writes one PPM each.
void testRender(const std::string& cdDir) {
    const swchess::board::SetId sets[4] = {
        swchess::board::SetId::WhiteBottom,
        swchess::board::SetId::WhiteTop,
        swchess::board::SetId::Facing,
        swchess::board::SetId::TwoD,
    };
    const char* names[4] = {"whtbtm", "whttop", "facing", "2d"};
    swchess::chess::Position start = swchess::chess::Position::start();
    for (int i = 0; i < 4; ++i) {
        swchess::board::BoardScene scene = swchess::board::loadBoardScene(cdDir, sets[i]);
        std::vector<swchess::board::PieceSprite> sprites =
            swchess::board::drawOrder(start, scene);
        checkInt(static_cast<int>(sprites.size()), 32,
                 std::string("pieces drawn in the ") + names[i] + " set");
        for (std::size_t s = 1; s < sprites.size(); ++s) {
            check(sprites[s - 1].depth >= sprites[s].depth,
                  std::string("the ") + names[i] + " draw order runs far to near");
        }
        swchess::Image canvas;
        swchess::board::renderPosition(start, scene, canvas);
        checkInt(canvas.width, scene.background.width,
                 std::string("the ") + names[i] + " canvas keeps the background width");
        std::string path = std::string("board_") + names[i] + ".ppm";
        swchess::writePPM(canvas, path);
        std::printf("wrote %s, %s over %s, %d pieces\n", path.c_str(),
                    swchess::board::setKey(sets[i]), scene.backgroundSource.c_str(),
                    static_cast<int>(sprites.size()));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: board_test <cd directory>\n");
        return 2;
    }
    std::string cdDir = argv[1];
    try {
        testSettings(cdDir);
        testTrigTables(cdDir);
        swchess::board::BoardSettings settings = swchess::board::loadBoardSettingsFromCd(cdDir);
        testGeometry(settings);
        swchess::board::BoardGeometry geometry = swchess::board::BoardGeometry::tilted(
            settings, swchess::board::Orientation::WhiteBottom);
        testHitTest(geometry);
        testSpriteRects(geometry);
        testPieceCells();
        testRender(cdDir);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
    if (failures > 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::printf("board_test passed\n");
    return 0;
}
