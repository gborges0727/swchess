#include "text/font.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

#include "assets/bmp.h"
#include "assets/ne.h"

namespace swchess::text {
namespace {

// Both fonts paint on palette index 159, which is pure black.
constexpr int kBackgroundIndex = 159;

constexpr int kLegCellW = 40;
constexpr int kLegCellH = 42;
constexpr int kLegColumns = 16;
// XCHESS.EXE stores the cell size just ahead of the 96 advance widths.
constexpr std::size_t kLegWidthsOffset = 0x36ec4;

constexpr int kGuiCellW = 16;
constexpr int kGuiCellH = 17;
// The GUITEXT space cell holds no ink, so the preview script gives it this
// advance by hand and this loader matches it.
constexpr int kGuiSpaceAdvance = 5;

// A byte the font cannot draw leaves a box this wide.
constexpr int kMissingAdvance = 14;

std::string joinPath(const std::string& dir, const char* name) {
    if (dir.empty()) {
        return name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t at) {
    if (at + 4 > data.size()) {
        throw std::runtime_error("read past the end of the file");
    }
    return static_cast<std::uint32_t>(data[at]) |
           (static_cast<std::uint32_t>(data[at + 1]) << 8) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16) |
           (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t at) {
    if (at + 2 > data.size()) {
        throw std::runtime_error("read past the end of the file");
    }
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

// Parses one uncompressed 8-bit DIB that starts at `start` with its
// BITMAPINFOHEADER. A bitmap resource inside an NE file has no BITMAPFILEHEADER
// ahead of it, so assets/bmp.h cannot read it and this does.
IndexedBitmap parseDib(const std::vector<std::uint8_t>& blob, std::size_t start,
                       const std::string& what) {
    const std::uint32_t headerSize = readU32(blob, start);
    if (headerSize < 40) {
        throw std::runtime_error("unsupported DIB header size in " + what);
    }
    const std::int32_t width = static_cast<std::int32_t>(readU32(blob, start + 4));
    const std::int32_t rawHeight = static_cast<std::int32_t>(readU32(blob, start + 8));
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
    const std::size_t pixelsAt = paletteAt + static_cast<std::size_t>(paletteUsed) * 4;
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

// Finds a named bitmap resource and returns the DIB it holds.
IndexedBitmap loadNamedBitmap(const std::string& path, const char* resourceName) {
    std::vector<std::uint8_t> blob = readBinaryFile(path);
    for (const NeResource& resource : readNeResources(blob)) {
        if (!resource.type.isNumeric || resource.type.id != kRtBitmap) {
            continue;
        }
        if (resource.name.isNumeric || resource.name.name != resourceName) {
            continue;
        }
        return parseDib(blob, resource.offset, path);
    }
    throw std::runtime_error(std::string(resourceName) + " is not a bitmap resource in " + path);
}

// How many columns of a cell carry ink, counted to the rightmost inked column.
int measureInk(const IndexedBitmap& bitmap, int x0, int y0, int width, int height) {
    int last = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = last + 1; x < width; ++x) {
            if (bitmap.indexAt(x0 + x, y0 + y) != kBackgroundIndex) {
                last = x;
            }
        }
    }
    return last + 1;
}

// The byte codes whose glyph is not the ASCII character, per font. The
// research note in docs/research/languages.md derives both tables.
struct CodePoint {
    char code;
    char32_t shown;
};

constexpr std::array<CodePoint, 14> kLegfontMap{{
    {'0', U'ä'},  // a with dieresis
    {'1', U'à'},  // a with grave
    {'2', U'è'},  // e with grave
    {'3', U'é'},  // e with acute
    {'4', U'ê'},  // e with circumflex
    {'5', U'ö'},  // o with dieresis
    {'6', U'ü'},  // u with dieresis
    {'7', U'á'},  // a with acute
    {'8', U'í'},  // i with acute
    {'9', U'ß'},  // sharp s
    {'[', U'1'},
    {'\\', U'0'},
    {']', U'©'},  // copyright sign
    {'^', U'®'},  // registered sign
}};

constexpr std::array<CodePoint, 3> kGuitextMap{{
    {'[', U'Ü'},  // U with dieresis
    {'\\', U'Ö'},  // O with dieresis
    {']', U'Ä'},  // A with dieresis
}};

void appendUtf8(std::string& out, char32_t value) {
    if (value < 0x80) {
        out.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xe0 | (value >> 12)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

// Reads one code point and moves `at` past it. A byte that starts no valid
// sequence returns itself, which keeps the caller moving.
char32_t nextUtf8(std::string_view text, std::size_t& at) {
    const auto byte = static_cast<unsigned char>(text[at]);
    std::size_t extra = 0;
    char32_t value = byte;
    if (byte >= 0xf0) {
        extra = 3;
        value = byte & 0x07u;
    } else if (byte >= 0xe0) {
        extra = 2;
        value = byte & 0x0fu;
    } else if (byte >= 0xc0) {
        extra = 1;
        value = byte & 0x1fu;
    }
    if (at + extra >= text.size()) {
        ++at;
        return byte;
    }
    for (std::size_t step = 1; step <= extra; ++step) {
        const auto follow = static_cast<unsigned char>(text[at + step]);
        if ((follow & 0xc0) != 0x80) {
            ++at;
            return byte;
        }
        value = (value << 6) | (follow & 0x3fu);
    }
    at += extra + 1;
    return value;
}

}  // namespace

const Glyph& BitmapFont::glyph(std::uint8_t code) const {
    if (code < kFirstCode) {
        throw std::runtime_error("no font cell for a control byte");
    }
    const std::size_t index = static_cast<std::size_t>(code) - kFirstCode;
    if (index >= glyphs.size()) {
        throw std::runtime_error("no font cell for a high byte");
    }
    return glyphs[index];
}

bool BitmapFont::has(std::uint8_t code) const {
    if (code < kFirstCode ||
        static_cast<std::size_t>(code) - kFirstCode >= glyphs.size()) {
        return false;
    }
    return glyphs[static_cast<std::size_t>(code) - kFirstCode].present;
}

BitmapFont loadLegFont(const std::string& cdDir) {
    const std::string titlePath = joinPath(cdDir, "TITLERES.DLL");
    IndexedBitmap sheet = loadNamedBitmap(titlePath, "LEGFONT");

    const std::string exePath = joinPath(cdDir, "XCHESS.EXE");
    std::vector<std::uint8_t> exe = readBinaryFile(exePath);
    const int tableCellW = readU16(exe, kLegWidthsOffset - 6);
    const int tableCellH = readU16(exe, kLegWidthsOffset - 4);
    if (tableCellW != kLegCellW || tableCellH != kLegCellH) {
        throw std::runtime_error("XCHESS.EXE does not hold a 40 by 42 cell ahead of the widths");
    }

    BitmapFont font;
    font.kind = FontKind::Legfont;
    font.cellWidth = kLegCellW - 1;
    font.cellHeight = kLegCellH - 1;
    font.glyphs.resize(kGlyphCount);
    for (int index = 0; index < kGlyphCount; ++index) {
        const int x = (index % kLegColumns) * kLegCellW + 1;
        const int y = (index / kLegColumns) * kLegCellH + 1;
        Glyph& glyph = font.glyphs[static_cast<std::size_t>(index)];
        glyph.width = font.cellWidth;
        glyph.height = font.cellHeight;
        glyph.advance =
            readU16(exe, kLegWidthsOffset + static_cast<std::size_t>(index) * 2);
        // A width of zero marks a cell the game never draws, and every one of
        // those cells holds no ink or leftover artwork.
        glyph.present = glyph.advance > 0;
        glyph.rgba = bmpRegionToRGBA(sheet, x, y, glyph.width, glyph.height, kBackgroundIndex);
    }
    return font;
}

BitmapFont loadGuiFont(const std::string& cdDir) {
    const std::string path = joinPath(cdDir, "CC256.DLL");
    IndexedBitmap strip = loadNamedBitmap(path, "GUITEXT");

    BitmapFont font;
    font.kind = FontKind::Guitext;
    font.cellWidth = kGuiCellW - 1;
    font.cellHeight = kGuiCellH - 1;
    font.glyphs.resize(kGlyphCount);
    for (int index = 0; index < kGlyphCount; ++index) {
        const int x = index * kGuiCellW;
        Glyph& glyph = font.glyphs[static_cast<std::size_t>(index)];
        glyph.width = font.cellWidth;
        glyph.height = font.cellHeight;
        const int ink = measureInk(strip, x, 0, glyph.width, glyph.height);
        glyph.present = ink > 0;
        glyph.advance = ink + 1;
        if (index == 0) {  // the space cell
            glyph.present = true;
            glyph.advance = kGuiSpaceAdvance;
        }
        glyph.rgba = bmpRegionToRGBA(strip, x, 0, glyph.width, glyph.height, kBackgroundIndex);
    }
    return font;
}

int measure(const BitmapFont& font, std::string_view raw) {
    int width = 0;
    for (char byte : raw) {
        const auto code = static_cast<std::uint8_t>(byte);
        const int advance = font.has(code) ? font.glyph(code).advance : kMissingAdvance;
        width += advance + 1;  // one blank column follows every glyph
    }
    return width;
}

TextImage render(const BitmapFont& font, std::string_view raw) {
    TextImage image;
    image.height = font.cellHeight;
    image.width = std::max(measure(font, raw), 1);
    image.rgba.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height) * 4,
                      0);

    auto pixel = [&image](int x, int y) -> std::uint8_t* {
        return &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
    };

    int pen = 0;
    for (std::size_t position = 0; position < raw.size(); ++position) {
        const auto code = static_cast<std::uint8_t>(raw[position]);
        if (!font.has(code)) {
            image.unmapped.push_back(position);
            for (int y = 0; y < image.height; ++y) {
                for (int x = 0; x < kMissingAdvance; ++x) {
                    const bool edge = x == 0 || x == kMissingAdvance - 1 || y == 0 ||
                                      y == image.height - 1;
                    if (!edge || pen + x >= image.width) {
                        continue;
                    }
                    std::uint8_t* out = pixel(pen + x, y);
                    out[0] = 255;
                    out[1] = 0;
                    out[2] = 0;
                    out[3] = 255;
                }
            }
            pen += kMissingAdvance + 1;
            continue;
        }
        const Glyph& glyph = font.glyph(code);
        // The cell is wider than the advance, so a few columns of the next
        // glyph's space carry ink. The preview script copies the same window.
        const int columns = std::min(glyph.width, glyph.advance + 4);
        for (int y = 0; y < image.height && y < glyph.height; ++y) {
            for (int x = 0; x < columns && pen + x < image.width; ++x) {
                const std::uint8_t* source =
                    &glyph.rgba[(static_cast<std::size_t>(y) * glyph.width + x) * 4];
                if (source[3] == 0) {
                    continue;
                }
                std::uint8_t* out = pixel(pen + x, y);
                out[0] = source[0];
                out[1] = source[1];
                out[2] = source[2];
                out[3] = 255;
            }
        }
        pen += glyph.advance + 1;
    }
    return image;
}

std::string toUtf8(FontKind kind, std::string_view raw) {
    std::string out;
    for (char byte : raw) {
        char32_t shown = static_cast<unsigned char>(byte);
        if (kind == FontKind::Legfont) {
            for (const CodePoint& entry : kLegfontMap) {
                if (entry.code == byte) {
                    shown = entry.shown;
                }
            }
        } else {
            for (const CodePoint& entry : kGuitextMap) {
                if (entry.code == byte) {
                    shown = entry.shown;
                }
            }
        }
        appendUtf8(out, shown);
    }
    return out;
}

std::string fromUtf8(FontKind kind, std::string_view utf8, std::string* rejected) {
    std::string out;
    std::size_t at = 0;
    while (at < utf8.size()) {
        const char32_t value = nextUtf8(utf8, at);
        char code = '?';
        bool found = false;
        if (kind == FontKind::Legfont) {
            for (const CodePoint& entry : kLegfontMap) {
                if (entry.shown == value) {
                    code = entry.code;
                    found = true;
                }
            }
        } else {
            for (const CodePoint& entry : kGuitextMap) {
                if (entry.shown == value) {
                    code = entry.code;
                    found = true;
                }
            }
        }
        if (!found) {
            // A plain ASCII character stands for itself, unless this font
            // spends that cell on an accented letter.
            const bool taken =
                kind == FontKind::Legfont && value >= U'0' && value <= U'9';
            if (value >= kFirstCode && value < kFirstCode + kGlyphCount && !taken) {
                code = static_cast<char>(value);
                found = true;
            }
        }
        if (!found && rejected != nullptr) {
            appendUtf8(*rejected, value);
        }
        out.push_back(code);
    }
    return out;
}

FontKind fontForId(int stringId) {
    const bool crawl = stringId >= 14000 && stringId <= 14031;
    const bool credits = stringId >= 14992 && stringId <= 15199;
    return (crawl || credits) ? FontKind::Legfont : FontKind::Guitext;
}

}  // namespace swchess::text
