// Decodes the original CD into the asset cache the game reads.
//
// This is the native port of `python3 -m tools.extract`. It writes the same
// directory layout and the same bytes: catalog.json at the root, one folder
// per capture under captures/, one folder per piece under pieces/, the sliced
// piece sheets under sets/, the four backgrounds under backgrounds/, every
// sound under audio/, the four language string tables under locales/, and the
// title artwork under ui/.
//
// The run writes into a temporary directory beside the requested one and moves
// it into place only after every count matches. An interrupted run therefore
// leaves the previous cache untouched.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace swchess::exporter {

// What the caller asks for.
struct ExtractOptions {
    std::string cdDir;   // the player's CD folder, only ever read
    std::string outDir;  // where the finished cache goes
    // Called once per stage with a line to show the player. The default
    // writes to stdout.
    std::function<void(const std::string&)> progress;
};

// What one run produced.
struct ExtractResult {
    // The nine counts catalog.json records, keyed the way it keys them.
    std::map<std::string, std::int64_t> counts;
    // The names of the counts that did not match the expected value.
    std::vector<std::string> mismatched;
    double seconds = 0.0;
    bool ok = false;
};

// The counts a complete CD produces. tools/extract carries the same numbers.
const std::map<std::string, std::int64_t>& expectedCounts();

// Returns the CD files the extractor reads and cannot do without. The caller
// checks these before starting so a missing file stops the run early.
std::vector<std::string> requiredCdFiles();

// Names the required files that `cdDir` does not hold.
std::vector<std::string> missingCdFiles(const std::string& cdDir);

// Runs the whole extraction. Throws std::runtime_error when a file cannot be
// read or written. Returns with `ok` false when a count came out wrong, and in
// that case the output directory is left as it was.
ExtractResult runExtract(const ExtractOptions& options);

}  // namespace swchess::exporter
