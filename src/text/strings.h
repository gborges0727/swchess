// Reader for the four language string tables of Star Wars Chess.
//
// RESENG.DLL, RESFRN.DLL, RESGER.DLL and RESSPN.DLL are NE files that hold 33
// resources each, all of type 6, the 16-bit Windows string table. One resource
// holds sixteen strings, each written as a length byte followed by that many
// bytes. The string id is (resource id - 1) * 16 + slot, so the four files
// cover the same 528 ids.
//
// The bytes are not text in any code page. The game reuses printable ASCII
// codes for accented letters, and which letter a code means depends on the font
// that draws the string. This reader keeps the stored bytes untouched. Ask
// src/text/font.h to turn them into glyphs or into UTF-8.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace swchess::text {

enum class Language { English, French, German, Spanish };

// The DLL that holds one language, for example "RESENG.DLL".
const char* languageFileName(Language language);

// The name a person reads, for example "english".
const char* languageName(Language language);

// One string slot. A slot with no length byte in the file leaves `present`
// false, and the game treats that id as empty on purpose.
struct StringEntry {
    int id = 0;
    bool present = false;
    std::string bytes;  // the stored bytes, never translated
};

// Every slot of one language, keyed by string id.
struct StringTable {
    Language language = Language::English;
    std::map<int, StringEntry> entries;

    // The stored bytes of one id. Returns an empty view for an id the file
    // leaves empty and for an id the file does not carry at all.
    std::string_view get(int id) const;

    // True when the file carries a length byte for this id.
    bool has(int id) const;

    // How many ids the file carries. All four languages report 528.
    std::size_t size() const { return entries.size(); }

    // Every id in ascending order.
    std::vector<int> ids() const;
};

// Reads one language out of `cdDir`. Throws std::runtime_error when the file is
// missing or is not an NE image.
StringTable loadStrings(const std::string& cdDir, Language language);

}  // namespace swchess::text
