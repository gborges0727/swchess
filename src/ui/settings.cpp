#include "ui/settings.h"

#include <cstdio>
#include <memory>
#include <stdexcept>

#include "assets/ini.h"

namespace swchess::ui {
namespace {

constexpr const char* kSection = "look_feel";

int readKey(const IniSection* section, const char* key, int fallback) {
    if (section == nullptr || !section->has(key)) {
        return fallback;
    }
    return iniAsInt(section->get(key), fallback);
}

}  // namespace

bool Settings::operator==(const Settings& other) const {
    return language == other.language && turn == other.turn && walking == other.walking &&
           captures == other.captures && sounds == other.sounds && playLevel == other.playLevel &&
           board == other.board;
}

Settings defaultSettings() {
    return Settings{};
}

Settings shippedSettings() {
    Settings settings;
    settings.walking = 1;
    settings.captures = 1;
    settings.sounds = 1;
    return settings;
}

Settings loadSettings(const std::string& path) {
    const Settings fallback = defaultSettings();
    std::unique_ptr<IniFile> file;
    try {
        file = std::make_unique<IniFile>(path);
    } catch (const std::exception&) {
        return fallback;
    }
    const IniSection* section = file->section(kSection);

    Settings settings;
    settings.language = readKey(section, "language", fallback.language);
    settings.turn = readKey(section, "turn", fallback.turn);
    settings.walking = readKey(section, "walking", fallback.walking);
    settings.captures = readKey(section, "captures", fallback.captures);
    settings.sounds = readKey(section, "sounds", fallback.sounds);
    settings.playLevel = readKey(section, "play_level", fallback.playLevel);
    settings.board = readKey(section, "board", fallback.board);
    return settings;
}

void saveSettings(const std::string& path, const Settings& settings) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot write settings to " + path);
    }
    std::fprintf(file, "[%s]\r\n", kSection);
    std::fprintf(file, "language=%d\r\n", settings.language);
    std::fprintf(file, "turn=%d\r\n", settings.turn);
    std::fprintf(file, "walking=%d\r\n", settings.walking);
    std::fprintf(file, "captures=%d\r\n", settings.captures);
    std::fprintf(file, "sounds=%d\r\n", settings.sounds);
    std::fprintf(file, "play_level=%d\r\n", settings.playLevel);
    std::fprintf(file, "board=%d\r\n", settings.board);
    std::fclose(file);
}

}  // namespace swchess::ui
