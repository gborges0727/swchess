#include "text/strings.h"

#include <stdexcept>

#include "assets/ne.h"

namespace swchess::text {
namespace {

// Sixteen strings live in one resource, and the first resource holds ids 0 to
// 15.
constexpr int kStringsPerTable = 16;

std::string joinPath(const std::string& dir, const char* name) {
    if (dir.empty()) {
        return name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

}  // namespace

const char* languageFileName(Language language) {
    switch (language) {
        case Language::English:
            return "RESENG.DLL";
        case Language::French:
            return "RESFRN.DLL";
        case Language::German:
            return "RESGER.DLL";
        case Language::Spanish:
            return "RESSPN.DLL";
    }
    throw std::runtime_error("unknown language");
}

const char* languageName(Language language) {
    switch (language) {
        case Language::English:
            return "english";
        case Language::French:
            return "french";
        case Language::German:
            return "german";
        case Language::Spanish:
            return "spanish";
    }
    throw std::runtime_error("unknown language");
}

std::string_view StringTable::get(int id) const {
    auto found = entries.find(id);
    if (found == entries.end()) {
        return {};
    }
    return found->second.bytes;
}

bool StringTable::has(int id) const {
    auto found = entries.find(id);
    return found != entries.end() && found->second.present;
}

std::vector<int> StringTable::ids() const {
    std::vector<int> out;
    out.reserve(entries.size());
    for (const auto& pair : entries) {
        out.push_back(pair.first);
    }
    return out;
}

StringTable loadStrings(const std::string& cdDir, Language language) {
    const std::string path = joinPath(cdDir, languageFileName(language));
    std::vector<std::uint8_t> blob = readBinaryFile(path);
    std::vector<NeResource> resources = readNeResources(blob);

    StringTable table;
    table.language = language;
    for (const NeResource& resource : resources) {
        if (!resource.type.isNumeric || resource.type.id != kRtString) {
            continue;
        }
        if (!resource.name.isNumeric) {
            throw std::runtime_error("string table with a named id in " + path);
        }
        if (resource.offset + resource.length > blob.size()) {
            throw std::runtime_error("string table runs past the end of " + path);
        }
        const int firstId = (resource.name.id - 1) * kStringsPerTable;
        std::size_t at = resource.offset;
        const std::size_t end = resource.offset + resource.length;
        for (int slot = 0; slot < kStringsPerTable; ++slot) {
            StringEntry entry;
            entry.id = firstId + slot;
            if (at < end) {
                const std::size_t length = blob[at];
                if (at + 1 + length > end) {
                    throw std::runtime_error("string runs past its table in " + path);
                }
                entry.present = true;
                if (length > 0) {
                    entry.bytes.assign(reinterpret_cast<const char*>(blob.data() + at + 1), length);
                }
                at += 1 + length;
            }
            table.entries[entry.id] = std::move(entry);
        }
    }
    if (table.entries.empty()) {
        throw std::runtime_error("no string table resources in " + path);
    }
    return table;
}

}  // namespace swchess::text
