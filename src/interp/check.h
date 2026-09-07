// Verifies one finished interp60 directory against its own manifest.
//
// This is the C++ port of tools/interp/check.py. The checks are the ones
// docs/plan.md section 3 asks for. Times are compared as exact fractions
// rebuilt from the decimal the manifest holds, so the duration total has to
// equal end_ms on the nose rather than merely come close.
#pragma once

#include <string>
#include <vector>

namespace swchess::interp {

// Everything one run of the checks found.
struct CheckReport {
    std::vector<std::string> notes;     // what passed
    std::vector<std::string> problems;  // what failed, empty when the run passed
    bool passed() const { return problems.empty(); }
};

// Runs every check over the directory at `outDir`. `full` reads each frame's
// pixels back for the pose copy comparison, which is the slow part.
CheckReport runChecks(const std::string& outDir, bool full);

}  // namespace swchess::interp
