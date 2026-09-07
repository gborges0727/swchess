// Small text helpers the extractor needs to match the Python tool.
//
// tools/extract reads the INI files as code page 437 and reads the language
// string tables twice, once as code page 437 and once as latin-1. JSON holds
// text, not bytes, so those readings have to reach the writer as UTF-8.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace swchess::exporter {

// Turns code page 437 bytes into UTF-8. Every one of the 256 bytes has a
// character, so nothing is ever dropped.
std::string cp437ToUtf8(const std::uint8_t* data, std::size_t size);
std::string cp437ToUtf8(const std::string& bytes);

// Turns latin-1 bytes into UTF-8. Every byte is a character here too.
std::string latin1ToUtf8(const std::uint8_t* data, std::size_t size);

// Lowercase hex, the way Python's bytes.hex() writes it.
std::string toHex(const std::uint8_t* data, std::size_t size);

// Upper cases the ASCII letters and leaves every other byte alone, the way
// AnsiUpper treats the names in these files.
std::string upperAscii(const std::string& text);

// Reads a decimal integer the way Python's int() does after a strip. Returns
// nothing when the text is not a whole number.
std::optional<int> parseInt(const std::string& text);

}  // namespace swchess::exporter
