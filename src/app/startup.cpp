#include "app/startup.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

#include "assets/cdfs.h"
#include "platform/paths.h"

namespace swchess::app {
namespace {

// The name of the file this code writes. The old macOS launcher wrote a file
// called `config` in the same directory, and readStartupConfig still reads
// that one so nobody has to pick their CD folder twice.
constexpr const char* kConfigName = "startup.conf";
constexpr const char* kLegacyConfigName = "config";
constexpr int kConfigVersion = 1;

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    std::filesystem::path path(dir);
    path /= name;
    return path.string();
}

// Drops the trailing separator a folder dialog puts on a directory path, and
// the spaces and line endings a hand-edited configuration file can carry.
std::string tidyPath(std::string path) {
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' ' ||
                            path.back() == '\t')) {
        path.pop_back();
    }
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    return path;
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

// Puts the missing names into one sentence the player can act on.
std::string missingFilesMessage(const std::string& dir,
                                const std::vector<std::string>& missing) {
    std::string message = dir + " is missing ";
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            message += i + 1 == missing.size() ? " and " : ", ";
        }
        message += missing[i];
    }
    message += ". Pick the folder that holds the Star Wars Chess CD files.";
    return message;
}

// What the folder dialog leaves behind. SDL calls the callback on the thread
// that pumps events, so no lock guards these fields.
struct FolderPick {
    bool done = false;
    bool cancelled = false;
    std::string path;
    std::string error;
};

void SDLCALL folderPicked(void* userdata, const char* const* files, int filter) {
    (void)filter;
    FolderPick* pick = static_cast<FolderPick*>(userdata);
    if (files == nullptr) {
        // SDL could not show the dialog at all.
        pick->error = SDL_GetError();
        pick->done = true;
        return;
    }
    if (files[0] == nullptr) {
        pick->cancelled = true;
        pick->done = true;
        return;
    }
    pick->path = tidyPath(files[0]);
    pick->done = true;
}

// Opens the folder dialog and waits for the callback. It keeps painting the
// window black and answers the window's close box, so the program stays
// responsive while the dialog stands in front of it.
FolderPick askForFolder(SDL_Window* window, const std::string& startAt) {
    FolderPick pick;
    SDL_ShowOpenFolderDialog(folderPicked, &pick, window,
                             startAt.empty() ? nullptr : startAt.c_str(), false);
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    while (!pick.done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                pick.cancelled = true;
                pick.done = true;
            }
        }
        if (renderer != nullptr) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
        }
        SDL_Delay(16);
    }
    return pick;
}

}  // namespace

std::string startupConfigDir(const StartupRequest& request) {
    return request.configDir.empty() ? platform::configDir() : request.configDir;
}

const std::vector<std::string>& requiredCdFiles() {
    // XCHESS.EXE is the program the original ran. CC256.DLL holds the piece
    // bitmaps the board draws, and CMWIN.DAT holds the opening book and the
    // engine data. A copy missing any of the three cannot run the game.
    static const std::vector<std::string> files = {"XCHESS.EXE", "CC256.DLL", "CMWIN.DAT"};
    return files;
}

std::vector<std::string> missingCdFiles(const std::string& dir) {
    std::vector<std::string> missing;
    if (dir.empty()) {
        return requiredCdFiles();
    }
    // CdDir indexes the folder by the uppercased form of each name, so a copy
    // that spells the files in lowercase passes the same check. Reading the
    // folder throws when it is not there or the player cannot open it, and
    // then every required name counts as missing.
    try {
        const CdDir names{std::filesystem::path(dir)};
        for (const std::string& wanted : requiredCdFiles()) {
            if (!names.has(wanted)) {
                missing.push_back(wanted);
            }
        }
    } catch (const std::exception&) {
        return requiredCdFiles();
    }
    return missing;
}

bool readStartupConfig(const std::string& dir, StartupConfig* out) {
    if (out == nullptr) {
        return false;
    }
    const std::string path = joinPath(dir, kConfigName);
    std::ifstream file(path);
    if (file) {
        std::string line;
        while (std::getline(file, line)) {
            line = tidyPath(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const std::size_t equals = line.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            const std::string key = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);
            if (key == "cd") {
                out->cdDir = value;
            } else if (key == "assets") {
                out->assetsDir = value;
            }
        }
        return true;
    }

    // The macOS shell launcher wrote two bare lines, the CD folder first and
    // the artwork second. Read that file so an existing installation keeps
    // the folder the player already picked.
    std::ifstream legacy(joinPath(dir, kLegacyConfigName));
    if (!legacy) {
        return false;
    }
    std::string line;
    if (std::getline(legacy, line)) {
        out->cdDir = tidyPath(line);
    }
    if (std::getline(legacy, line)) {
        out->assetsDir = tidyPath(line);
    }
    return true;
}

