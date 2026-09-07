#include "engine/original/cmp.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace swchess::engine::original {
namespace {

std::uint16_t word(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

std::int16_t signedWord(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::int16_t>(word(b, at));
}

// Reads a NUL-padded fixed-width name and drops the padding.
std::string readName(const std::vector<std::uint8_t>& b, std::size_t at, std::size_t width) {
    std::size_t end = at;
    while (end < at + width && b[end] != 0) ++end;
    return std::string(b.begin() + static_cast<long>(at), b.begin() + static_cast<long>(end));
}

StyleWeights weights(const std::vector<std::uint8_t>& b, std::size_t at) {
    StyleWeights w;
    w.randomness = signedWord(b, at);
    w.attack = signedWord(b, at + 2);
    w.defense = signedWord(b, at + 4);
    w.material = signedWord(b, at + 6);
    w.mobility = signedWord(b, at + 8);
    w.pawnStructure = signedWord(b, at + 10);
    return w;
}

}  // namespace

Personality Personality::load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) throw std::runtime_error("cannot open " + path);
    std::vector<std::uint8_t> bytes(kPersonalitySize);
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    const bool more = std::fgetc(f) != EOF;
    std::fclose(f);
    if (got != kPersonalitySize || more) {
        throw std::runtime_error(path + " is not 102 bytes");
    }

    Personality p;
    p.name = readName(bytes, 0x00, 32);
    p.signature = word(bytes, 0x20);
    if (p.signature != 0x201a) throw std::runtime_error(path + " has no 0x201A signature");
    p.primary = weights(bytes, 0x22);
    p.secondary = weights(bytes, 0x2e);
    p.contempt = signedWord(bytes, 0x3a);
    p.searchDepth = signedWord(bytes, 0x3c);
    p.secondsPerMove = signedWord(bytes, 0x3e);
    p.bookBreadth = signedWord(bytes, 0x40);
    p.aggression = signedWord(bytes, 0x42);
    p.echoName = readName(bytes, 0x44, 32);
    p.flagA = bytes[0x64];
    p.flagB = bytes[0x65];
    return p;
}

}  // namespace swchess::engine::original
