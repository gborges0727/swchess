// Compares the C++ decoders against the Python reference.
//
// tests/anx_dump.py writes one checksum line per distinct ANX record and per
// BMP. This program decodes the same files with src/assets/anx.cpp and
// src/assets/bmp.cpp, builds the same lines, and reports every difference.
//
//   anx_oracle_test <oracle.txt> <cd dir> anx <NAME.ANX>
//   anx_oracle_test <oracle.txt> <cd dir> bmp <NAME.BMP>
//   anx_oracle_test <oracle.txt> <cd dir> totals <records> <timeline>


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "assets/anx.h"
#include "assets/bmp.h"
#include "export/sha256.h"

namespace {

std::string sha256Hex(const std::uint8_t* bytes, std::size_t length) {
    return swchess::exporter::sha256Hex(bytes, length);
}

std::vector<std::string> readOracle(const std::string& path, const std::string& prefix) {
    std::ifstream input(path);
    if (!input) {
        std::fprintf(stderr, "cannot read the oracle file %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<std::string> wanted;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) == 0) {
            wanted.push_back(line);
        }
    }
    return wanted;
}

// Reports the first ten differences, then how many remain.
int compare(const std::vector<std::string>& expected, const std::vector<std::string>& actual) {
    std::size_t differences = 0;
    std::size_t shown = 0;
    std::size_t count = expected.size() > actual.size() ? expected.size() : actual.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::string mine = i < actual.size() ? actual[i] : std::string("<missing in C++>");
        const std::string theirs = i < expected.size() ? expected[i] : std::string("<missing in Python>");
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

int checkAnx(const std::string& oraclePath, const std::string& cdDir, const std::string& name) {
    swchess::AnxFile file = swchess::loadAnx(cdDir + "/" + name);
    std::vector<std::string> actual;
    for (const auto& [offset, record] : file.records) {
        std::ostringstream line;
        line << "ANX " << name << " 0x" << std::hex << offset << std::dec << " " << record.width
             << " " << record.height << " "
             << sha256Hex(record.indices.data(), record.indices.size());
        actual.push_back(line.str());
        if (!record.complete) {
            std::fprintf(stderr, "record 0x%x in %s decoded %zu of %lld pixels\n", offset,
                         name.c_str(), record.indices.size(),
                         static_cast<long long>(record.width) * record.height);
            return 1;
        }
    }
    return compare(readOracle(oraclePath, "ANX " + name + " "), actual);
}

int checkBmp(const std::string& oraclePath, const std::string& cdDir, const std::string& name) {
    swchess::Image image = swchess::loadBmp(cdDir + "/" + name);
    std::ostringstream line;
    line << "BMP " << name << " " << image.width << " " << image.height << " "
         << sha256Hex(image.rgba.data(), image.rgba.size());
    std::vector<std::string> actual{line.str()};
    return compare(readOracle(oraclePath, "BMP " + name + " "), actual);
}

// Confirms the corpus totals the plan states: 4799 distinct records and 5423
// timeline entries.
int checkTotals(const std::string& oraclePath, const std::string& cdDir, int wantRecords,
                int wantTimeline) {
    std::vector<std::string> oracleTotals = readOracle(oraclePath, "TOTAL ");
    if (oracleTotals.size() != 1) {
        std::fprintf(stderr, "expected one TOTAL line in the oracle, found %zu\n",
                     oracleTotals.size());
        return 1;
    }
    std::ostringstream expected;
    expected << "TOTAL records " << wantRecords << " timeline " << wantTimeline;
    if (oracleTotals[0] != expected.str()) {
        std::fprintf(stderr, "python: %s\nwanted: %s\n", oracleTotals[0].c_str(),
                     expected.str().c_str());
        return 1;
    }

    // Count the same two numbers straight from the C++ decoder.
    std::vector<std::string> anxLines = readOracle(oraclePath, "ANX ");
    std::vector<std::string> names;
    for (const std::string& line : anxLines) {
        std::istringstream parts(line);
        std::string tag;
        std::string name;
        parts >> tag >> name;
        if (names.empty() || names.back() != name) {
            names.push_back(name);
        }
    }
    long records = 0;
    long timeline = 0;
    for (const std::string& name : names) {
        swchess::AnxFile file = swchess::loadAnx(cdDir + "/" + name);
        records += static_cast<long>(file.records.size());
        timeline += static_cast<long>(file.timeline.size());
    }
    if (records != wantRecords || timeline != wantTimeline) {
        std::fprintf(stderr, "c++ counted %ld records and %ld timeline entries in %zu files\n",
                     records, timeline, names.size());
        return 1;
    }
    std::printf("%zu capture files hold %ld distinct records and %ld timeline entries\n",
                names.size(), records, timeline);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: anx_oracle_test <oracle.txt> <cd dir> <anx|bmp|totals> ...\n");
        return 2;
    }
    std::string oraclePath = argv[1];
    std::string cdDir = argv[2];
    std::string mode = argv[3];
    try {
        if (mode == "anx" && argc == 5) {
            return checkAnx(oraclePath, cdDir, argv[4]);
        }
        if (mode == "bmp" && argc == 5) {
            return checkBmp(oraclePath, cdDir, argv[4]);
        }
        if (mode == "totals" && argc == 6) {
            return checkTotals(oraclePath, cdDir, std::atoi(argv[4]), std::atoi(argv[5]));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "bad arguments\n");
    return 2;
}
