#include "ui/res_bitmap.h"

#include <stdexcept>
#include <utility>

#include "assets/ne.h"

namespace swchess::ui {
namespace {

std::uint16_t readU16(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 2 > blob.size()) {
        throw std::runtime_error("DIB header runs past the end of the file");
    }
    return static_cast<std::uint16_t>(blob[at] | (blob[at + 1] << 8));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 4 > blob.size()) {
        throw std::runtime_error("DIB header runs past the end of the file");
    }
    return static_cast<std::uint32_t>(blob[at]) | (static_cast<std::uint32_t>(blob[at + 1]) << 8) |
           (static_cast<std::uint32_t>(blob[at + 2]) << 16) |
           (static_cast<std::uint32_t>(blob[at + 3]) << 24);
}

IndexedBitmap parseDib(const std::vector<std::uint8_t>& blob, std::size_t start,
                       const std::string& what) {
    const std::uint32_t headerSize = readU32(blob, start);
    if (headerSize < 40) {
        throw std::runtime_error("unsupported DIB header size in " + what);
    }
    const auto width = static_cast<std::int32_t>(readU32(blob, start + 4));
    const auto rawHeight = static_cast<std::int32_t>(readU32(blob, start + 8));
    const std::uint16_t bitCount = readU16(blob, start + 14);
    const std::uint32_t compression = readU32(blob, start + 16);
    if (bitCount != 8 || compression != 0) {
        throw std::runtime_error("expected an uncompressed 8-bit DIB in " + what);
    }

    IndexedBitmap bitmap;
    bitmap.topDown = rawHeight < 0;
    bitmap.width = width;
    bitmap.height = bitmap.topDown ? -rawHeight : rawHeight;
    if (width <= 0 || bitmap.height <= 0) {
        throw std::runtime_error("bad DIB dimensions in " + what);
    }

    std::uint32_t paletteUsed = readU32(blob, start + 32);
    if (paletteUsed == 0) {
        paletteUsed = 256;
    }
    const std::size_t paletteAt = start + headerSize;
    const std::size_t pixelsAt = paletteAt + static_cast<std::size_t>(paletteUsed) * 4u;
    bitmap.stride = (static_cast<std::size_t>(width) + 3u) & ~std::size_t(3u);
    const std::size_t need = bitmap.stride * static_cast<std::size_t>(bitmap.height);
    if (pixelsAt + need > blob.size()) {
        throw std::runtime_error("DIB pixels run past the end of " + what);
    }
    bitmap.palette.assign(blob.begin() + static_cast<std::ptrdiff_t>(paletteAt),
                          blob.begin() + static_cast<std::ptrdiff_t>(pixelsAt));
    bitmap.pixels.assign(blob.begin() + static_cast<std::ptrdiff_t>(pixelsAt),
                         blob.begin() + static_cast<std::ptrdiff_t>(pixelsAt + need));
    return bitmap;
}

}  // namespace

ResourceBitmaps::ResourceBitmaps(std::string path) : path_(std::move(path)) {
    blob_ = readBinaryFile(path_);
    for (const NeResource& resource : readNeResources(blob_)) {
        if (!resource.type.isNumeric || resource.type.id != kRtBitmap) {
            continue;
        }
        if (resource.name.isNumeric) {
            continue;
        }
        if (has(resource.name.name)) {
            continue;  // FindResource answers with the first copy
        }
        names_.push_back(resource.name.name);
        offsets_.push_back(resource.offset);
    }
    cache_.resize(names_.size());
    decoded_.assign(names_.size(), false);
}

std::size_t ResourceBitmaps::indexOf(const std::string& name) const {
    for (std::size_t at = 0; at < names_.size(); ++at) {
        if (names_[at] == name) {
            return at;
        }
    }
    return names_.size();
}

bool ResourceBitmaps::has(const std::string& name) const {
    return indexOf(name) < names_.size();
}

const IndexedBitmap& ResourceBitmaps::bitmap(const std::string& name) const {
    const std::size_t at = indexOf(name);
    if (at >= names_.size()) {
        throw std::runtime_error(name + " is not a bitmap resource in " + path_);
    }
    if (!decoded_[at]) {
        cache_[at] = parseDib(blob_, offsets_[at], path_ + " resource " + name);
        decoded_[at] = true;
    }
    return cache_[at];
}

std::vector<std::uint8_t> ResourceBitmaps::rgba(const std::string& name,
                                                int transparentIndex) const {
    const IndexedBitmap& source = bitmap(name);
    return bmpRegionToRGBA(source, 0, 0, source.width, source.height, transparentIndex);
}

}  // namespace swchess::ui
