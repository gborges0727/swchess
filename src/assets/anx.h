// Decoder for the .ANX capture files shipped on the Star Wars Chess CD.
//
// An ANX file starts with a uint32 frame count, then 450 uint32 offset slots.
// Each offset is relative to 0x70c and points at a BITMAPINFOHEADER of 40
// bytes, followed by 256 BGRA palette entries, followed by RLE pixel data.
// The dword at header+16 holds the compression field. Its high word is the
// escape byte. A byte equal to the escape byte starts an `escape, value,
// count` run. Any other byte is one literal palette index. Rows are tight,
// width * height bytes total, stored bottom-up like a Windows DIB.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace swchess {

// One decoded bitmap record. `indices` holds width * height palette indices in
// the order the file stores them, so the first row is the bottom row.
struct AnxRecord {
    std::uint32_t offset = 0;  // offset slot value, relative to 0x70c
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint8_t escape = 0;
    std::uint32_t sizeImage = 0;
    std::vector<std::uint8_t> palette;  // 1024 bytes, BGRA per entry
    std::vector<std::uint8_t> indices;
    bool complete = false;  // the decoder produced exactly width * height bytes
};

// One capture file. `timeline` lists the offset slot for every timeline entry,
// in play order. Several entries may name the same record. `records` holds each
// distinct record once, keyed by its offset slot value.
struct AnxFile {
    std::uint32_t frameCount = 0;
    std::vector<std::uint32_t> timeline;
    std::map<std::uint32_t, AnxRecord> records;
};

// Reads and decodes every distinct record in the file at `path`.
// Throws std::runtime_error when the file cannot be read or is too short.
AnxFile loadAnx(const std::string& path);

// Converts one record to top-down RGBA8, four bytes per pixel.
// Palette index 0 becomes alpha 0. Every other index becomes alpha 255.
std::vector<std::uint8_t> anxToRGBA(const AnxRecord& record);

}  // namespace swchess
