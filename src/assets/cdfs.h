#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace swchess {

// The original CD stores every file under an uppercase name. A copy made on a
// case-preserving filesystem, or unpacked by a tool that lowercases, still
// holds the same bytes under a different spelling. CdDir indexes one directory
// by the uppercased form of each name so a reader can ask for "CM.INI" and get
// back whatever the copy actually calls it.
//
// The index never renames anything on the player's disk.
class CdDir {
  public:
    CdDir() = default;

    explicit CdDir(const std::filesystem::path& directory) : directory_(directory) {
        std::error_code error;
        std::filesystem::directory_iterator entries(directory, error);
        if (error) {
            throw std::runtime_error("cannot read the CD directory " + directory.string() + ": " +
                                     error.message());
        }
        for (const std::filesystem::directory_entry& entry : entries) {
            add(entry.path().filename().string());
        }
    }

    // Builds the index from a list of names instead of reading a directory.
    // The test uses this to check the ambiguous case on macOS and Windows,
    // where the filesystem cannot hold both CM.INI and cm.ini at once.
    CdDir(const std::filesystem::path& directory, const std::vector<std::string>& names)
        : directory_(directory) {
        for (const std::string& name : names) {
            add(name);
        }
    }

    const std::filesystem::path& directory() const { return directory_; }

    bool has(std::string_view name) const {
        return byUpperName_.find(upper(name)) != byUpperName_.end();
    }

    // Returns the real path for a name written in any case. Throws when the
    // directory holds no such name, and throws when it holds two names that
    // differ only by case, because then no single answer is right.
    std::filesystem::path resolve(std::string_view name) const {
        std::string key = upper(name);
        auto clash = ambiguous_.find(key);
        if (clash != ambiguous_.end()) {
            throw std::runtime_error("the CD directory " + directory_.string() + " holds " +
                                     clash->second +
                                     ", two names that differ only by case, so " +
                                     std::string(name) + " has no single match");
        }
        auto found = byUpperName_.find(key);
        if (found == byUpperName_.end()) {
            throw std::runtime_error("the CD directory " + directory_.string() + " has no " +
                                     std::string(name));
        }
        return directory_ / found->second;
    }

  private:
    void add(const std::string& name) {
        std::string key = upper(name);
        auto placed = byUpperName_.emplace(key, name);
        if (!placed.second && placed.first->second != name) {
            ambiguous_.emplace(key, placed.first->second + " and " + name);
        }
    }

    static std::string upper(std::string_view text) {
        std::string out(text);
        for (char& c : out) {
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        return out;
    }

    std::filesystem::path directory_;
    std::map<std::string, std::string> byUpperName_;
    std::map<std::string, std::string> ambiguous_;
};

// Builds one CdDir per directory and keeps it, so a reader that holds only the
// directory string pays the scan once. Several threads load assets at the same
// time, so a mutex guards the cache.
inline const CdDir& sharedCdDir(const std::string& cdDir) {
    static std::mutex lock;
    static std::map<std::string, CdDir> cache;
    std::lock_guard<std::mutex> guard(lock);
    auto found = cache.find(cdDir);
    if (found == cache.end()) {
        found = cache.emplace(cdDir, CdDir(std::filesystem::path(cdDir))).first;
    }
    return found->second;
}

// Looks up one CD file by its uppercase name and returns the path that exists.
inline std::filesystem::path resolveCdFile(const std::string& cdDir, std::string_view name) {
    return sharedCdDir(cdDir).resolve(name);
}

}  // namespace swchess
