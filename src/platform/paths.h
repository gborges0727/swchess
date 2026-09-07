// Where the game keeps its own files on each operating system.
//
// Three directories answer three different questions. configDir() holds
// SWC.INI and the startup configuration file. cacheDir() holds the decoded
// artwork, which the player can delete and the game can rebuild. dataDir()
// holds saved games.
//
// None of these functions creates a directory. Call ensureDir() before you
// write into one.
//
// docs/research/packaging-cross-platform.md section "Shared startup and
// dialogs" and section "Linux build and package" set these locations.
#pragma once

#include <string>

namespace swchess::platform {

// macOS: ~/Library/Application Support/Star Wars Chess
// Windows: %APPDATA%\FairLine\Star Wars Chess
// Linux: $XDG_CONFIG_HOME/swchess, or ~/.config/swchess
std::string configDir();

// macOS: ~/Library/Application Support/Star Wars Chess/assets
// Windows: %LOCALAPPDATA%\FairLine\Star Wars Chess\cache
// Linux: $XDG_CACHE_HOME/swchess, or ~/.cache/swchess
std::string cacheDir();

// macOS: ~/Library/Application Support/Star Wars Chess
// Windows: %APPDATA%\FairLine\Star Wars Chess
// Linux: $XDG_DATA_HOME/swchess, or ~/.local/share/swchess
std::string dataDir();

// Creates `dir` and every parent it needs. Returns false when the operating
// system refuses.
bool ensureDir(const std::string& dir);

}  // namespace swchess::platform
