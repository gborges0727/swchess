#include "assets/bmp.h"

#include <cstdio>
#include <stdexcept>

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

Image loadBmp(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size < 0 ? 0 : size));
    if (!data.empty() && std::fread(data.data(), 1, data.size(), file) != data.size()) {
        std::fclose(file);
        throw std::runtime_error("short read on " + path);
    }
    std::fclose(file);

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

    bool topDown = rawHeight < 0;
    std::int32_t height = topDown ? -rawHeight : rawHeight;
    if (width <= 0 || height <= 0) {
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

    std::size_t stride = (static_cast<std::size_t>(width) + 3u) & ~std::size_t(3u);
    if (pixelsAt + stride * static_cast<std::size_t>(height) > data.size()) {
        throw std::runtime_error("BMP pixel data runs past end of file: " + path);
    }

    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 255);
    for (std::int32_t y = 0; y < height; ++y) {
        std::size_t sourceRow = topDown ? static_cast<std::size_t>(y)
                                        : static_cast<std::size_t>(height - 1 - y);
        const std::uint8_t* row = &data[pixelsAt + sourceRow * stride];
        for (std::int32_t x = 0; x < width; ++x) {
            const std::uint8_t* entry = &data[paletteAt + static_cast<std::size_t>(row[x]) * 4];
            std::uint8_t* out = &image.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            out[0] = entry[2];
            out[1] = entry[1];
            out[2] = entry[0];
            out[3] = 255;
        }
    }
    return image;
}

}  // namespace swchess
