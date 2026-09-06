#include "render/compositor.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace swchess {

Placement centerOn(const Image& canvas, int spriteWidth, int spriteHeight) {
    Placement place;
    place.x = (canvas.width - spriteWidth) / 2;
    place.y = (canvas.height - spriteHeight) / 2;
    return place;
}

void blitRGBA(Image& canvas, const std::uint8_t* rgba, int width, int height, int x, int y) {
    for (int row = 0; row < height; ++row) {
        int destY = y + row;
        if (destY < 0 || destY >= canvas.height) {
            continue;
        }
        for (int col = 0; col < width; ++col) {
            int destX = x + col;
            if (destX < 0 || destX >= canvas.width) {
                continue;
            }
            const std::uint8_t* source = &rgba[(static_cast<std::size_t>(row) * width + col) * 4];
            std::uint8_t alpha = source[3];
            if (alpha == 0) {
                continue;
            }
            std::uint8_t* dest =
                &canvas.rgba[(static_cast<std::size_t>(destY) * canvas.width + destX) * 4];
            for (int channel = 0; channel < 3; ++channel) {
                int mixed = source[channel] * alpha + dest[channel] * (255 - alpha);
                dest[channel] = static_cast<std::uint8_t>((mixed + 127) / 255);
            }
            dest[3] = 255;
        }
    }
}

void writePPM(const Image& canvas, const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot write " + path);
    }
    std::fprintf(file, "P6\n%d %d\n255\n", canvas.width, canvas.height);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(canvas.width) * 3);
    for (int y = 0; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            const std::uint8_t* pixel =
                &canvas.rgba[(static_cast<std::size_t>(y) * canvas.width + x) * 4];
            row[static_cast<std::size_t>(x) * 3 + 0] = pixel[0];
            row[static_cast<std::size_t>(x) * 3 + 1] = pixel[1];
            row[static_cast<std::size_t>(x) * 3 + 2] = pixel[2];
        }
        std::fwrite(row.data(), 1, row.size(), file);
    }
    std::fclose(file);
}

}  // namespace swchess
