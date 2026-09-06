// The row of six bitmap buttons along the bottom of the game screen, and the
// status bar beside it.
//
// XCHESS.EXE builds this row from a 13 page by 6 slot table. Each slot names a
// command id, a bitmap base name and a string id, where the string id is always
// page * 10 + slot. The buttons sit at x = 15 + 41 * slot, y = 445, each 41 by
// 25 pixels, and the BorderStatic status bar sits at 278, 453 sized 342 by 12.
// Only one page shows at a time. A command from 1000 to 1199 pushes the page
// (command - 1000) / 10 onto a stack at most three deep, and command 1200 pops
// it.
//
// The button art lives in XCHESS.EXE as 153 uncompressed 8-bit DIBs. A base
// name such as GAME gets one bitmap per drawing state: GAME_U released, GAME_D
// pressed and GAME_I disabled. CC.DLL never draws a label onto a button, so the
// wording the player reads shows only in the status bar, in the GUITEXT font.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "assets/bmp.h"
#include "text/font.h"
#include "text/strings.h"
#include "ui/draw.h"
#include "ui/res_bitmap.h"
#include "ui/settings.h"

namespace swchess::ui {

// How a button behaves when the player clicks it. The original stores these in
// the low bits of the control style: 0 is a push button, 2 a check button that
// keeps its pressed state, and 9 an auto radio button where exactly one button
// of the group stays pressed.
enum class ButtonStyle { Push, Check, Radio };

// Which face a button draws.
enum class ButtonFace { Up, Down, Disabled };

// The geometry SUB_1060_01be creates the controls with.
inline constexpr int kPageCount = 13;
inline constexpr int kSlotCount = 6;
inline constexpr int kButtonWidth = 41;
inline constexpr int kButtonHeight = 25;
inline constexpr int kButtonOriginX = 15;
inline constexpr int kButtonOriginY = 445;
inline constexpr int kButtonPitch = 41;
inline constexpr int kStatusX = 278;
inline constexpr int kStatusY = 453;
inline constexpr int kStatusWidth = 342;
inline constexpr int kStatusHeight = 12;

// No button fired.
inline constexpr int kNoCommand = -1;
inline constexpr int kNoSlot = -1;

// The command ids the menu tree uses. Anything from kPushFirst to kPushLast
// pushes a page, and kPop pops one.
namespace command {
inline constexpr int kPushFirst = 1000;
inline constexpr int kPushLast = 1199;
inline constexpr int kPop = 1200;
inline constexpr int kNewGame = 100;
inline constexpr int kLoadGame = 101;
inline constexpr int kSaveGame = 103;
inline constexpr int kSaveSettings = 112;
inline constexpr int kLoadSettings = 113;
inline constexpr int kRestoreSettings = 115;
inline constexpr int kQuitOk = 119;
inline constexpr int kMusicToggle = 157;
inline constexpr int kCapturesToggle = 158;
inline constexpr int kWalkingToggle = 159;
inline constexpr int kSwitchSides = 200;
inline constexpr int kForceMove = 201;
inline constexpr int kTakeBackMove = 202;
inline constexpr int kReplayMove = 205;
inline constexpr int kOfferDraw = 209;
inline constexpr int kHint = 250;
inline constexpr int kChangeBoard = 271;
inline constexpr int kWhiteOnBottom = 296;
inline constexpr int kShowCaptured = 324;
inline constexpr int kSetupClear = 450;
inline constexpr int kSetupNew = 451;
inline constexpr int kSetupBack = 452;
inline constexpr int kSetupDone = 453;
inline constexpr int kWhiteToMove = 454;
inline constexpr int kBlackToMove = 455;
inline constexpr int kLevelNewcomer = 460;
inline constexpr int kLevelExpert = 464;
inline constexpr int kDemoMode = 2000;
inline constexpr int kHumanComputer = 3000;
inline constexpr int kHumanHuman = 3001;
inline constexpr int kComputerComputer = 3002;
inline constexpr int kMinimize = 3010;
}  // namespace command

// One slot of the page table. An empty slot leaves `command` at kNoCommand and
// creates no button.
struct ButtonSpec {
    int command = kNoCommand;
    int stringId = -1;
    const char* bitmapBase = "";
    ButtonStyle style = ButtonStyle::Push;

