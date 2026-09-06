#include "assets/ini.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "assets/ne.h"

namespace swchess {
namespace {

std::string lowered(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string trimmed(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() && isSpace(text[first])) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && isSpace(text[last - 1])) {
        --last;
    }
    return text.substr(first, last - first);
}

}  // namespace

void IniSection::add(std::string key, std::string value) {
    keys_.push_back(key);
    values_.emplace_back(lowered(key), std::move(value));
}

bool IniSection::has(const std::string& key) const {
    const std::string want = lowered(key);
    for (const auto& pair : values_) {
        if (pair.first == want) {
            return true;
        }
    }
    return false;
}

std::string IniSection::get(const std::string& key, const std::string& fallback) const {
    const std::string want = lowered(key);
    // A repeated key wins with its last value, the way the profile reader that
    // rewrites the file leaves only the last one behind.
    const std::string* found = nullptr;
    for (const auto& pair : values_) {
        if (pair.first == want) {
            found = &pair.second;
        }
    }
    return found != nullptr ? *found : fallback;
}

IniFile::IniFile(const std::string& path) : path_(path) {
    std::vector<std::uint8_t> blob = readBinaryFile(path);
    std::string text(reinterpret_cast<const char*>(blob.data()), blob.size());

    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t lineEnd = text.find('\n', at);
        if (lineEnd == std::string::npos) {
            lineEnd = text.size();
        }
        std::string line = trimmed(text.substr(at, lineEnd - at));
        at = lineEnd + 1;
        if (line.empty() || line[0] == ';') {
            if (lineEnd == text.size()) {
                break;
            }
            continue;
        }
        if (line[0] == '[') {
            std::size_t close = line.find(']');
            if (close != std::string::npos) {
                sections_.emplace_back(trimmed(line.substr(1, close - 1)));
            }
        } else if (!sections_.empty()) {
            std::size_t equals = line.find('=');
            if (equals != std::string::npos) {
                sections_.back().add(trimmed(line.substr(0, equals)),
                                     trimmed(line.substr(equals + 1)));
            }
        }
        if (lineEnd == text.size()) {
            break;
        }
    }
}

const IniSection* IniFile::section(const std::string& name) const {
    const std::string want = lowered(name);
    for (const IniSection& section : sections_) {
        if (lowered(section.name()) == want) {
            return &section;
        }
    }
    return nullptr;
}

std::vector<std::string> iniTokens(const std::string& value) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < value.size()) {
        while (at < value.size() && isSpace(value[at])) {
            ++at;
        }
        std::size_t start = at;
        while (at < value.size() && !isSpace(value[at])) {
            ++at;
        }
        if (at > start) {
            out.push_back(value.substr(start, at - start));
        }
    }
    return out;
}

int iniAsInt(const std::string& value, int fallback) {
    std::string text = trimmed(value);
    if (text.empty()) {
        return fallback;
    }
    char* stop = nullptr;
    long parsed = std::strtol(text.c_str(), &stop, 10);
    if (stop == nullptr || *stop != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

}  // namespace swchess
