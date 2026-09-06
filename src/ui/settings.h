// The seven values Star Wars Chess keeps under [look_feel] in SWC.INI.
//
// The original builds the path as "<program directory>\SWC.INI", reads the file
// at startup and again whenever the player picks LOAD SETTINGS or RESTORE
// SETTINGS, and writes it only from the SAVE SETTINGS button. This module reads
// and writes the same seven keys at a path the caller gives.
//
// The 144-byte CMWIN.DAT blob the original reads just before these keys holds
// the chess engine's own configuration. Nobody has decoded it, so nothing here
// touches it.
#pragma once

#include <string>

namespace swchess::ui {

// Command ids 460 to 464 are the five difficulty buttons, so 464 is EXPERT.
inline constexpr int kPlayLevelNewcomer = 460;
inline constexpr int kPlayLevelExpert = 464;

struct Settings {
    int language = 0;    // index into RESENG, RESGER, RESFRN, RESSPN
    int turn = 0;        // board rotation in degrees
    int walking = 0;     // animate pieces walking between squares
    int captures = 0;    // play the capture animation
    int sounds = 0;      // play music and effects
    int playLevel = kPlayLevelExpert;  // the command id of the pressed difficulty
    int board = 0;       // 0 draws thron256.bmp, non-zero draws space256.bmp

    // True when the board rotation puts white at the top. The original stores
    // this as `turn % 360 != 0` and uses it to pick the chess set.
    bool whiteOnTop() const { return turn % 360 != 0; }
    void setWhiteOnTop(bool onTop) { turn = onTop ? 180 : 0; }

    bool operator==(const Settings& other) const;
    bool operator!=(const Settings& other) const { return !(*this == other); }
};

// The values the code falls back to when a key is missing, which is what the
// original passes to GetPrivateProfileInt.
Settings defaultSettings();

// The values the SWC.INI shipped on the CD carries. It leaves `board` out, so
// that one keeps its default.
Settings shippedSettings();

// Reads [look_feel] out of the INI file at `path`. A missing file, a missing
// section and a key that does not parse as a number all fall back to
// defaultSettings().
Settings loadSettings(const std::string& path);

// Writes [look_feel] to `path`, replacing whatever was there. The keys go out
// in the order the original writes them: language, turn, walking, captures,
// sounds, play_level, board. Throws std::runtime_error when the file cannot be
// written.
void saveSettings(const std::string& path, const Settings& settings);

}  // namespace swchess::ui
