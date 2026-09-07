#include "export/text.h"

#include <cctype>

namespace swchess::exporter {
namespace {

// The 128 characters code page 437 puts above ASCII, in byte order from 0x80.
const std::uint16_t kCp437High[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB,
    0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4,
    0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5,
    0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592,
    0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557,
    0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C,
    0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518,
    0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0,
    0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6,
    0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

void appendUtf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

}  // namespace

std::string cp437ToUtf8(const std::uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t byte = data[i];
        appendUtf8(out, byte < 0x80 ? byte : kCp437High[byte - 0x80]);
    }
    return out;
}

std::string cp437ToUtf8(const std::string& bytes) {
    return cp437ToUtf8(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

std::string latin1ToUtf8(const std::uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        appendUtf8(out, data[i]);
    }
    return out;
}

std::string toHex(const std::uint8_t* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0xF];
    }
    return out;
}

std::string upperAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

std::optional<int> parseInt(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }
    if (first == last) {
        return std::nullopt;
    }
    std::size_t at = first;
    bool negative = false;
    if (text[at] == '+' || text[at] == '-') {
        negative = text[at] == '-';
        ++at;
    }
    if (at == last) {
        return std::nullopt;
    }
    long long value = 0;
    for (; at < last; ++at) {
        if (text[at] < '0' || text[at] > '9') {
            return std::nullopt;
        }
        value = value * 10 + (text[at] - '0');
        if (value > 4000000000LL) {
            return std::nullopt;
        }
    }
    return static_cast<int>(negative ? -value : value);
}

}  // namespace swchess::exporter
