#include "board/geometry.h"

#include <cstdio>
#include <stdexcept>

namespace swchess::board {
namespace {

#include "board/trig_tables.inc"

// The C integer division the original runs on. Every quotient truncates
// toward zero, which matters because the projection divides negative
// coordinates.
std::int64_t truncDiv(std::int64_t a, std::int64_t b) {
    return a / b;
}

// Reads one little endian signed 16-bit value.
int readWord(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    int value = static_cast<int>(bytes[offset]) | (static_cast<int>(bytes[offset + 1]) << 8);
    if (value >= 0x8000) {
        value -= 0x10000;
    }
    return value;
}

// The angle index into the tables. The game stores 360 for no rotation.
int angleIndex(int degrees) {
    int index = degrees % 360;
    if (index < 0) {
        index += 360;
    }
    return index;
}

}  // namespace

const std::int32_t* sineTable() { return kSine; }
const std::int32_t* cosineTable() { return kCosine; }

BoardSettings parseBoardSettings(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != 144) {
        throw std::runtime_error("CMWIN.DAT must be 144 bytes long");
    }
    BoardSettings settings;
    settings.bitmapStyle = bytes[0x10];
    settings.vanish3D = readWord(bytes, 0x12);
    settings.size3D = readWord(bytes, 0x14);
    settings.turn3D = readWord(bytes, 0x16);
    settings.tilt3D = readWord(bytes, 0x18);
    settings.wood3DCenterX = readWord(bytes, 0x1a);
    settings.wood3DCenterY = readWord(bytes, 0x1c);
    settings.wood2DCenterX = readWord(bytes, 0x1e);
    settings.wood2DCenterY = readWord(bytes, 0x20);
    settings.marble3DCenterX = readWord(bytes, 0x22);
    settings.marble3DCenterY = readWord(bytes, 0x24);
    settings.marble2DCenterX = readWord(bytes, 0x26);
    settings.marble2DCenterY = readWord(bytes, 0x28);
    settings.size2D = readWord(bytes, 0x2a);
    settings.turn2D = readWord(bytes, 0x2c);
    settings.vanish = readWord(bytes, 0x2e);
    settings.size = readWord(bytes, 0x30);
    settings.turn = readWord(bytes, 0x32);
    settings.tilt = readWord(bytes, 0x34);
    settings.chessSet = readWord(bytes, 0x42);
    settings.background = readWord(bytes, 0x84);
    return settings;
}

BoardSettings loadBoardSettings(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::vector<std::uint8_t> bytes(145);
    std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    bytes.resize(read);
    return parseBoardSettings(bytes);
}

BoardSettings loadBoardSettingsFromCd(const std::string& cdDir) {
    return loadBoardSettings(cdDir + "/CMWIN.DAT");
}

BoardGeometry BoardGeometry::build(const BoardSettings& settings, Orientation orientation,
                                   bool flat) {
    BoardGeometry geometry;
    geometry.settings_ = settings;
    geometry.flat_ = flat;
    geometry.size_ = flat ? settings.size2D : settings.size;
    int baseTurn = flat ? settings.turn2D : settings.turn;
    // Turning the board by half a circle is what puts white at the top.
    geometry.turn_ = orientation == Orientation::WhiteTop ? baseTurn + 180 : baseTurn;
    // A flat board is one whose tilt angle is a whole number of turns, which
    // is what FUN_1028_00e8 tests for.
    geometry.tilt_ = flat ? 360 : settings.tilt;
    // FUN_1028_0107 takes vanishpt_3D for a styled tilted board and the live
    // value otherwise. Both hold 335 in the shipped file.
    geometry.vanish_ =
        (settings.bitmapStyle != 0 && !flat) ? settings.vanish3D : settings.vanish;
    geometry.step_ = geometry.size_ / 4;

    // FUN_1028_095d mode 2 picks the board center.
    if (settings.bitmapStyle == 2) {
        geometry.centerX_ = flat ? settings.marble2DCenterX : settings.marble3DCenterX;
        geometry.centerY_ = flat ? settings.marble2DCenterY : settings.marble3DCenterY;
    } else if (settings.bitmapStyle == 1) {
        geometry.centerX_ = flat ? settings.wood2DCenterX : settings.wood3DCenterX;
        geometry.centerY_ = flat ? settings.wood2DCenterY : settings.wood3DCenterY;
    } else {
        // Style 0 centers the board inside the 640 by 480 window.
        geometry.centerX_ = 320;
        geometry.centerY_ = 240;
    }
    return geometry;
}

