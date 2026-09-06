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

// Reads the BMP at `path`. Throws std::runtime_error when the file is missing
// or is not an uncompressed 8-bit BMP.
Image loadBmp(const std::string& path);

}  // namespace swchess
