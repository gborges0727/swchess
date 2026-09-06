#include "anim/png_read.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace swchess::anim {
namespace {

std::uint32_t readBE32(const std::uint8_t* at) {
    return (static_cast<std::uint32_t>(at[0]) << 24) | (static_cast<std::uint32_t>(at[1]) << 16) |
           (static_cast<std::uint32_t>(at[2]) << 8) | static_cast<std::uint32_t>(at[3]);
}

int paethPredictor(int left, int up, int upLeft) {
    const int estimate = left + up - upLeft;
    const int distLeft = estimate > left ? estimate - left : left - estimate;
    const int distUp = estimate > up ? estimate - up : up - estimate;
    const int distUpLeft = estimate > upLeft ? estimate - upLeft : upLeft - estimate;
    if (distLeft <= distUp && distLeft <= distUpLeft) {
        return left;
    }
    if (distUp <= distUpLeft) {
        return up;
    }
    return upLeft;
}

// Grows `out` until the whole deflate stream in `in` has been written into it.
void inflateAll(const std::vector<std::uint8_t>& in, std::size_t expected,
                std::vector<std::uint8_t>* out, const std::string& what) {
    out->assign(expected, 0);
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (inflateInit(&stream) != Z_OK) {
        throw std::runtime_error(what + ": zlib would not start");
    }
    stream.next_in = const_cast<Bytef*>(in.data());
    stream.avail_in = static_cast<uInt>(in.size());
    stream.next_out = out->data();
    stream.avail_out = static_cast<uInt>(out->size());
    const int status = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        throw std::runtime_error(what + ": the pixel stream did not inflate");
    }
    if (produced != expected) {
        throw std::runtime_error(what + ": the pixel stream is the wrong length");
    }
}

}  // namespace

PngImage decodePng(const std::vector<std::uint8_t>& bytes, const std::string& what) {
    static const std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0) {
        throw std::runtime_error(what + ": not a PNG");
    }

    PngImage image;
    std::vector<std::uint8_t> deflated;
    bool sawHeader = false;
    std::size_t at = 8;
    while (at + 8 <= bytes.size()) {
        const std::uint32_t length = readBE32(&bytes[at]);
        const char* type = reinterpret_cast<const char*>(&bytes[at + 4]);
        const std::size_t body = at + 8;
        if (body + length + 4 > bytes.size()) {
            throw std::runtime_error(what + ": a chunk runs past the end of the file");
        }
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (length != 13) {
                throw std::runtime_error(what + ": IHDR is the wrong size");
            }
            image.width = static_cast<int>(readBE32(&bytes[body]));
            image.height = static_cast<int>(readBE32(&bytes[body + 4]));
            const int depth = bytes[body + 8];
            const int colourType = bytes[body + 9];
            const int compression = bytes[body + 10];
            const int filter = bytes[body + 11];
            const int interlace = bytes[body + 12];
            if (depth != 8 || colourType != 6) {
                throw std::runtime_error(what + ": only 8-bit RGBA is supported");
            }
            if (compression != 0 || filter != 0 || interlace != 0) {
                throw std::runtime_error(what + ": only the standard non-interlaced form is supported");
            }
            if (image.width <= 0 || image.height <= 0) {
                throw std::runtime_error(what + ": the picture has no pixels");
            }
            sawHeader = true;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            if (!sawHeader) {
                throw std::runtime_error(what + ": IDAT comes before IHDR");
            }
            deflated.insert(deflated.end(), bytes.begin() + static_cast<std::ptrdiff_t>(body),
                            bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at = body + length + 4;
    }
    if (!sawHeader) {
        throw std::runtime_error(what + ": the file has no IHDR");
    }
    if (deflated.empty()) {
        throw std::runtime_error(what + ": the file has no IDAT");
    }

    const std::size_t stride = static_cast<std::size_t>(image.width) * 4;
    const std::size_t rows = static_cast<std::size_t>(image.height);
    std::vector<std::uint8_t> raw;
    inflateAll(deflated, (stride + 1) * rows, &raw, what);

    image.pixels.assign(stride * rows, 0);
    for (std::size_t y = 0; y < rows; ++y) {
        const std::uint8_t filter = raw[y * (stride + 1)];
        const std::uint8_t* in = &raw[y * (stride + 1) + 1];
        std::uint8_t* out = &image.pixels[y * stride];
        const std::uint8_t* above = y == 0 ? nullptr : &image.pixels[(y - 1) * stride];
        for (std::size_t x = 0; x < stride; ++x) {
            const int left = x >= 4 ? out[x - 4] : 0;
            const int up = above != nullptr ? above[x] : 0;
            const int upLeft = (above != nullptr && x >= 4) ? above[x - 4] : 0;
            int value = in[x];
            switch (filter) {
                case 0:
                    break;
                case 1:
                    value += left;
                    break;
                case 2:
                    value += up;
                    break;
                case 3:
                    value += (left + up) / 2;
                    break;
                case 4:
                    value += paethPredictor(left, up, upLeft);
                    break;
                default:
                    throw std::runtime_error(what + ": unknown row filter");
            }
            out[x] = static_cast<std::uint8_t>(value & 0xff);
        }
    }
    return image;
}

PngImage readPng(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t chunk[64 * 1024];
    while (true) {
        const std::size_t got = std::fread(chunk, 1, sizeof(chunk), file);
        if (got == 0) {
            break;
        }
        bytes.insert(bytes.end(), chunk, chunk + got);
    }
    std::fclose(file);
    return decodePng(bytes, path);
}

}  // namespace swchess::anim
