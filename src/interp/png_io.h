// Reads the 8-bit PNGs this pipeline hands to RIFE and reads back.
//
// tools/interp/png.py reads grayscale, RGB and RGBA at 8 bits with no
// interlacing, which covers the pose images the extractor writes and the
// pictures rife-ncnn-vulkan writes. This reader handles the same set and
// refuses everything else. The writer lives in src/export/png_write.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess::interp {

// One decoded picture. `pixels` holds height rows of width * channels bytes,
// top row first, with no padding.
struct Picture {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<std::uint8_t> pixels;
};

// Reads the PNG at `path`. Throws std::runtime_error when the file is
// missing, truncated, palette based, interlaced, or not 8 bits a channel.
Picture readPng(const std::string& path);

// Reads only the width and the height out of a PNG header.
void readPngSize(const std::string& path, int* width, int* height);

}  // namespace swchess::interp
