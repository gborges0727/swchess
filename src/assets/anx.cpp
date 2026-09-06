#include "assets/anx.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>

namespace swchess {
namespace {

constexpr std::size_t kBase = 0x70c;
constexpr std::size_t kPaletteBytes = 1024;

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t at) {
    if (at + 4 > data.size()) {
        throw std::runtime_error("ANX read past end of file");
    }
    return static_cast<std::uint32_t>(data[at]) |
           (static_cast<std::uint32_t>(data[at + 1]) << 8) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16) |
           (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::int32_t readI32(const std::vector<std::uint8_t>& data, std::size_t at) {
    return static_cast<std::int32_t>(readU32(data, at));
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        throw std::runtime_error("cannot size " + path);
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty() && std::fread(data.data(), 1, data.size(), file) != data.size()) {
        std::fclose(file);
        throw std::runtime_error("short read on " + path);
    }
    std::fclose(file);
    return data;
}

// Decodes the record that starts at `start` and may run up to `end`.
AnxRecord decodeRecord(const std::vector<std::uint8_t>& data, std::size_t start, std::size_t end) {
    AnxRecord record;
    std::uint32_t headerSize = readU32(data, start + 0);
    record.width = readI32(data, start + 4);
    record.height = readI32(data, start + 8);
    std::uint32_t compression = readU32(data, start + 16);
    record.escape = static_cast<std::uint8_t>(compression >> 16);
    record.sizeImage = readU32(data, start + 20);
    std::uint16_t bitCount = static_cast<std::uint16_t>(readU32(data, start + 12) >> 16);
    std::uint32_t paletteUsed = readU32(data, start + 32);
    if (paletteUsed == 0 && bitCount <= 8) {
        paletteUsed = 1u << bitCount;
    }

    std::size_t paletteBytes = static_cast<std::size_t>(paletteUsed) * 4;
    std::size_t paletteAt = start + headerSize;
    if (paletteAt + paletteBytes > data.size()) {
        throw std::runtime_error("ANX palette runs past end of file");
    }
    record.palette.assign(data.begin() + static_cast<std::ptrdiff_t>(paletteAt),
                          data.begin() + static_cast<std::ptrdiff_t>(paletteAt + paletteBytes));

    std::size_t need = static_cast<std::size_t>(record.width) * static_cast<std::size_t>(record.height);
    record.indices.reserve(need);
    std::size_t at = paletteAt + paletteBytes;
    while (record.indices.size() < need && at < end) {
        std::uint8_t byte = data[at++];
        if (byte == record.escape) {
            // A run may read its two operand bytes past `end`, so bound the
            // read by the file instead. The Python reference does the same.
            if (at + 2 > data.size()) {
                break;
            }
            std::uint8_t value = data[at];
            std::uint8_t count = data[at + 1];
            at += 2;
            record.indices.insert(record.indices.end(), count, value);
        } else {
            record.indices.push_back(byte);
        }
    }
    record.complete = record.indices.size() == need;
    return record;
}

}  // namespace

AnxFile loadAnx(const std::string& path) {
    std::vector<std::uint8_t> data = readFile(path);
    if (data.size() < kBase) {
        throw std::runtime_error("ANX file shorter than 0x70c bytes: " + path);
    }

    AnxFile file;
    file.frameCount = readU32(data, 0);
    file.timeline.reserve(file.frameCount);
    for (std::uint32_t i = 0; i < file.frameCount; ++i) {
        file.timeline.push_back(readU32(data, 4 + 4 * static_cast<std::size_t>(i)));
    }

    // Each record ends where the next distinct record begins. The last one runs
    // to the end of the file.
    std::set<std::uint32_t> distinct(file.timeline.begin(), file.timeline.end());
    std::vector<std::uint32_t> sorted(distinct.begin(), distinct.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        std::size_t start = kBase + sorted[i];
        std::size_t end = (i + 1 < sorted.size()) ? kBase + sorted[i + 1] : data.size();
        AnxRecord record = decodeRecord(data, start, end);
        record.offset = sorted[i];
        file.records.emplace(sorted[i], std::move(record));
    }
    return file;
}

std::vector<std::uint8_t> anxToRGBA(const AnxRecord& record) {
    std::size_t width = static_cast<std::size_t>(record.width);
    std::size_t height = static_cast<std::size_t>(record.height);
    std::vector<std::uint8_t> rgba(width * height * 4, 0);
    if (record.palette.size() < kPaletteBytes) {
        return rgba;
    }
    for (std::size_t y = 0; y < height; ++y) {
        // The file stores the bottom row first, so flip while copying.
        const std::size_t sourceRow = height - 1 - y;
        for (std::size_t x = 0; x < width; ++x) {
            std::size_t sourceAt = sourceRow * width + x;
            if (sourceAt >= record.indices.size()) {
                continue;
            }
            std::uint8_t index = record.indices[sourceAt];
            const std::uint8_t* entry = &record.palette[static_cast<std::size_t>(index) * 4];
            std::uint8_t* out = &rgba[(y * width + x) * 4];
            out[0] = entry[2];  // red
            out[1] = entry[1];  // green
            out[2] = entry[0];  // blue
            out[3] = (index == 0) ? 0 : 255;
        }
    }
    return rgba;
}

}  // namespace swchess
