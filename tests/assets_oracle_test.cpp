// Compares the C++ asset readers against the Python extractor.
//
// tests/assets_dump.py writes one checksum line per piece bitmap, per sheet
// cell and per WAVE record. This program reads the same original files with
// src/assets, builds the same lines, and reports every difference.
//
//   assets_oracle_test <oracle.txt> <cd dir> piece <AT>
//   assets_oracle_test <oracle.txt> <cd dir> cells <WHTBTM>
//   assets_oracle_test <oracle.txt> <cd dir> waves
//   assets_oracle_test <oracle.txt> <cd dir> totals <pieces> <cells> <waves> <distinct>

#include <CommonCrypto/CommonDigest.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/ne.h"
#include "assets/piece_dll.h"
#include "assets/sheet.h"
#include "assets/wav.h"

namespace {

std::string sha256Hex(const std::uint8_t* bytes, std::size_t length) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes, static_cast<CC_LONG>(length), digest);
    char text[CC_SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i) {
        std::snprintf(text + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(text, CC_SHA256_DIGEST_LENGTH * 2);
}

std::string hex(std::size_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

std::vector<std::string> readOracle(const std::string& path,
                                    const std::vector<std::string>& prefixes) {
    std::ifstream input(path);
    if (!input) {
        std::fprintf(stderr, "cannot read the oracle file %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<std::string> wanted;
    std::string line;
    while (std::getline(input, line)) {
        for (const std::string& prefix : prefixes) {
            if (line.rfind(prefix, 0) == 0) {
                wanted.push_back(line);
                break;
            }
        }
    }
    return wanted;
}

// Reports the first ten differences, then how many remain.
int compare(const std::vector<std::string>& expected, const std::vector<std::string>& actual) {
    if (expected.empty()) {
        std::fprintf(stderr, "the oracle holds no lines for this test\n");
        return 1;
    }
    std::size_t differences = 0;
    std::size_t shown = 0;
    std::size_t count = expected.size() > actual.size() ? expected.size() : actual.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::string mine = i < actual.size() ? actual[i] : std::string("<missing in C++>");
        const std::string theirs =
            i < expected.size() ? expected[i] : std::string("<missing in Python>");
        if (mine != theirs) {
            ++differences;
            if (shown < 10) {
                std::fprintf(stderr, "python: %s\nc++   : %s\n", theirs.c_str(), mine.c_str());
                ++shown;
            }
        }
    }
    if (differences != 0) {
        std::fprintf(stderr, "%zu of %zu lines differ\n", differences, count);
        return 1;
    }
    std::printf("%zu lines match\n", count);
    return 0;
}

int checkPiece(const std::string& oraclePath, const std::string& cdDir, const std::string& piece) {
    swchess::PieceDll dll = swchess::loadPieceDll(cdDir, piece);
    std::vector<std::string> actual;
    for (const swchess::PieceBitmap& bitmap : dll.bitmaps) {
        std::vector<std::uint8_t> rgba = swchess::pieceToRGBA(bitmap);
        std::ostringstream line;
        line << "PIECE " << piece << " " << bitmap.resourceName << " " << hex(bitmap.resourceOffset)
             << " " << bitmap.resourceLength << " " << bitmap.record.width << " "
             << bitmap.record.height << " " << sha256Hex(rgba.data(), rgba.size());
        actual.push_back(line.str());
    }
    return compare(readOracle(oraclePath, {"PIECE " + piece + " "}), actual);
}

int checkCells(const std::string& oraclePath, const std::string& cdDir, const std::string& set) {
    swchess::PieceSheet sheet = swchess::loadPieceSheet(cdDir, set + "_");
    std::vector<std::string> actual;
    for (const swchess::SheetCell& cell : sheet.cells) {
        std::ostringstream line;
        line << "CELL " << set << " r" << cell.row << "c" << cell.column << " " << cell.x << " "
             << cell.y << " " << cell.width << " " << cell.height << " "
             << sha256Hex(cell.rgba.data(), cell.rgba.size());
        actual.push_back(line.str());
    }
    // Every separator line must be one flat color for the slicing rule to hold.
    if (!sheet.separatorsFlat) {
        std::fprintf(stderr, "a separator line in %s is not one flat color\n",
                     sheet.source.c_str());
        return 1;
    }
    return compare(readOracle(oraclePath, {"CELL " + set + " "}), actual);
}

int checkWaves(const std::string& oraclePath, const std::string& cdDir) {
    std::vector<swchess::WaveResource> waves = swchess::loadAudioDll(cdDir);
    std::vector<std::string> actual;
    int index = 0;
    for (const swchess::WaveResource& wave : waves) {
        std::ostringstream line;
        line << "WAVE " << index << " " << wave.name << " " << hex(wave.offset) << " "
             << wave.riffLength << " " << wave.sound.channels << " " << wave.sound.sampleRate << " "
             << wave.sound.bitsPerSample << " "
             << sha256Hex(wave.sound.samples.data(), wave.sound.samples.size());
        actual.push_back(line.str());
        ++index;
    }
    for (const char* const name : swchess::kLooseWavFiles) {
        swchess::WaveSound sound = swchess::loadWavFile(cdDir + "/" + name);
        std::ostringstream line;
        line << "LOOSE " << name << " " << sound.channels << " " << sound.sampleRate << " "
             << sound.bitsPerSample << " " << sha256Hex(sound.samples.data(), sound.samples.size());
        actual.push_back(line.str());
    }
    return compare(readOracle(oraclePath, {"WAVE ", "LOOSE "}), actual);
}

// Confirms the counts the plan states: 1344 piece bitmaps, 48 sheet cells and
// 110 WAVE records carrying 109 distinct names.
int checkTotals(const std::string& oraclePath, const std::string& cdDir, int wantPieces,
                int wantCells, int wantWaves, int wantDistinct) {
    std::vector<std::string> totals = readOracle(oraclePath, {"TOTAL "});
    if (totals.size() != 1) {
        std::fprintf(stderr, "expected one TOTAL line in the oracle, found %zu\n", totals.size());
        return 1;
    }
    std::ostringstream expected;
    expected << "TOTAL pieces " << wantPieces << " cells " << wantCells << " waves " << wantWaves
             << " distinct " << wantDistinct;
    if (totals[0] != expected.str()) {
        std::fprintf(stderr, "python: %s\nwanted: %s\n", totals[0].c_str(),
                     expected.str().c_str());
        return 1;
    }

    int pieces = 0;
    for (const char* const code : swchess::kPieceCodes) {
        pieces += static_cast<int>(swchess::loadPieceDll(cdDir, code).bitmaps.size());
    }
    int cells = 0;
    for (const char* const key : swchess::kSheetKeys) {
        cells += static_cast<int>(swchess::loadPieceSheet(cdDir, key).cells.size());
    }
    std::vector<swchess::WaveResource> waves = swchess::loadAudioDll(cdDir);
    std::set<std::string> names;
    for (const swchess::WaveResource& wave : waves) {
        names.insert(wave.name);
    }
    if (pieces != wantPieces || cells != wantCells ||
        static_cast<int>(waves.size()) != wantWaves ||
        static_cast<int>(names.size()) != wantDistinct) {
        std::fprintf(stderr,
                     "c++ counted %d piece bitmaps, %d cells, %zu WAVE records, %zu names\n",
                     pieces, cells, waves.size(), names.size());
        return 1;
    }
    std::printf("%d piece bitmaps, %d sheet cells, %zu WAVE records under %zu names\n", pieces,
                cells, waves.size(), names.size());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: assets_oracle_test <oracle.txt> <cd dir> "
                     "<piece|cells|waves|totals> ...\n");
        return 2;
    }
    std::string oraclePath = argv[1];
    std::string cdDir = argv[2];
    std::string mode = argv[3];
    try {
        if (mode == "piece" && argc == 5) {
            return checkPiece(oraclePath, cdDir, argv[4]);
        }
        if (mode == "cells" && argc == 5) {
            return checkCells(oraclePath, cdDir, argv[4]);
        }
        if (mode == "waves" && argc == 4) {
            return checkWaves(oraclePath, cdDir);
        }
        if (mode == "totals" && argc == 8) {
            return checkTotals(oraclePath, cdDir, std::atoi(argv[4]), std::atoi(argv[5]),
                               std::atoi(argv[6]), std::atoi(argv[7]));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "bad arguments\n");
    return 2;
}
