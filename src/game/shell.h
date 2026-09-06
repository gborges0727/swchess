// The whole program the player sees: the title sequence, the game screen, the
// menu buttons and the status bar, and the settings file behind them.
//
// GameShell owns a GameSession for the board, a ui::ButtonBar for the six
// buttons and the status bar, and a ui::TitleSequence for the five screens that
// run before and after the game. It opens no window. Feed it a clock, mouse
// positions and keys, ask it to draw, and it composites the 674 by 512 picture
// the original window showed.
//
// docs/research/menus.md section 2 describes the screen flow, section 3 the
// game screen, section 4 the menu tree and section 5 the settings file.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "audio/wav_load.h"
#include "game/session.h"
#include "ui/buttons.h"
#include "ui/settings.h"
#include "ui/title.h"

namespace swchess::game {

// The window FUN_1070_01f8 creates, and the rectangle FUN_1008_4509 then moves
// the main window into.
inline constexpr int kWindowWidth = ui::kTitleWidth;
inline constexpr int kWindowHeight = ui::kTitleHeight;

// Puts the 640 by 480 picture the session composites into the window canvas at
// the top-left corner, then draws the button row and the status bar over it.
void composeGameScreen(Image& canvas, const Image& board, const ui::ButtonBar& bar);

// Where the shell stands. It runs the title screens, then the game, and the
// QUIT OK button runs the credit roll before the program ends.
enum class ShellState { Title, Playing, Credits, Finished };

const char* shellStateName(ShellState state);

struct ShellOptions;

// Where SWC.INI sits for these options.
std::string settingsPathFor(const ShellOptions& options);

// Reads that file. A missing file reads as the defaults.
ui::Settings loadShellSettings(const ShellOptions& options);

// The settings a session starts with: the SWC.INI values with any value the
// command line named on top of them.
Settings sessionSettingsFor(const ShellOptions& options, const ui::Settings& ini);

// The SWC.INI values a bar shows for those session settings, so the toggles
// match the board even when the command line overrode the file.
ui::Settings barSettingsFor(const Settings& live, const ui::Settings& ini);

// The directory SWC.INI lives in, ~/Library/Application Support/Star Wars
// Chess on macOS. It falls back to the working directory when there is no
// home directory to build it from.
std::string defaultConfigDir();

struct ShellOptions {
    std::string cdDir;
    std::string assetsDir;
    // Where SWC.INI lives. An empty string picks defaultConfigDir().
    std::string configDir;
    // Skips the title screens and opens the game screen straight away.
    bool skipTitle = false;
    // A .CMG file or a native .json to open once the game screen appears.
    std::string loadPath;
    // Command line values. Each one given wins over the SWC.INI value.
    std::optional<board::SetId> set;
    std::optional<text::Language> language;
    std::optional<bool> walking;
    std::optional<bool> captures;
    anim::Cadence cadence = anim::Cadence::Interpolated60;
    // Asks the player for a path. `save` is true for SAVE GAME and false for
    // LOAD GAME. An empty answer cancels, and no callback at all leaves both
    // buttons reporting that they have no chooser.
    std::function<std::string(bool save)> chooseFile;
};

class GameShell {
public:
    // Reads SWC.INI, then everything the session, the buttons and the title
    // screens need. Throws std::runtime_error when a CD file is missing.
    explicit GameShell(const ShellOptions& options);

    // Starts the clock. The shell shows the Toolworks logo, or the game screen
    // when the caller asked to skip the title.
    void start(std::int64_t nowMs);

    // Moves the clock and finishes whatever came due.
    void advance(std::int64_t nowMs);

    ShellState state() const { return state_; }
    bool finished() const { return state_ == ShellState::Finished; }

    // The player asked to close the window, with Esc or the window's own
    // close box.
    void quit() { state_ = ShellState::Finished; }

    // The MINIMIZE button fired. The window clears this once it has minimized.
    bool takeMinimize();

    // Mouse events reach the button row first and the board second. Each one
    // answers true when it changed something.
    bool onMouseMove(int x, int y);
    bool onMouseDown(int x, int y, std::int64_t nowMs);
    bool onMouseUp(int x, int y, std::int64_t nowMs);

    // One key. Pass the lowercase character, or 0x1b for Esc. A title screen
    // and a running capture both swallow the key first, the way the original
    // does.
    bool onKey(char key, std::int64_t nowMs);

    // Runs one menu command, the number WM_COMMAND carried.
    void runCommand(int command, std::int64_t nowMs);

    // Draws the whole 674 by 512 window.
    void render(Image& out);

    GameSession& session() { return session_; }
    const GameSession& session() const { return session_; }
    ui::ButtonBar& bar() { return *bar_; }
    const ui::ButtonBar& bar() const { return *bar_; }
    const ui::TitleSequence* title() const { return title_.get(); }

    // The values SWC.INI holds right now, which the toggles keep in step.
    const ui::Settings& settings() const { return ini_; }
    const std::string& settingsPath() const { return settingsPath_; }

    // Reads and writes SWC.INI. LOAD SETTINGS, RESTORE SETTINGS and SAVE
    // SETTINGS call these, and the constructor calls the reader.
    void loadSettingsFile();
    bool saveSettingsFile();

    // Opens a saved game. A path ending in .cmg reads the original format
    // through readCmg and anything else reads the native JSON. Returns false
    // and leaves the reason in the status bar when it cannot.
    bool loadGameFile(const std::string& path);
    // Writes one. A path ending in .cmg writes the original format.
    bool saveGameFile(const std::string& path);

private:
    void applySettings(bool includeBackground);
    void applySoundSettings();
    void setLanguage(text::Language language);
    void setSet(board::SetId set);
    void refreshStatus();
    void setMessageId(int id);
    void setMessageBytes(std::string bytes);
    void startCredits(std::int64_t nowMs);
    void playCue(const std::string& name);
    bool boardClick(int x, int y, std::int64_t nowMs);
    // Writes the reason a click on `square` changed nothing into the status
    // bar. The ids are the ones FUN_1148_031e reaches, 33026 upward.
    void noteRefusedClick(chess::Square square);
    // Hands the move to `color`, which is what SWITCH SIDES and the two setup
    // buttons do. Returns false when the rules module refuses the position.
    bool setSideToMove(chess::Color color);
    std::string chooseFile(bool save);

    ShellOptions options_;
    std::string settingsPath_;
    ui::Settings ini_{};

    GameSession session_;
    std::unique_ptr<ui::ButtonBar> bar_;
    std::unique_ptr<ui::TitleSequence> title_;

    ShellState state_ = ShellState::Title;
    std::int64_t nowMs_ = 0;
    bool minimize_ = false;
    // What the last command or the last refused click left on the bar. It
    // clears as soon as the position changes.
    std::string message_;
    std::string lastFen_;
    // The board picture the window canvas is built from.
    Image board_{};
    // The two loose title WAV files, read from the CD on first use.
    std::map<std::string, audio::Clip> looseClips_;
};

}  // namespace swchess::game
