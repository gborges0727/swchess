#include "interp/png_io.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

namespace swchess::interp {
namespace {

const std::uint8_t kMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

std::vector<std::uint8_t> readAll(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<std::uint8_t> blob(static_cast<std::size_t>(size < 0 ? 0 : size));
    if (!blob.empty() && std::fread(blob.data(), 1, blob.size(), file) != blob.size()) {
        std::fclose(file);
        throw std::runtime_error("short read on " + path);
    }
    std::fclose(file);
    return blob;
}

std::uint32_t readU32(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 4 > blob.size()) {
        throw std::runtime_error("a PNG chunk runs past the end of the file");
    }
    return (static_cast<std::uint32_t>(blob[at]) << 24) |
           (static_cast<std::uint32_t>(blob[at + 1]) << 16) |
           (static_cast<std::uint32_t>(blob[at + 2]) << 8) |
           static_cast<std::uint32_t>(blob[at + 3]);
}

int channelsFor(int colorType) {
    switch (colorType) {
        case 0: return 1;
        case 2: return 3;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

// Undoes the five per-row filters PNG defines.
void unfilter(const std::vector<std::uint8_t>& raw, int width, int height, int channels,
              std::vector<std::uint8_t>* out, const std::string& what) {
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    out->assign(stride * static_cast<std::size_t>(height), 0);
    std::vector<std::uint8_t> previous(stride, 0);
    std::size_t at = 0;
    for (int y = 0; y < height; ++y) {
        if (at + 1 + stride > raw.size()) {
            throw std::runtime_error("the pixel stream in " + what + " stops early");
        }
        const std::uint8_t filter = raw[at];
        std::uint8_t* line = out->data() + static_cast<std::size_t>(y) * stride;
        std::memcpy(line, raw.data() + at + 1, stride);
        at += 1 + stride;
        switch (filter) {
            case 0:
                break;
            case 1:
                for (std::size_t i = channels; i < stride; ++i) {
                    line[i] = static_cast<std::uint8_t>(line[i] + line[i - channels]);
                }
                break;
            case 2:
                for (std::size_t i = 0; i < stride; ++i) {
                    line[i] = static_cast<std::uint8_t>(line[i] + previous[i]);
                }
                break;
            case 3:
                for (std::size_t i = 0; i < stride; ++i) {
                    const int left = i >= static_cast<std::size_t>(channels)
                                         ? line[i - channels]
                                         : 0;
                    line[i] = static_cast<std::uint8_t>(line[i] + ((left + previous[i]) >> 1));
                }
                break;
            case 4:
                for (std::size_t i = 0; i < stride; ++i) {
                    const int a = i >= static_cast<std::size_t>(channels) ? line[i - channels] : 0;
                    const int b = previous[i];
                    const int c = i >= static_cast<std::size_t>(channels)
                                      ? previous[i - channels]
                                      : 0;
                    const int p = a + b - c;
                    const int pa = p > a ? p - a : a - p;
                    const int pb = p > b ? p - b : b - p;
                    const int pc = p > c ? p - c : c - p;
                    int predicted = c;
                    if (pa <= pb && pa <= pc) {
                        predicted = a;
                    } else if (pb <= pc) {
                        predicted = b;
                    }
                    line[i] = static_cast<std::uint8_t>(line[i] + predicted);
                }
                break;
            default:
                throw std::runtime_error("unknown PNG filter in " + what);
        }
        std::memcpy(previous.data(), line, stride);
    }
}

}  // namespace

Picture readPng(const std::string& path) {
    std::vector<std::uint8_t> blob = readAll(path);
    if (blob.size() < 8 || std::memcmp(blob.data(), kMagic, 8) != 0) {
        throw std::runtime_error("not a PNG: " + path);
    }
    Picture picture;
    int colorType = -1;
    std::vector<std::uint8_t> deflated;
    std::size_t at = 8;
    while (at + 8 <= blob.size()) {
        const std::uint32_t length = readU32(blob, at);
        const char* tag = reinterpret_cast<const char*>(blob.data() + at + 4);
        const std::size_t body = at + 8;
        if (body + length > blob.size()) {
            throw std::runtime_error("a PNG chunk runs past the end of " + path);
        }
        if (std::memcmp(tag, "IHDR", 4) == 0) {
            picture.width = static_cast<int>(readU32(blob, body));
            picture.height = static_cast<int>(readU32(blob, body + 4));
            if (blob[body + 8] != 8) {
                throw std::runtime_error("expected 8 bits a channel in " + path);
            }
            colorType = blob[body + 9];
            if (blob[body + 12] != 0) {
                throw std::runtime_error("expected a non interlaced PNG in " + path);
            }
        } else if (std::memcmp(tag, "IDAT", 4) == 0) {
            deflated.insert(deflated.end(), blob.begin() + static_cast<std::ptrdiff_t>(body),
                            blob.begin() + static_cast<std::ptrdiff_t>(body + length));
        } else if (std::memcmp(tag, "IEND", 4) == 0) {
            break;
        }
        at = body + length + 4;
    }
    picture.channels = channelsFor(colorType);
    if (picture.channels == 0) {
        throw std::runtime_error("unsupported PNG colour type in " + path);
    }

    const std::size_t stride = static_cast<std::size_t>(picture.width) * picture.channels;
    std::vector<std::uint8_t> raw((stride + 1) * static_cast<std::size_t>(picture.height));
    uLongf size = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &size, deflated.data(),
                   static_cast<uLong>(deflated.size())) != Z_OK) {
        throw std::runtime_error("cannot inflate the pixel stream in " + path);
    }
    raw.resize(size);
    unfilter(raw, picture.width, picture.height, picture.channels, &picture.pixels, path);
    return picture;
}

void readPngSize(const std::string& path, int* width, int* height) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::uint8_t head[24];
    const std::size_t got = std::fread(head, 1, sizeof(head), file);
    std::fclose(file);
    if (got < sizeof(head) || std::memcmp(head, kMagic, 8) != 0) {
        throw std::runtime_error("not a PNG: " + path);
    }
    *width = (head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
    *height = (head[20] << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
}

}  // namespace swchess::interp