BoardGeometry BoardGeometry::tilted(const BoardSettings& settings, Orientation orientation) {
    return build(settings, orientation, false);
}

BoardGeometry BoardGeometry::flat(const BoardSettings& settings, Orientation orientation) {
    return build(settings, orientation, true);
}

int BoardGeometry::grid(int index) const {
    if (index < 0 || index > 8) {
        throw std::runtime_error("grid index out of range");
    }
    return step_ * (index - 4);
}

ProjectedPoint BoardGeometry::project(int modelX, int modelY) const {
    const std::int32_t* sine = sineTable();
    const std::int32_t* cosine = cosineTable();

    // Rotate about Z by the turn angle.
    int turnIndex = angleIndex(turn_);
    std::int64_t sinTurn = sine[turnIndex];
    std::int64_t cosTurn = cosine[turnIndex];
    std::int64_t x = truncDiv(modelX * cosTurn - modelY * sinTurn, kFixedOne);
    std::int64_t y = truncDiv(modelX * sinTurn + modelY * cosTurn, kFixedOne);
    std::int64_t z = 0;

    // Rotate about X by the tilt angle.
    int tiltIndex = angleIndex(tilt_);
    std::int64_t sinTilt = sine[tiltIndex];
    std::int64_t cosTilt = cosine[tiltIndex];
    std::int64_t rotatedY = truncDiv(y * cosTilt + z * sinTilt, kFixedOne);
    std::int64_t rotatedZ = truncDiv(-y * sinTilt + z * cosTilt, kFixedOne);

    // Push the board away from the eye by grid[8] * vanishpt / 100.
    std::int64_t distance = truncDiv(static_cast<std::int64_t>(step_) * 4 * vanish_, 100);
    if (distance == 0) {
        throw std::runtime_error("the projection distance is zero");
    }
    rotatedZ -= distance;

    ProjectedPoint out;
    out.depth = static_cast<int>(rotatedZ < 0 ? -rotatedZ : rotatedZ);

    // Divide by perspective. FUN_1038_0626 forms the divisor first, then
    // scales every component by 32767 and divides.
    std::int64_t divisor = kFixedOne + truncDiv(-static_cast<std::int64_t>(kFixedOne) * rotatedZ,
                                                distance);
    if (divisor == 0) {
        throw std::runtime_error("the perspective divisor is zero");
    }
    out.x = centerX_ + static_cast<int>(truncDiv(x * kFixedOne, divisor));
    out.y = centerY_ + static_cast<int>(truncDiv(rotatedY * kFixedOne, divisor));
    return out;
}

ProjectedPoint BoardGeometry::corner(int row, int col) const {
    if (row < 0 || row > 8 || col < 0 || col > 8) {
        throw std::runtime_error("corner index out of range");
    }
    return project(grid(col), grid(row));
}

ProjectedPoint BoardGeometry::squareCenter(int file, int rank) const {
    if (file < 0 || file > 7 || rank < 0 || rank > 7) {
        throw std::runtime_error("square out of range");
    }
    int row = rowForRank(rank);
    // GetPieceOrigin adds half a square to both model coordinates.
    return project(grid(file) + step_ / 2, grid(row) + step_ / 2);
}

ProjectedPoint BoardGeometry::squareCenter(int file, int rank, Orientation orientation) const {
    BoardGeometry turned = build(settings_, orientation, flat_);
    return turned.squareCenter(file, rank);
}

ProjectedPoint BoardGeometry::corner(int row, int col, Orientation orientation) const {
    BoardGeometry turned = build(settings_, orientation, flat_);
    return turned.corner(row, col);
}

}  // namespace swchess::board
