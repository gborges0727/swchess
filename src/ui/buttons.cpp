#include "ui/buttons.h"

#include <stdexcept>
#include <utility>

namespace swchess::ui {
namespace {

constexpr ButtonStyle kPush = ButtonStyle::Push;
constexpr ButtonStyle kCheck = ButtonStyle::Check;
constexpr ButtonStyle kRadio = ButtonStyle::Radio;

// An empty slot. SUB_1060_01be skips every slot whose command id is -1.
constexpr ButtonSpec kEmpty{kNoCommand, -1, "", kPush};

// Page times ten plus slot gives the string id, so the table repeats it only
// to keep each row readable. Slot 5 of pages 1 to 7 and of page 9 is the same
// BACK button that pops the menu stack.
constexpr ButtonSpec kPages[kPageCount][kSlotCount] = {
    // Page 0, the top level.
    {{1010, 0, "GAME", kPush},
     {1020, 1, "PLAY", kPush},
     {1030, 2, "ACTION", kPush},
     {1040, 3, "MENTOR", kPush},
     {3010, 4, "MIN", kPush},
     {1100, 5, "BACK", kPush}},
    // Page 1, the game menu.
    {{2000, 10, "DEMO", kPush},
     {100, 11, "NGAME", kPush},
     {101, 12, "LGAME", kPush},
     {103, 13, "SGAME", kPush},
     {1050, 14, "SET", kPush},
     {1200, 15, "BACK", kPush}},
    // Page 2, the play menu.
    {{1060, 20, "SPLAY", kPush},
     {1070, 21, "LKFL", kPush},
     {1080, 22, "SETUP", kPush},
     {324, 23, "CAPTUR", kPush},
     kEmpty,
     {1200, 25, "BACK", kPush}},
    // Page 3, the actions menu.
    {{200, 30, "SWITCH", kPush},
     {201, 31, "FORCE", kPush},
     {202, 32, "TBACK", kPush},
     {205, 33, "REPLAY", kPush},
     {209, 34, "DRAW", kPush},
     {1200, 35, "BACK", kPush}},
    // Page 4, the mentor menu.
    {{250, 40, "HINT", kPush},
     {1090, 41, "PLAYLV", kPush},
     kEmpty,
     kEmpty,
     kEmpty,
     {1200, 45, "BACK", kPush}},
    // Page 5, the settings menu.
    {{113, 50, "LOAD", kPush},
     {112, 51, "SAVE", kPush},
     {115, 52, "RSTR", kPush},
     kEmpty,
     kEmpty,
     {1200, 55, "BACK", kPush}},
    // Page 6, the player combinations. One of the three stays pressed.
    {{3000, 60, "HC", kRadio},
     {3001, 61, "HH", kRadio},
     {3002, 62, "CC", kRadio},
     kEmpty,
     kEmpty,
     {1200, 65, "BACK", kPush}},
    // Page 7, look and feel. The first four keep their pressed state.
    {{296, 70, "WT", kCheck},
     {157, 71, "SOUND", kCheck},
     {158, 72, "CAP", kCheck},
     {159, 73, "WALK", kCheck},
     {271, 74, "CHGBRD", kPush},
     {1200, 75, "BACK", kPush}},
    // Page 8, board setup. Its BACK button carries command 452, not 1200.
    {{450, 80, "CLEAR", kPush},
     {451, 81, "NEW", kPush},
     {453, 82, "DONE", kPush},
     {454, 83, "WTM", kRadio},
     {455, 84, "BTM", kRadio},
     {452, 85, "BACK", kPush}},
    // Page 9, the five difficulties. One of the five stays pressed.
    {{460, 90, "NEWCMR", kRadio},
     {461, 91, "EASY", kRadio},
     {462, 92, "MODERT", kRadio},
     {463, 93, "HARD", kRadio},
     {464, 94, "EXPERT", kRadio},
     {1200, 95, "BACK", kPush}},
    // Page 10, the quit confirmation.
    {{119, 100, "OK", kPush},
     {1200, 101, "CANCEL", kPush},
     kEmpty,
     kEmpty,
     kEmpty,
     kEmpty},
    // Pages 11 and 12 exist in the tables but make no buttons.
    {kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty},
    {kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty},
};

// How deep the menu stack at 11d8:83e0 goes.
constexpr std::size_t kStackLimit = 3;

std::string faceName(const ButtonSpec& spec, ButtonFace face) {
    std::string name = spec.bitmapBase;
    switch (face) {
        case ButtonFace::Up:
            return name + "_U";
        case ButtonFace::Down:
            return name + "_D";
        case ButtonFace::Disabled:
            return name + "_I";
    }
    return name + "_U";
}

std::string joinPath(const std::string& dir, const char* name) {
    if (dir.empty() || dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

}  // namespace

const ButtonSpec& buttonSpec(int page, int slot) {
    if (page < 0 || page >= kPageCount || slot < 0 || slot >= kSlotCount) {
        throw std::out_of_range("no such button slot");
    }
    return kPages[page][slot];
}

Rect buttonRect(int slot) {
    return Rect{kButtonOriginX + kButtonPitch * slot, kButtonOriginY, kButtonWidth, kButtonHeight};
}

Rect statusRect() {
    return Rect{kStatusX, kStatusY, kStatusWidth, kStatusHeight};
}

Rect buttonRowRect() {
    return Rect{kButtonOriginX, kButtonOriginY, kStatusX - kButtonOriginX, kButtonHeight};
}

bool isPagePush(int command, int* page) {
    if (command < command::kPushFirst || command > command::kPushLast) {
        return false;
    }
    const int target = (command - command::kPushFirst) / 10;
    if (target < 0 || target >= kPageCount) {
        return false;
    }
    if (page != nullptr) {
        *page = target;
    }
    return true;
}

ButtonBar::ButtonBar(const std::string& cdDir, text::Language language, const Settings& settings)
    : art_(joinPath(cdDir, "XCHESS.EXE")),
      font_(text::loadGuiFont(cdDir)),
      strings_(text::loadStrings(cdDir, language)),
      settings_(settings) {
    for (int page = 0; page < kPageCount; ++page) {
        for (int slot = 0; slot < kSlotCount; ++slot) {
            enabled_[page][slot] = true;
        }
    }
    refreshStatus();
}

void ButtonBar::setPage(int page) {
    if (page < 0 || page >= kPageCount) {
        throw std::out_of_range("no such button page");
    }
    page_ = page;
    stack_.clear();
    hovered_ = kNoSlot;
    armed_ = kNoSlot;
    refreshStatus();
}

int ButtonBar::slotAt(int x, int y) const {
    for (int slot = 0; slot < kSlotCount; ++slot) {
        const ButtonSpec& spec = kPages[page_][slot];
        if (spec.empty()) {
            continue;
        }
        if (buttonRect(slot).contains(x, y)) {
            return slot;
        }
    }
    return kNoSlot;
}

bool ButtonBar::toggleFlag(int stringId) const {
    switch (stringId) {
        case 70:
            return settings_.whiteOnTop();
        case 71:
            return settings_.sounds != 0;
        case 72:
            return settings_.captures != 0;
        case 73:
            return settings_.walking != 0;
        default:
            return false;
    }
}

int ButtonBar::statusIdFor(int page, int slot) const {
    const ButtonSpec& spec = buttonSpec(page, slot);
    if (spec.empty()) {
        return -1;
    }
    if (spec.stringId >= 70 && spec.stringId <= 73 && !toggleFlag(spec.stringId)) {
        return spec.stringId + kToggleOffOffset;
    }
    return spec.stringId;
}

void ButtonBar::refreshStatus() {
    if (hovered_ == kNoSlot) {
        statusBytes_ = messageBytes_;
        return;
    }
    const int id = statusIdFor(page_, hovered_);
    statusBytes_ = std::string(strings_.get(id));
}

int ButtonBar::onMouseMove(int x, int y) {
    const int slot = slotAt(x, y);
    if (slot != hovered_) {
        hovered_ = slot;
        refreshStatus();
    }
    return kNoCommand;
}

int ButtonBar::onMouseDown(int x, int y) {
    onMouseMove(x, y);
    const int slot = slotAt(x, y);
    armed_ = (slot != kNoSlot && enabled_[page_][slot]) ? slot : kNoSlot;
    return kNoCommand;
}

void ButtonBar::applyToggle(int fired) {
    switch (fired) {
        case command::kWhiteOnBottom:
            settings_.setWhiteOnTop(!settings_.whiteOnTop());
            break;
        case command::kMusicToggle:
            settings_.sounds = settings_.sounds != 0 ? 0 : 1;
            break;
        case command::kCapturesToggle:
            settings_.captures = settings_.captures != 0 ? 0 : 1;
            break;
        case command::kWalkingToggle:
            settings_.walking = settings_.walking != 0 ? 0 : 1;
            break;
        case command::kChangeBoard:
            settings_.board = settings_.board != 0 ? 0 : 1;
            break;
        default:
            break;
    }
}

int ButtonBar::onMouseUp(int x, int y) {
    const int armed = armed_;
    armed_ = kNoSlot;
    onMouseMove(x, y);
    const int slot = slotAt(x, y);
    if (armed == kNoSlot || slot != armed) {
        return kNoCommand;
    }
    const ButtonSpec& spec = kPages[page_][slot];
    const int fired = spec.command;

    if (spec.style == ButtonStyle::Check) {
        applyToggle(fired);
    } else if (spec.style == ButtonStyle::Radio) {
        if (fired >= command::kLevelNewcomer && fired <= command::kLevelExpert) {
            settings_.playLevel = fired;
        } else if (fired == command::kWhiteToMove || fired == command::kBlackToMove) {
            sideToMove_ = fired;
        } else {
            players_ = fired;
        }
    } else if (fired == command::kChangeBoard) {
        applyToggle(fired);
    }

    int target = 0;
    if (isPagePush(fired, &target)) {
        if (stack_.size() < kStackLimit) {
            stack_.push_back(page_);
        }
        page_ = target;
        hovered_ = kNoSlot;
    } else if (fired == command::kPop || fired == command::kSetupBack) {
        if (!stack_.empty()) {
            page_ = stack_.back();
            stack_.pop_back();
        } else {
            page_ = 0;
        }
        hovered_ = kNoSlot;
    }

    refreshStatus();
    return fired;
}

bool ButtonBar::pressedState(int page, int slot) const {
    const ButtonSpec& spec = buttonSpec(page, slot);
    if (spec.style == ButtonStyle::Check) {
        return toggleFlag(spec.stringId);
    }
    if (spec.style == ButtonStyle::Radio) {
        if (spec.command >= command::kLevelNewcomer && spec.command <= command::kLevelExpert) {
            return settings_.playLevel == spec.command;
        }
        if (spec.command == command::kWhiteToMove || spec.command == command::kBlackToMove) {
            return sideToMove_ == spec.command;
        }
        return players_ == spec.command;
    }
    return false;
}

ButtonFace ButtonBar::faceOf(int slot) const {
    const ButtonSpec& spec = buttonSpec(page_, slot);
    if (spec.empty()) {
        return ButtonFace::Up;
    }
    if (!enabled_[page_][slot]) {
        return ButtonFace::Disabled;
    }
    if (armed_ == slot) {
        return ButtonFace::Down;
    }
    return pressedState(page_, slot) ? ButtonFace::Down : ButtonFace::Up;
}

void ButtonBar::setEnabled(int page, int slot, bool value) {
    buttonSpec(page, slot);
    enabled_[page][slot] = value;
}

bool ButtonBar::enabled(int page, int slot) const {
    buttonSpec(page, slot);
    return enabled_[page][slot];
}

void ButtonBar::setMessageId(int stringId) {
    setMessageBytes(std::string(strings_.get(stringId)));
}

void ButtonBar::setMessageBytes(std::string bytes) {
    messageBytes_ = std::move(bytes);
    if (hovered_ == kNoSlot) {
        statusBytes_ = messageBytes_;
    }
}

std::string ButtonBar::statusText() const {
    return text::toUtf8(text::FontKind::Guitext, statusBytes_);
}

void ButtonBar::setSettings(const Settings& settings) {
    settings_ = settings;
    refreshStatus();
}

void ButtonBar::setPlayers(int fired) {
    players_ = fired;
}

void ButtonBar::setSideToMove(int fired) {
    sideToMove_ = fired;
}

void ButtonBar::render(Image& canvas) const {
    for (int slot = 0; slot < kSlotCount; ++slot) {
        const ButtonSpec& spec = kPages[page_][slot];
        if (spec.empty()) {
            continue;
        }
        const Rect rect = buttonRect(slot);
        const std::string name = faceName(spec, faceOf(slot));
        const std::vector<std::uint8_t> face = art_.rgba(name);
        const IndexedBitmap& bitmap = art_.bitmap(name);
        blitClipped(canvas, face.data(), bitmap.width, bitmap.height, rect.x, rect.y, rect);
    }

    // The status bar is a black plate with a one-pixel border, and the label
    // draws in GUITEXT two pixels in from the left edge. GUITEXT cells stand
    // 15 pixels tall and the bar is 12, so a descender clips the way it does in
    // the original.
    const Rect bar = statusRect();
    fillRect(canvas, bar, 0, 0, 0);
    strokeRect(canvas, bar, 96, 96, 96);
    if (!statusBytes_.empty()) {
        const text::TextImage line = text::render(font_, statusBytes_);
        const int top = bar.y + (bar.height - line.height) / 2;
        blitClipped(canvas, line.rgba.data(), line.width, line.height, bar.x + 2, top, bar);
    }
}

}  // namespace swchess::ui