    bool empty() const { return command == kNoCommand; }
};

// The slot at `page`, `slot`. Throws std::out_of_range outside the table.
const ButtonSpec& buttonSpec(int page, int slot);

// Where slot `slot` of the active page draws.
Rect buttonRect(int slot);

// Where the status bar draws.
Rect statusRect();

// The rectangle the page-switch code repaints, left 15, top 445, right 278,
// bottom 470.
Rect buttonRowRect();

// True when this command pushes a page, and which page it pushes.
bool isPagePush(int command, int* page = nullptr);

// The four look-and-feel toggles use string ids 70 to 73 while the setting is
// on. FUN_1060_00fe adds six when the setting is off, so the bar offers to
// switch it back on.
inline constexpr int kToggleOffOffset = 6;

// The row of buttons and the status bar beside it.
//
// The bar owns no window. Feed it mouse positions in canvas pixels, ask it to
// draw onto a canvas, and read the command id it returns when a button fires.
// A press fires on the mouse button coming back up over the same slot, the way
// a Windows push button does.
class ButtonBar {
public:
    // Reads the button art from `cdDir`/XCHESS.EXE, the status font from
    // `cdDir`/CC256.DLL and the labels from the language DLL. Throws
    // std::runtime_error when a file is missing.
    ButtonBar(const std::string& cdDir, text::Language language, const Settings& settings);

    // Which page shows, and the stack under it. Page 0 is the top level.
    int page() const { return page_; }
    const std::vector<int>& pageStack() const { return stack_; }

    // Shows `page` and empties the stack.
    void setPage(int page);

    // The mouse moved to `x`, `y`. Returns kNoCommand always, and updates the
    // hovered slot and the status text.
    int onMouseMove(int x, int y);

    // The left button went down. Returns kNoCommand, and arms the slot under
    // the mouse.
    int onMouseDown(int x, int y);

    // The left button came up. Returns the command id when it comes up over the
    // slot the press armed, and kNoCommand otherwise. A page push, a page pop,
    // a toggle flip and a radio selection all take effect here before the
    // command id comes back.
    int onMouseUp(int x, int y);

    int hoveredSlot() const { return hovered_; }
    int armedSlot() const { return armed_; }

    // Which face slot `slot` of the active page draws right now.
    ButtonFace faceOf(int slot) const;

    // Greys a button out. A disabled button draws its _I face and fires
    // nothing.
    void setEnabled(int page, int slot, bool enabled);
    bool enabled(int page, int slot) const;

    // The engine and the game leave their notices here. Both take the stored
    // bytes the string table holds, not UTF-8.
    void setMessageId(int stringId);
    void setMessageBytes(std::string bytes);

    // What the bar shows right now, as stored bytes and as readable UTF-8.
    const std::string& statusBytes() const { return statusBytes_; }
    std::string statusText() const;

    // The string id the bar would show for a hovered slot, after the toggle
    // rule turns id 70 to 73 into 76 to 79 for a switched-off setting.
    int statusIdFor(int page, int slot) const;

    // The settings the toggles and the radio groups keep. The caller seeds
    // these at startup and re-reads them after a click.
    const Settings& settings() const { return settings_; }
    void setSettings(const Settings& settings);

    // Which player combination button of page 6 is pressed, one of
    // command::kHumanComputer, kHumanHuman or kComputerComputer.
    int players() const { return players_; }
    void setPlayers(int command);

    // Which side-to-move button of page 8 is pressed, command::kWhiteToMove or
    // command::kBlackToMove.
    int sideToMove() const { return sideToMove_; }
    void setSideToMove(int command);

    // Draws the active page's buttons and the status bar onto `canvas`.
    void render(Image& canvas) const;

private:
    bool toggleFlag(int stringId) const;
    void applyToggle(int command);
    bool pressedState(int page, int slot) const;
    int slotAt(int x, int y) const;
    void refreshStatus();

    ResourceBitmaps art_;
    text::BitmapFont font_;
    text::StringTable strings_;
    Settings settings_;

    int page_ = 0;
    std::vector<int> stack_;
    int hovered_ = kNoSlot;
    int armed_ = kNoSlot;
    int players_ = command::kHumanComputer;
    int sideToMove_ = command::kWhiteToMove;
    bool enabled_[kPageCount][kSlotCount];

    // What the bar showed before the mouse entered a button, restored when it
    // leaves. FUN_1060_00fe saves it with WM_GETTEXT and FUN_1060_00cc puts it
    // back.
    std::string messageBytes_;
    std::string statusBytes_;
};

}  // namespace swchess::ui
