// Writes JSON byte for byte the way Python's json.dump writes it.
//
// tools/extract and tools/interp both call json.dump(obj, fh, indent=1), and
// tools/extract/locales.py adds ensure_ascii=False. The native helpers have to
// produce the same bytes so a run of swchess-extract can be diffed against a
// run of the Python tool.
//
// nlohmann's own dump differs from Python in two ways. It prints doubles with
// its own shortest-round-trip formatter, which does not always agree with
// Python's repr, and it escapes a different set of characters. So this file
// keeps nlohmann for building and parsing the tree and writes the text itself.
//
// Every object here is nlohmann::ordered_json, which keeps keys in insertion
// order. Python dictionaries keep insertion order too, and neither tool sorts
// its keys, so insertion order is the contract.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace swchess::exporter {

// Keys stay in the order the code inserts them, the way a Python dict does.
using Json = nlohmann::ordered_json;

// Formats one double the way Python's repr does. Python prints the shortest
// decimal that reads back as the same double, adds ".0" to a whole number, and
// switches to exponent form outside the range 1e-4 to 1e16.
std::string pythonFloatRepr(double value);

// Returns the text json.dump(value, indent=1) would write. `ensureAscii` picks
// between the two escaping rules Python offers.
std::string dumpPythonJson(const Json& value, bool ensureAscii);

// Writes that text to `path`. `trailingNewline` adds the "\n" that
// tools/interp writes after its manifest and tools/extract does not.
void writeJsonFile(const std::string& path, const Json& value, bool ensureAscii,
                   bool trailingNewline);

}  // namespace swchess::exporter
