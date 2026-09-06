#include "assets/ne.h"

#include <cstdio>
#include <stdexcept>

namespace swchess {
namespace {

std::uint16_t readU16(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 2 > blob.size()) {
        throw std::runtime_error("NE read past end of file");
    }
    return static_cast<std::uint16_t>(blob[at] | (blob[at + 1] << 8));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 4 > blob.size()) {
        throw std::runtime_error("NE read past end of file");
    }
    return static_cast<std::uint32_t>(blob[at]) |
           (static_cast<std::uint32_t>(blob[at + 1]) << 8) |
           (static_cast<std::uint32_t>(blob[at + 2]) << 16) |
           (static_cast<std::uint32_t>(blob[at + 3]) << 24);
}

// Turns one 16-bit table value into a number or into the Pascal string it
// points at. Bit 15 marks a number. Everything else is a byte offset counted
// from the start of the resource table.
NeId resolveId(const std::vector<std::uint8_t>& blob, std::size_t table, std::uint16_t value) {
    NeId out;
    if ((value & 0x8000u) != 0) {
        out.isNumeric = true;
        out.id = value & 0x7FFFu;
        return out;
    }
    std::size_t start = table + value;
    if (start >= blob.size()) {
        throw std::runtime_error("NE resource name runs past end of file");
    }
    std::size_t length = blob[start];
    if (start + 1 + length > blob.size()) {
        throw std::runtime_error("NE resource name runs past end of file");
    }
    out.name.assign(reinterpret_cast<const char*>(&blob[start + 1]), length);
    return out;
}

}  // namespace

std::string NeId::text() const {
    if (isNumeric) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%d", id);
        return std::string(buffer);
    }
    return name;
}

std::vector<std::uint8_t> readBinaryFile(const std::string& path) {
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
    std::vector<std::uint8_t> blob(static_cast<std::size_t>(size));
    if (!blob.empty() && std::fread(blob.data(), 1, blob.size(), file) != blob.size()) {
        std::fclose(file);
        throw std::runtime_error("short read on " + path);
    }
    std::fclose(file);
    return blob;
}

std::vector<NeResource> readNeResources(const std::vector<std::uint8_t>& blob) {
    if (blob.size() < 0x40 || blob[0] != 'M' || blob[1] != 'Z') {
        throw std::runtime_error("not an MZ image");
    }
    std::size_t neAt = readU32(blob, 0x3C);
    if (neAt + 2 > blob.size() || blob[neAt] != 'N' || blob[neAt + 1] != 'E') {
        throw std::runtime_error("no NE header");
    }
    std::size_t table = neAt + readU16(blob, neAt + 0x24);
    std::uint16_t shift = readU16(blob, table);

    std::vector<NeResource> out;
    std::size_t pos = table + 2;
    while (true) {
        std::uint16_t typeValue = readU16(blob, pos);
        if (typeValue == 0) {
            break;
        }
        std::uint16_t count = readU16(blob, pos + 2);
        pos += 8;
        for (std::uint16_t i = 0; i < count; ++i) {
            NeResource resource;
            std::uint16_t offset = readU16(blob, pos);
            std::uint16_t length = readU16(blob, pos + 2);
            resource.flags = readU16(blob, pos + 4);
            std::uint16_t nameValue = readU16(blob, pos + 6);
            pos += 12;
            resource.type = resolveId(blob, table, typeValue);
            resource.name = resolveId(blob, table, nameValue);
            resource.offset = static_cast<std::size_t>(offset) << shift;
            resource.length = static_cast<std::size_t>(length) << shift;
            out.push_back(std::move(resource));
        }
    }
    return out;
}

std::vector<NeResource> readNeFile(const std::string& path) {
    return readNeResources(readBinaryFile(path));
}

}  // namespace swchess
