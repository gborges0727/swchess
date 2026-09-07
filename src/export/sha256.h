// SHA-256, the hash the manifests record for every file and every source.
//
// The Python tools call hashlib.sha256 and write the digest as lowercase hex.
// This is the same algorithm written out so the helpers need no crypto library
// and produce the same strings.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess::exporter {

// Returns the lowercase hex digest of the bytes in `data`.
std::string sha256Hex(const std::uint8_t* data, std::size_t size);

inline std::string sha256Hex(const std::vector<std::uint8_t>& data) {
    return sha256Hex(data.data(), data.size());
}

// Reads the file at `path` in blocks and returns its digest.
// Throws std::runtime_error when the file cannot be read.
std::string sha256File(const std::string& path);

}  // namespace swchess::exporter
