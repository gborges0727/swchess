// Reader for the game's INI files.
//
// The original calls GetPrivateProfileString, which ignores case in section and
// key names. This reader does the same and keeps every value as the raw string
// the file holds. Nothing here converts a value to a number on its own.
//
// The parser skips blank lines and lines that start with a semicolon. It trims
// spaces around a section name, a key and a value. A line with no equals sign
// outside any section goes nowhere.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace swchess {

// One INI section with its keys in file order.
class IniSection {
public:
    explicit IniSection(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }
    // The keys in the order the file lists them, spelled as the file spells them.
    const std::vector<std::string>& keys() const { return keys_; }
    std::size_t size() const { return keys_.size(); }

    void add(std::string key, std::string value);
    bool has(const std::string& key) const;
    // Returns the raw value, or `fallback` when the key is absent.
    std::string get(const std::string& key, const std::string& fallback = std::string()) const;

private:
    std::string name_;
    std::vector<std::string> keys_;
    std::vector<std::pair<std::string, std::string>> values_;  // lowered key, raw value
};

// A parsed INI file. Section lookup ignores case.
class IniFile {
public:
    // Reads and parses the file at `path`. Throws std::runtime_error when the
    // file cannot be read.
    explicit IniFile(const std::string& path);

    const std::vector<IniSection>& sections() const { return sections_; }
    // Returns the section, or nullptr when the file has no such section.
    const IniSection* section(const std::string& name) const;

private:
    std::string path_;
    std::vector<IniSection> sections_;
};

// Splits a value on runs of spaces and tabs.
std::vector<std::string> iniTokens(const std::string& value);

// Reads a value as a decimal integer. Returns `fallback` when the text is not
// one. Leading and trailing spaces are allowed, other trailing text is not.
int iniAsInt(const std::string& value, int fallback);

}  // namespace swchess
