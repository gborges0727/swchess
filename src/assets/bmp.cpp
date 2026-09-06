#include "assets/bmp.h"

#include <stdexcept>

#include "assets/ne.h"

namespace swchess {
namespace {

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) |
           (static_cast<std::uint32_t>(data[at + 1]) << 8) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16) |
           (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

}  // namespace

std::uint8_t IndexedBitmap::indexAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        throw std::runtime_error("BMP pixel read outside the bitmap");
    }
    std::size_t row = topDown ? static_cast<std::size_t>(y)
                              : static_cast<std::size_t>(height - 1 - y);
    return pixels[row * stride + static_cast<std::size_t>(x)];
}

IndexedBitmap loadBmpIndexed(const std::string& path) {
    std::vector<std::uint8_t> data = readBinaryFile(path);
    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') {
        throw std::runtime_error("not a BMP file: " + path);
    }
    std::uint32_t pixelsAt = readU32(data, 10);
    std::uint32_t headerSize = readU32(data, 14);
    if (headerSize < 40) {
        throw std::runtime_error("unsupported BMP header size in " + path);
    }
    std::int32_t width = static_cast<std::int32_t>(readU32(data, 18));
    std::int32_t rawHeight = static_cast<std::int32_t>(readU32(data, 22));
    std::uint16_t bitCount = readU16(data, 28);
    std::uint32_t compression = readU32(data, 30);
    if (bitCount != 8 || compression != 0) {
        throw std::runtime_error("expected an uncompressed 8-bit BMP: " + path);
    }

    IndexedBitmap bitmap;
    bitmap.topDown = rawHeight < 0;
    bitmap.width = width;
    bitmap.height = bitmap.topDown ? -rawHeight : rawHeight;
    if (width <= 0 || bitmap.height <= 0) {
        throw std::runtime_error("bad BMP dimensions in " + path);
    }

    std::size_t paletteAt = 14 + headerSize;
    std::uint32_t paletteUsed = readU32(data, 46);
    if (paletteUsed == 0) {
        paletteUsed = 256;
    }
    if (paletteAt + static_cast<std::size_t>(paletteUsed) * 4 > data.size()) {
        throw std::runtime_error("BMP palette runs past end of file: " + path);
    }
    bitmap.palette.assign(
        data.begin() + static_cast<std::ptrdiff_t>(paletteAt),
        data.begin() + static_cast<std::ptrdiff_t>(paletteAt + paletteUsed * 4));

    bitmap.stride = (static_cast<std::size_t>(width) + 3u) & ~std::size_t(3u);
    std::size_t need = bitmap.stride * static_cast<std::size_t>(bitmap.height);
    if (pixelsAt + need > data.size()) {
        throw std::runtime_error("BMP pixel data runs past end of file: " + path);
    }
    bitmap.pixels.assign(data.begin() + static_cast<std::ptrdiff_t>(pixelsAt),
                         data.begin() + static_cast<std::ptrdiff_t>(pixelsAt + need));
    return bitmap;
}

std::vector<std::uint8_t> bmpRegionToRGBA(const IndexedBitmap& bitmap, int x, int y, int width,
                                          int height, int transparentIndex) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > bitmap.width ||
        y + height > bitmap.height) {
        throw std::runtime_error("BMP region leaves the bitmap");
    }
    std::size_t entries = bitmap.palette.size() / 4;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height) * 4);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            std::uint8_t index = bitmap.indexAt(x + column, y + row);
            std::uint8_t* out =
                &rgba[(static_cast<std::size_t>(row) * width + column) * 4];
            if (static_cast<std::size_t>(index) < entries) {
                const std::uint8_t* entry = &bitmap.palette[static_cast<std::size_t>(index) * 4];
                out[0] = entry[2];  // red
                out[1] = entry[1];  // green
                out[2] = entry[0];  // blue
            } else {
                out[0] = 0;
                out[1] = 0;
                out[2] = 0;
            }
            out[3] = (transparentIndex >= 0 && index == transparentIndex) ? 0 : 255;
        }
    }
    return rgba;
}

Image loadBmp(const std::string& path) {
    IndexedBitmap bitmap = loadBmpIndexed(path);
    Image image;
    image.width = bitmap.width;
    image.height = bitmap.height;
    image.rgba = bmpRegionToRGBA(bitmap, 0, 0, bitmap.width, bitmap.height, -1);
    return image;
}

}  // namespace swchess
