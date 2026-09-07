// The reader for the .CMP play-level files.
//
// Each of NEWCOMER.CMP, NOVICE.CMP, MODERATE.CMP, HARD.CMP and EXPERT.CMP is
// exactly 102 bytes. The front end XCHESS.EXE read one, and sent the settings
// inside it to the engine process CHESSAPP.EXE over DDE. The engine itself
// never opened a file.
//
// The file is one Chessmaster personality: a display name, a block of style
// weights that bias the evaluation, and the depth and time settings that stop
// the search.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace swchess::engine::original {

// The six style weights. The file holds two copies of them, one used while
// the engine is on its own clock and one used while it is on someone else's.
// Every shipped level except HARD.CMP holds the same six numbers twice.
struct StyleWeights {
    std::int16_t randomness{0};   // 0x22 and 0x2e, always 10
    std::int16_t attack{0};       // 0x24 and 0x30, 90 to 110
    std::int16_t defense{0};      // 0x26 and 0x32, 50 to 70
    std::int16_t material{0};     // 0x28 and 0x34, 25 to 34
    std::int16_t mobility{0};     // 0x2a and 0x36, 20 to 34
    std::int16_t pawnStructure{0};  // 0x2c and 0x38, 10 or 14

    friend bool operator==(const StyleWeights&, const StyleWeights&) = default;
};

// One .CMP file, field by field.
struct Personality {
    std::string name;        // 0x00, 32 bytes, NUL padded: "Kamikaze"
    std::uint16_t signature{0};  // 0x20, always 0x201A
    StyleWeights primary{};      // 0x22 to 0x2c
    StyleWeights secondary{};    // 0x2e to 0x38
    std::int16_t contempt{0};    // 0x3a, always 2
    std::int16_t searchDepth{0};   // 0x3c, 3 to 35
    std::int16_t secondsPerMove{0};  // 0x3e, 0 to 60
    std::int16_t bookBreadth{0};     // 0x40, 0 to 100
    std::int16_t aggression{0};      // 0x42, 0 to 100
    std::string echoName;    // 0x44, the same 32 bytes again
    std::uint8_t flagA{0};   // 0x64, always 2
    std::uint8_t flagB{0};   // 0x65, 1 on HARD and EXPERT, 0 on the rest

    // Reads the file. Throws std::runtime_error when it is missing, is not
    // 102 bytes, or does not carry the 0x201A signature.
    static Personality load(const std::string& path);
};

// The exact size every shipped .CMP file has.
constexpr std::size_t kPersonalitySize = 102;

}  // namespace swchess::engine::original
