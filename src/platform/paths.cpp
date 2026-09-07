#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>

namespace swchess::platform {
namespace {

// Reads an environment variable. An unset or empty variable reads as an empty
// string, which every caller treats the same way.
std::string env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::string();
    }
    return std::string(value);
}

#if !defined(__APPLE__) && !defined(_WIN32)
// The XDG specification says to ignore a directory value that is not an
// absolute path.
std::string absoluteEnv(const char* name) {
    const std::string value = env(name);
    if (value.empty() || value.front() != '/') {
        return std::string();
    }
    return value;
}
#endif

std::string homeDir() {
#ifdef _WIN32
    const std::string profile = env("USERPROFILE");
    if (!profile.empty()) {
        return profile;
    }
    const std::string drive = env("HOMEDRIVE");
    const std::string path = env("HOMEPATH");
    if (!drive.empty() && !path.empty()) {
        return drive + path;
    }
    return std::string();
#else
    return env("HOME");
#endif
}

// SDL builds a directory under the place the operating system reserves for an
// application. The three functions below fall back to it when the environment
// names no home directory of its own.
std::string prefPath(const char* suffix) {
    char* path = SDL_GetPrefPath("FairLine", "Star Wars Chess");
    if (path == nullptr) {
        return suffix == nullptr ? std::string(".") : std::string(".") + "/" + suffix;
    }
    std::string answer(path);
    SDL_free(path);
    while (answer.size() > 1 && (answer.back() == '/' || answer.back() == '\\')) {
        answer.pop_back();
    }
    if (suffix != nullptr) {
        answer += "/";
        answer += suffix;
    }
    return answer;
}

}  // namespace

std::string configDir() {
#if defined(__APPLE__)
    const std::string home = homeDir();
    if (home.empty()) {
        return prefPath(nullptr);
    }
    return home + "/Library/Application Support/Star Wars Chess";
#elif defined(_WIN32)
    const std::string roaming = env("APPDATA");
    if (roaming.empty()) {
        return prefPath(nullptr);
    }
    return roaming + "\\FairLine\\Star Wars Chess";
#else
    const std::string configHome = absoluteEnv("XDG_CONFIG_HOME");
    if (!configHome.empty()) {
        return configHome + "/swchess";
    }
    const std::string home = homeDir();
    if (home.empty()) {
        return prefPath(nullptr);
    }
    return home + "/.config/swchess";
#endif
}

std::string cacheDir() {
#if defined(__APPLE__)
    const std::string home = homeDir();
    if (home.empty()) {
        return prefPath("assets");
    }
    return home + "/Library/Application Support/Star Wars Chess/assets";
#elif defined(_WIN32)
    const std::string local = env("LOCALAPPDATA");
    if (local.empty()) {
        return prefPath("cache");
    }
    return local + "\\FairLine\\Star Wars Chess\\cache";
#else
    const std::string cacheHome = absoluteEnv("XDG_CACHE_HOME");
    if (!cacheHome.empty()) {
        return cacheHome + "/swchess";
    }
    const std::string home = homeDir();
    if (home.empty()) {
        return prefPath("cache");
    }
    return home + "/.cache/swchess";
#endif
}

std::string dataDir() {
#if defined(__APPLE__) || defined(_WIN32)
    return configDir();
#else
    const std::string dataHome = absoluteEnv("XDG_DATA_HOME");
    if (!dataHome.empty()) {
        return dataHome + "/swchess";
    }
    const std::string home = homeDir();
    if (home.empty()) {
        return prefPath(nullptr);
    }
    return home + "/.local/share/swchess";
#endif
}

bool ensureDir(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    return std::filesystem::is_directory(dir, error);
}

}  // namespace swchess::platform
