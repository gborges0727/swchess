// Board geometry for the Star Wars Chess native port.
//
// The Windows front end projects a chess square onto the screen with 16-bit
// fixed point arithmetic. docs/research/board-geometry.md records the
// functions behind every step. This header reproduces that arithmetic, so
// squareCenter() and corner() return the same pixels the original computed.
//
// The numbers the game runs on live in CMWIN.DAT, a 144-byte file next to
// XCHESS.EXE. Read them with loadBoardSettings() instead of writing them into
// the code.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess::board {

// The board variables CMWIN.DAT writes over the data block at 11d8:67ff.
// Field names follow the meanings in the research doc, and the defaults are
// the shipped values.
struct BoardSettings {
    int bitmapStyle = 2;  // 2 picks the marble centers, 1 the wood ones, 0 the window
    int vanish3D = 335;
    int size3D = 535;
    int turn3D = 360;
    int tilt3D = 307;
    int wood3DCenterX = 319;
    int wood3DCenterY = 226;
    int wood2DCenterX = 275;
    int wood2DCenterY = 219;
    int marble3DCenterX = 318;
    int marble3DCenterY = 256;
    int marble2DCenterX = 275;
    int marble2DCenterY = 217;
    int size2D = 394;
    int turn2D = 360;
    // The four live values the projection reads.
    int vanish = 335;
    int size = 535;
    int turn = 360;
    int tilt = 307;
    int chessSet = 0;   // index into the CM.INI [chesssets] records
    int background = 1;  // 0 is the throne room, anything else is space
};

// Decodes the 144 bytes of CMWIN.DAT. Throws std::runtime_error when the
// buffer is not exactly 144 bytes long, the same length check FUN_1008_37bc
// makes.
BoardSettings parseBoardSettings(const std::vector<std::uint8_t>& bytes);

// Reads one CMWIN.DAT file. `path` names the file itself, not its directory.
BoardSettings loadBoardSettings(const std::string& path);

// Reads CMWIN.DAT out of the CD directory.
BoardSettings loadBoardSettingsFromCd(const std::string& cdDir);

// The two turn angles the game uses. A turn that is a multiple of 360 leaves
// white at the bottom and picks the WHTBTM_ sheet. Any other turn puts white
// at the top and picks WHTTOP_.
enum class Orientation : std::uint8_t { WhiteBottom, WhiteTop };

// A pixel on the 640x480 screen.
struct ScreenPoint {
    int x = 0;
    int y = 0;

    friend bool operator==(const ScreenPoint&, const ScreenPoint&) = default;
};

// A projected point with the distance the draw order sorts on. Smaller depth
// means nearer the viewer.
struct ProjectedPoint {
    int x = 0;
    int y = 0;
    int depth = 0;

    friend bool operator==(const ProjectedPoint&, const ProjectedPoint&) = default;
};

// The sine and cosine tables lifted from XCHESS.EXE at file offsets 0x35164
// and 0x35704. Each holds 360 signed longs equal to the function times 32767,
// indexed by whole degrees. Three entries differ from the rounded values a
// libm call produces, so the tables are copied rather than recomputed.
const std::int32_t* sineTable();
const std::int32_t* cosineTable();

// The fixed point scale. 1.0 is 32767.
constexpr std::int32_t kFixedOne = 32767;

// One board layout. It projects model coordinates the way FUN_1028_0107 does:
// rotate about Z by the turn angle, rotate about X by the tilt angle,
// translate away from the eye, then divide by perspective.
class BoardGeometry {
public:
    BoardGeometry() = default;

    // The tilted board the three 3D sets stand on.
    static BoardGeometry tilted(const BoardSettings& settings, Orientation orientation);

    // The flat board the 2D set stands on. Its tilt is 360, which
    // FUN_1028_00e8 reads as flat, and it uses size_2D and turn_2D.
    static BoardGeometry flat(const BoardSettings& settings, Orientation orientation);

    bool isFlat() const { return tilt_ % 360 == 0; }
    int size() const { return size_; }
    int turn() const { return turn_; }
    int tilt() const { return tilt_; }
    int vanishingPoint() const { return vanish_; }
    ScreenPoint center() const { return ScreenPoint{centerX_, centerY_}; }

    // One square is this many model units wide.
    int step() const { return step_; }

    // The nine grid line coordinates, -532 to 532 for the shipped size.
    int grid(int index) const;

    // Projects one model point on the board plane and adds the board center.
    ProjectedPoint project(int modelX, int modelY) const;

    // One corner of the nine by nine grid. Row 0 is the rank 8 edge and
    // column 0 is the a file edge, the indexing GetSquareOrigin uses.
    ProjectedPoint corner(int row, int col) const;

    // The center of one square. File 0 is the a file and rank 0 is rank 1, the
    // indexing swchess::chess::Square uses.
    ProjectedPoint squareCenter(int file, int rank) const;

    // The same center with the other orientation. The turn angle is the whole
    // mechanism, so this rebuilds the layout with that angle and projects the
    // same model point.
    ProjectedPoint squareCenter(int file, int rank, Orientation orientation) const;
    ProjectedPoint corner(int row, int col, Orientation orientation) const;

private:
    static BoardGeometry build(const BoardSettings& settings, Orientation orientation, bool flat);

    BoardSettings settings_{};
    bool flat_ = false;
    int vanish_ = 0;
    int size_ = 0;
    int turn_ = 0;
    int tilt_ = 0;
    int step_ = 0;
    int centerX_ = 0;
    int centerY_ = 0;
};

// Turns a file and a rank into the row and column GetSquareOrigin takes.
// Row 0 is rank 8 and column 0 is file a.
constexpr int rowForRank(int rank) { return 7 - rank; }
constexpr int rankForRow(int row) { return 7 - row; }

}  // namespace swchess::board
