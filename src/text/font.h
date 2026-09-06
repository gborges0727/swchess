// The two bitmap fonts of Star Wars Chess, and the layout that draws a stored
// string with one of them.
//
// LEGFONT is a bitmap resource in TITLERES.DLL. It is a 16 by 6 grid of 40 by
// 42 cells, one cell per byte from 0x20 to 0x7f, and a white separator line
// runs down the left edge and along the top edge of every cell. XCHESS.EXE
// carries its advance widths as 96 little-endian words at file offset 0x36ec4.
// The opening crawl and the credit roll draw through LEGFONT.
//
// GUITEXT is a bitmap resource in CC256.DLL. It is one 1542 by 17 strip of 96
// cells, 16 pixels apart, with a separator line down the right edge and along
// the bottom of each cell. No advance-width table for it turned up anywhere on
// the CD, so this loader measures the ink of each cell the way
// tools/fonts/legfont.py does. Every menu label and every move message draws
// through GUITEXT.
//
// Neither font stores digits and accented letters where ASCII puts them.
// LEGFONT keeps accented lowercase letters in its digit cells, and GUITEXT
// keeps the three German capitals in its bracket cells. Rendering takes the
// stored bytes and needs no translation. Use toUtf8 and fromUtf8 to cross
// between stored bytes and the text a person reads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace swchess::text {

enum class FontKind { Legfont, Guitext };

// The first byte both fonts draw, and how many cells each one holds.
constexpr int kFirstCode = 0x20;
constexpr int kGlyphCount = 96;

// One glyph cell as top-down RGBA8. The background pixels get alpha 0, so a
// caller can composite a glyph straight onto artwork. `advance` is how far the
// pen moves before the one-pixel gap that follows every glyph.
struct Glyph {
    int width = 0;
    int height = 0;
    int advance = 0;
    bool present = false;  // false for a cell the game never draws
    std::vector<std::uint8_t> rgba;
};

struct BitmapFont {
    FontKind kind = FontKind::Legfont;
    int cellWidth = 0;   // the drawable width of a cell, separator excluded
    int cellHeight = 0;  // the drawable height of a cell, and the line height
    std::vector<Glyph> glyphs;  // 96 entries, one per byte from 0x20 to 0x7f

    // The cell one stored byte selects. Throws std::runtime_error for a byte
    // outside 0x20 to 0x7f.
    const Glyph& glyph(std::uint8_t code) const;

    // True when this byte has a cell the game draws.
    bool has(std::uint8_t code) const;
};

BitmapFont loadLegFont(const std::string& cdDir);
BitmapFont loadGuiFont(const std::string& cdDir);

// One rendered line as top-down RGBA8. Background pixels keep alpha 0.
// `unmapped` lists the byte positions the font cannot draw. Each of those
// leaves a red 14 by cellHeight outline, which is what the preview script
// draws.
struct TextImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<std::size_t> unmapped;
};

// How wide `raw` draws, counting the one-pixel gap after every glyph.
int measure(const BitmapFont& font, std::string_view raw);

// Draws the stored bytes left to right. Nothing is translated here, so pass
// the bytes the string table holds.
TextImage render(const BitmapFont& font, std::string_view raw);

// Turns stored bytes into the text a person reads, in UTF-8.
std::string toUtf8(FontKind kind, std::string_view raw);

// Turns readable UTF-8 back into the bytes the game stores. A character
// neither font code covers becomes '?' and, when `rejected` is not null, is
// appended to it.
std::string fromUtf8(FontKind kind, std::string_view utf8, std::string* rejected = nullptr);

// Which font draws a string id. The crawl at 14000 to 14031 and the credit
// roll at 14992 to 15199 use LEGFONT, and every other id uses GUITEXT.
FontKind fontForId(int stringId);

}  // namespace swchess::text
