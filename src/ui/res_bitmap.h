// Reads a named bitmap resource out of a 16-bit New Executable file.
//
// XCHESS.EXE carries the 153 button faces and TITLERES.DLL carries the three
// title screens. Both store a plain uncompressed 8-bit DIB with no
// BITMAPFILEHEADER in front of it, so assets/bmp.h cannot read them and this
// does.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets/bmp.h"

namespace swchess::ui {

// Every bitmap in one NE file, keyed by resource name, decoded on demand.
class ResourceBitmaps {
public:
    // Reads the file at `path`. Throws std::runtime_error when it cannot.
    explicit ResourceBitmaps(std::string path);

    // The names of every bitmap resource, in resource table order. A name that
    // appears twice is listed once, because FindResource returns the first
    // match and the second copy never loads.
    const std::vector<std::string>& names() const { return names_; }

    bool has(const std::string& name) const;

    // The DIB one resource holds. Throws std::runtime_error when the file has
    // no bitmap by that name.
    const IndexedBitmap& bitmap(const std::string& name) const;

    // The whole bitmap as top-down RGBA8. Pixels whose palette index equals
    // `transparentIndex` get alpha 0, and -1 keeps every pixel opaque.
    std::vector<std::uint8_t> rgba(const std::string& name, int transparentIndex = -1) const;

private:
    std::string path_;
    std::vector<std::string> names_;
    std::vector<std::size_t> offsets_;
    mutable std::vector<IndexedBitmap> cache_;
    mutable std::vector<bool> decoded_;
    std::vector<std::uint8_t> blob_;

    std::size_t indexOf(const std::string& name) const;
};

}  // namespace swchess::ui
