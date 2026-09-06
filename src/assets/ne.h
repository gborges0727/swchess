// Reader for the resource table of a 16-bit New Executable file.
//
// The piece DLLs, SWCAUDIO.DLL, TITLERES.DLL and the language DLLs are all NE
// files. This reader treats them as data. It reads the MZ header, follows
// e_lfanew to the NE header, and walks the resource table at NE offset 0x24.
// It never loads a library and never runs any code from the file.
//
// A type id or a resource name is either a number with bit 15 set or a byte
// offset to a Pascal string inside the resource table. The table starts with an
// alignment shift, and every stored offset and length shifts left by it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess {

// Windows resource type numbers this port cares about.
constexpr int kRtBitmap = 2;
constexpr int kRtString = 6;

// A resource type or resource name. A numeric entry sets `id` and leaves
// `name` empty. A named entry sets `name` and leaves `id` at -1.
struct NeId {
    bool isNumeric = false;
    int id = -1;
    std::string name;

    // "WAVE" for a named entry, "#2" for a numeric one.
    std::string text() const;
};

// One entry in the resource table. `offset` and `length` are file offsets in
// bytes, already shifted by the table's alignment.
struct NeResource {
    NeId type;
    NeId name;
    std::size_t offset = 0;
    std::size_t length = 0;
    std::uint16_t flags = 0;
};

// Reads a whole file into memory. Throws std::runtime_error when it cannot.
std::vector<std::uint8_t> readBinaryFile(const std::string& path);

// Returns every resource in an NE image held in memory, in table order.
// Throws std::runtime_error when the bytes are not an NE image.
std::vector<NeResource> readNeResources(const std::vector<std::uint8_t>& blob);

// Reads the file at `path` and returns its resources. The caller usually needs
// the bytes too, so read the file once with readBinaryFile and call
// readNeResources on the result instead when that matters.
std::vector<NeResource> readNeFile(const std::string& path);

}  // namespace swchess
