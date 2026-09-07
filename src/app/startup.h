// Finds the two directories the game cannot start without: the folder holding
// the original CD files, and the folder holding the decoded artwork.
//
// A shell script did this on macOS. The game binary does it now, so the same
// code serves macOS, Windows and Linux.
//
// Four sources answer the question, and the first one that answers wins:
//
//   1. the command line, --cd and --assets
//   2. the environment, SWCHESS_CD and SWCHESS_ASSETS
//   3. the configuration file in swchess::platform::configDir()
//   4. a folder dialog the player picks the CD folder in
//
// Only resolveStartup() opens a dialog. A headless run calls
// resolveWithoutAsking(), which fails with a message instead.
//
// docs/research/packaging-cross-platform.md section "Shared startup and
// dialogs" describes the steps this file implements.
#pragma once

#include <string>
#include <vector>

struct SDL_Window;

namespace swchess::app {

// What the caller already knows before startup runs.
struct StartupRequest {
    // The values --cd, --assets and --config carried, empty when the player
    // named none.
    std::string cdDir;
    std::string assetsDir;
    std::string configDir;
    // Set this to skip the environment, which the test does so a stray
    // SWCHESS_CD on the machine cannot change the answer.
    bool readEnvironment = true;
};

enum class StartupStatus {
    // Both directories are ready and the game can start.
    Ready,
    // The player closed the folder dialog without picking anything. The
    // program exits quietly with a zero code.
    Cancelled,
    // Something went wrong. `message` says what.
    Failed,
};

struct StartupResult {
    StartupStatus status = StartupStatus::Failed;
    std::string cdDir;
    std::string assetsDir;
    // Why it failed, or why the CD folder was rejected.
    std::string message;
    // True when the caller should write the answer back to the configuration
    // file, which is the case whenever a dialog or a default filled a value in.
    bool shouldSave = false;
};

// The two directories the configuration file holds.
struct StartupConfig {
    std::string cdDir;
    std::string assetsDir;
};

// The directory the configuration file lives in. That is `request.configDir`
// when the player named one, and swchess::platform::configDir() otherwise.
std::string startupConfigDir(const StartupRequest& request);

// Reads the configuration file in `dir`. It reads the versioned file written
// by writeStartupConfig, and it also reads the two-line file the old macOS
// launcher wrote. Returns false when neither file is there.
bool readStartupConfig(const std::string& dir, StartupConfig* out);

// Writes the versioned file into `dir`, creating the directory when it is
// missing. Returns false when the write fails.
bool writeStartupConfig(const std::string& dir, const StartupConfig& config);

// Names the files a folder must hold to count as the Star Wars Chess CD.
// The check ignores the case of the names, because some copies of the CD are
// all uppercase and some are all lowercase.
const std::vector<std::string>& requiredCdFiles();

// Lists the required files that `dir` does not hold. An empty list means the
// folder is a usable CD folder. A folder that is not there at all comes back
// with every name in it.
std::vector<std::string> missingCdFiles(const std::string& dir);

// Reads the command line, the environment and the configuration file, and
// stops there. It opens no dialog, so a headless run and the test can both
// call it. The status is Failed when nothing named a usable CD folder.
StartupResult resolveWithoutAsking(const StartupRequest& request);

// The whole thing. It runs resolveWithoutAsking first, and when that comes up
// empty or names a folder that has moved, it asks the player with
// SDL_ShowOpenFolderDialog and validates what comes back. `window` must
// already be on screen, and this function must run on the thread that created
// it. It keeps pumping SDL events while the dialog is open.
StartupResult resolveStartup(const StartupRequest& request, SDL_Window* window);

// Shows one message in a native dialog box, and prints it to stderr too.
// `window` may be null before the window exists.
void showStartupMessage(SDL_Window* window, const std::string& message);

// Builds the decoded artwork the game plays at 60 frames per second. Nothing
// happens yet. The native extractor lands in another change, and this is
// where the game will call it. Until then a cache with no catalog.json simply
// plays the original 120 ms poses.
void offerExtraction(const std::string& cdDir, const std::string& assetsDir);

}  // namespace swchess::app
