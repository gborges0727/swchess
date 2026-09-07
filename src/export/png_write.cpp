#include "export/png_write.h"

#include <cstdio>
#include <stdexcept>

#include <zlib.h>

namespace swchess::exporter {
namespace {

int channelsFor(int colorType) {
    switch (colorType) {
        case kPngGray: return 1;
        case kPngRgb: return 3;
        case kPngRgba: return 4;
        default: throw std::runtime_error("unsupported PNG colour type");
    }
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void appendChunk(std::vector<std::uint8_t>& out, const char* tag,
                 const std::vector<std::uint8_t>& body) {
    appendU32(out, static_cast<std::uint32_t>(body.size()));
    std::size_t crcStart = out.size();
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(tag[i]));
    }
    out.insert(out.end(), body.begin(), body.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + crcStart, static_cast<uInt>(out.size() - crcStart));
    appendU32(out, static_cast<std::uint32_t>(crc));
}

}  // namespace

std::vector<std::uint8_t> encodePng(int width, int height, int colorType,
                                    const std::vector<std::uint8_t>& pixels, int level) {
    const std::size_t channels = static_cast<std::size_t>(channelsFor(colorType));
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    const std::size_t rows = static_cast<std::size_t>(height);
    if (pixels.size() != stride * rows) {
        throw std::runtime_error("the pixel buffer does not match the image size");
    }

    // Every row gets a leading filter byte of 0, which means "store the row".
    std::vector<std::uint8_t> raw;
    raw.reserve(rows * (stride + 1));
    for (std::size_t y = 0; y < rows; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(y * stride),
                   pixels.begin() + static_cast<std::ptrdiff_t>((y + 1) * stride));
    }

    uLongf capacity = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> deflated(capacity);
    int status = compress2(deflated.data(), &capacity, raw.data(),
                           static_cast<uLong>(raw.size()), level);
    if (status != Z_OK) {
        throw std::runtime_error("zlib refused the pixel stream");
    }
    deflated.resize(capacity);

    std::vector<std::uint8_t> header;
    appendU32(header, static_cast<std::uint32_t>(width));
    appendU32(header, static_cast<std::uint32_t>(height));
    header.push_back(8);
    header.push_back(static_cast<std::uint8_t>(colorType));
    header.push_back(0);
    header.push_back(0);
    header.push_back(0);

    std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    appendChunk(file, "IHDR", header);
    appendChunk(file, "IDAT", deflated);
    appendChunk(file, "IEND", {});
    return file;
}

void writePng(const std::string& path, int width, int height, int colorType,
              const std::vector<std::uint8_t>& pixels, int level) {
    std::vector<std::uint8_t> file = encodePng(width, height, colorType, pixels, level);
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) {
        throw std::runtime_error("cannot write " + path);
    }
    std::size_t written = std::fwrite(file.data(), 1, file.size(), out);
    std::fclose(out);
    if (written != file.size()) {
        throw std::runtime_error("short write on " + path);
    }
}

}  // namespace swchess::exporter