bool writeStartupConfig(const std::string& dir, const StartupConfig& config) {
    if (!platform::ensureDir(dir)) {
        return false;
    }
    std::ofstream file(joinPath(dir, kConfigName), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    // The file is UTF-8 and holds no byte order mark. Windows reads a path
    // with an accent in it correctly that way, and so do macOS and Linux.
    file << "# Star Wars Chess startup settings. The game writes this file.\n";
    file << "version=" << kConfigVersion << "\n";
    file << "cd=" << config.cdDir << "\n";
    file << "assets=" << config.assetsDir << "\n";
    return static_cast<bool>(file);
}

StartupResult resolveWithoutAsking(const StartupRequest& request) {
    StartupResult result;
    result.cdDir = tidyPath(request.cdDir);
    result.assetsDir = tidyPath(request.assetsDir);

    if (request.readEnvironment) {
        if (result.cdDir.empty()) {
            result.cdDir = tidyPath(env("SWCHESS_CD"));
        }
        if (result.assetsDir.empty()) {
            result.assetsDir = tidyPath(env("SWCHESS_ASSETS"));
        }
    }

    if (result.cdDir.empty() || result.assetsDir.empty()) {
        StartupConfig saved;
        if (readStartupConfig(startupConfigDir(request), &saved)) {
            if (result.cdDir.empty()) {
                result.cdDir = tidyPath(saved.cdDir);
            }
            if (result.assetsDir.empty()) {
                result.assetsDir = tidyPath(saved.assetsDir);
            }
        }
    }

    // Nothing names the artwork, so use the cache directory for this system.
    // A cache with no catalog.json in it plays the original 120 ms poses.
    if (result.assetsDir.empty()) {
        result.assetsDir = platform::cacheDir();
        result.shouldSave = true;
    }

    if (result.cdDir.empty()) {
        result.status = StartupStatus::Failed;
        result.message =
            "No CD folder is set. Pass --cd, set SWCHESS_CD, or start the game with a window "
            "so it can ask.";
        return result;
    }

    const std::vector<std::string> missing = missingCdFiles(result.cdDir);
    if (!missing.empty()) {
        result.status = StartupStatus::Failed;
        result.message = missingFilesMessage(result.cdDir, missing);
        return result;
    }

    result.status = StartupStatus::Ready;
    return result;
}

void showStartupMessage(SDL_Window* window, const std::string& message) {
    std::fprintf(stderr, "%s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Star Wars Chess", message.c_str(), window);
}

StartupResult resolveStartup(const StartupRequest& request, SDL_Window* window) {
    StartupResult result = resolveWithoutAsking(request);
    if (result.status == StartupStatus::Ready) {
        return result;
    }
    if (window == nullptr) {
        return result;
    }

    // Every trip round this loop shows the reason the last folder did not
    // work, then asks again. The player can stop by closing the dialog.
    std::string startAt = result.cdDir;
    while (true) {
        showStartupMessage(window, result.message);
        const FolderPick pick = askForFolder(window, startAt);
        if (!pick.error.empty()) {
            result.status = StartupStatus::Failed;
            result.message = "The folder chooser did not open: " + pick.error;
            return result;
        }
        if (pick.cancelled) {
            result.status = StartupStatus::Cancelled;
            result.message.clear();
            return result;
        }

        const std::vector<std::string> missing = missingCdFiles(pick.path);
        if (missing.empty()) {
            result.status = StartupStatus::Ready;
            result.cdDir = pick.path;
            result.message.clear();
            result.shouldSave = true;
            return result;
        }
        result.message = missingFilesMessage(pick.path, missing);
        startAt = pick.path;
    }
}

void offerExtraction(const std::string& cdDir, const std::string& assetsDir) {
    // The native extractor is not written yet, so this does nothing today.
    // The game runs without a cache and plays the authored poses at their
    // original 120 ms cadence.
    (void)cdDir;
    (void)assetsDir;
}

}  // namespace swchess::app
