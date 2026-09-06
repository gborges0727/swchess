// Loader for the 8-bit Windows BMP files on the Star Wars Chess CD.
// SPACE256.BMP and THRON256.BMP are 640x480 backgrounds. The piece sheets use
// the same format at other sizes. Every one stores a 40-byte
// BITMAPINFOHEADER, a 256-entry BGRA palette, and uncompressed rows padded to
// four bytes, bottom row first.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess {

// One decoded image in top-down RGBA8, four bytes per pixel, alpha 255
// everywhere. Backgrounds stay opaque even where the palette index is 0.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// One BMP still in its palette form. The piece sheets need the palette index
// of each pixel, because index 0 is the transparent color and because the
// separator lines between cells are checked by index.
struct IndexedBitmap {
    int width = 0;
    int height = 0;
    bool topDown = false;             // a negative biHeight stores rows top row first
    std::size_t stride = 0;           // bytes per stored row, padded to four
    std::vector<std::uint8_t> palette;  // four bytes per entry, blue green red unused
    std::vector<std::uint8_t> pixels;   // stride * height bytes, in stored order

    // Returns the palette index at `x`, `y` counted from the top-left corner.
    std::uint8_t indexAt(int x, int y) const;
};

// Reads the BMP at `path` without expanding the palette. Throws
// std::runtime_error when the file is missing or is not an uncompressed 8-bit
// BMP.
IndexedBitmap loadBmpIndexed(const std::string& path);

// Copies the rectangle at `x`, `y` sized `width` by `height` into top-down
// RGBA8. Pixels whose palette index equals `transparentIndex` get alpha 0.
// Pass -1 to keep every pixel opaque. Throws when the rectangle leaves the
// bitmap.
std::vector<std::uint8_t> bmpRegionToRGBA(const IndexedBitmap& bitmap, int x, int y, int width,
                                          int height, int transparentIndex);

// Reads the BMP at `path`. Throws std::runtime_error when the file is missing
// or is not an uncompressed 8-bit BMP.
Image loadBmp(const std::string& path);

}  // namespace swchess
