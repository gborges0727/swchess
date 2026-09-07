// swchess-extract, the native replacement for `python3 -m tools.extract`.
//
//   swchess-extract --cd '/path/to/CD' --out '/path/to/cache'
//
// It reads the player's CD folder without changing it and fills the output
// directory with the decoded artwork the game loads. The run writes into a
// sibling directory and moves it into place only when every count matches, so
// an interrupted run leaves an existing cache alone.
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "export/extract.h"

namespace {

int usage() {
    std::cerr << "usage: swchess-extract --cd <cd directory> --out <cache directory>\n"
              << "       --verify   check the CD for missing files and stop\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string cdDir = "original/win3x/cd";
    std::string outDir = "assets";
    bool verifyOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool hasValue = i + 1 < argc;
        if (flag == "--cd" && hasValue) {
            cdDir = argv[++i];
        } else if (flag == "--out" && hasValue) {
            outDir = argv[++i];
        } else if (flag == "--verify") {
            verifyOnly = true;
        } else if (flag == "-h" || flag == "--help") {
            return usage();
        } else {
            std::cerr << "swchess-extract: unknown argument " << flag << "\n";
            return usage();
        }
    }

    try {
        std::vector<std::string> missing = swchess::exporter::missingCdFiles(cdDir);
        if (!missing.empty()) {
            std::cerr << "swchess-extract: " << cdDir << " is missing " << missing.size()
                      << " files\n";
            for (const std::string& name : missing) {
                std::cerr << "  " << name << "\n";
            }
            return 1;
        }
        if (verifyOnly) {
            std::cout << "every file the extractor reads is in " << cdDir << std::endl;
            return 0;
        }

        swchess::exporter::ExtractOptions options;
        options.cdDir = cdDir;
        options.outDir = outDir;
        swchess::exporter::ExtractResult result = swchess::exporter::runExtract(options);

        std::cout << "extractor finished in " << result.seconds << " seconds" << std::endl;
        for (const auto& count : swchess::exporter::expectedCounts()) {
            const std::int64_t got = result.counts.at(count.first);
            std::cout << "  " << (got == count.second ? "ok " : "BAD") << " " << count.first
                      << ": " << got << " (expected " << count.second << ")" << std::endl;
        }
        return result.ok ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "swchess-extract: " << error.what() << std::endl;
        return 1;
    }
}
