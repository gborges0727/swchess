#include "engine/original/cmp.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace swchess::engine::original {
namespace {

std::vector<std::uint8_t> readExactly(const std::string& path, std::size_t want) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) throw std::runtime_error("cannot open " + path);
    std::vector<std::uint8_t> bytes(want);
    const std::size_t got = std::fread(bytes.data(), 1, want, f);
    const bool more = std::fgetc(f) != EOF;
    std::fclose(f);
    if (got != want || more) {
        throw std::runtime_error(path + " is not " + std::to_string(want) + " bytes");
    }
    return bytes;
}

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

PieceValues readValues(const std::vector<std::uint8_t>& b, std::size_t at) {
    PieceValues v;
    v.centerPawn = signedWord(b, at);
    v.queen = signedWord(b, at + 2);
    v.rook = signedWord(b, at + 4);
    v.bishop = signedWord(b, at + 6);
    v.knight = signedWord(b, at + 8);
    v.pawn = signedWord(b, at + 10);
    return v;
}

}  // namespace

std::int16_t PieceValues::of(chess::PieceType type) const {
    switch (type) {
        case chess::PieceType::King: return centerPawn;
        case chess::PieceType::Queen: return queen;
        case chess::PieceType::Rook: return rook;
        case chess::PieceType::Bishop: return bishop;
        case chess::PieceType::Knight: return knight;
        case chess::PieceType::Pawn: return pawn;
    }
    return 0;
}

int Personality::skipPercent() const {
    // The front end sends accuracy + 40 and the engine keeps 100 minus that,
    // which is 60 minus the field.
    return std::clamp(60 - static_cast<int>(accuracy), 0, 100);
}

Personality Personality::load(const std::string& path) {
    const std::vector<std::uint8_t> bytes = readExactly(path, kPersonalitySize);
    Personality p;
    p.title = readName(bytes, 0x00, 32);
    p.magic = word(bytes, 0x20);
    p.own = readValues(bytes, 0x22);
    p.opponent = readValues(bytes, 0x2e);
    p.contempt = signedWord(bytes, 0x3a);
    p.bookMoves = signedWord(bytes, 0x3c);
    p.accuracy = signedWord(bytes, 0x3e);
    p.pieceVersusPawn = signedWord(bytes, 0x40);
    p.materialWeight = signedWord(bytes, 0x42);
    p.name = readName(bytes, 0x44, 32);
    p.playerType = bytes[0x64];
    p.ponder = bytes[0x65];
    return p;
}

TimeControl TimeControl::load(const std::string& path) {
    const std::vector<std::uint8_t> bytes = readExactly(path, kTimeControlSize);
    TimeControl t;
    t.mode = word(bytes, 0x64);
    t.secondsPerMove = signedWord(bytes, 0x66);
    t.fixedDepth = signedWord(bytes, 0x6c);
    return t;
}

}  // namespace swchess::engine::original
