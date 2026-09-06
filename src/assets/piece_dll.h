// Reader for the twelve piece DLLs: AT BF C3 CB DV EM LO LS R2 SP ST YO.
//
// A piece DLL has no code segments. It is a resource container holding one
// bitmap per animation frame. Every bitmap resource starts with a
// BITMAPINFOHEADER, then a palette, then the same escape-byte RLE the ANX
// capture files use, so this reader calls decodeRleRecord from anx.h.
//
// A resource name such as AT_S002 names the piece, the walking direction and
// the frame number. Palette index 0 is the transparent color.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets/anx.h"

namespace swchess {

// The twelve piece codes, in the order the plan lists them.
extern const char* const kPieceCodes[12];

// One bitmap resource decoded out of a piece DLL.
struct PieceBitmap {
    std::string resourceName;   // "AT_S002", straight from the resource table
    std::size_t resourceOffset = 0;
    std::size_t resourceLength = 0;
    std::string direction;      // "S", "NE", "R" and so on, empty when the name does not parse
    int frame = -1;             // frame number from the name, -1 when it does not parse
    AnxRecord record;           // dimensions, palette and palette indices
};

// Every bitmap in one piece DLL, in resource table order.
struct PieceDll {
    std::string piece;   // "AT"
    std::string source;  // "AT.DLL"
    std::vector<PieceBitmap> bitmaps;
};

// Reads `dir`/`piece`.DLL and decodes every bitmap resource in it.
// Throws std::runtime_error when the file is missing or a bitmap decodes short.
PieceDll loadPieceDll(const std::string& dir, const std::string& piece);

// Converts one piece bitmap to top-down RGBA8. Palette index 0 becomes alpha 0.
std::vector<std::uint8_t> pieceToRGBA(const PieceBitmap& bitmap);

// Splits a resource name such as AT_S002 into its direction and frame number.
// Returns false when the name does not follow that shape.
bool parsePieceResourceName(const std::string& name, std::string* direction, int* frame);

}  // namespace swchess
