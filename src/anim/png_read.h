// Reads the RGBA PNGs the interpolation tool writes.
//
// tools/interp/png.py writes 8-bit RGBA, non-interlaced, one IHDR, one or more
// IDAT chunks, no palette and no ancillary data this reader needs. So this
// decoder handles exactly that shape and refuses everything else. It inflates
// the pixel stream with zlib and undoes the five per-row filters PNG defines.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess::anim {

// One decoded picture. `pixels` holds width * height * 4 bytes, top row first,
// red green blue alpha in that order, with alpha kept straight.
struct PngImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Decodes the 8-bit RGBA PNG at `path`.
// Throws std::runtime_error when the file is missing, truncated, or is any
// other PNG colour type, bit depth, or interlace mode.
PngImage readPng(const std::string& path);

// The same decoder over bytes already in memory. `what` names the source in
// error messages.
PngImage decodePng(const std::vector<std::uint8_t>& bytes, const std::string& what);

}  // namespace swchess::anim
