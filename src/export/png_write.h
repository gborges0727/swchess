// Writes the 8-bit PNG files the asset cache holds.
//
// tools/extract/png.py and tools/interp/png.py both write one IHDR, one IDAT
// and one IEND, with filter type 0 on every row and zlib at the level the
// caller picks. zlib.compress(data, level) and zlib's own compress2 produce
// the same bytes at the same level, so a file written here matches the Python
// file byte for byte.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess::exporter {

// PNG colour types. The two tools use these three.
constexpr int kPngGray = 0;
constexpr int kPngRgb = 2;
constexpr int kPngRgba = 6;

// Encodes one image and returns the whole file. `pixels` runs top row first
// with no padding between rows, width * channels bytes per row.
std::vector<std::uint8_t> encodePng(int width, int height, int colorType,
                                    const std::vector<std::uint8_t>& pixels, int level);

// Encodes the image and writes it to `path`. Throws std::runtime_error when
// the file cannot be written.
void writePng(const std::string& path, int width, int height, int colorType,
              const std::vector<std::uint8_t>& pixels, int level = 6);

}  // namespace swchess::exporter
