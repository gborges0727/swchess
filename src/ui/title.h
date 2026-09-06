// The five screens that run before the board appears.
//
// FUN_1070_01f8 loads TITLERES.DLL and creates a TitleWindow at 0, 0 sized 674
// by 512 over a black brush. A word at 11d8:93d8 says which screen shows, and
// WM_TIMER counts elapsed milliseconds against a duration table at 11d8:32e4.
// When the count passes the duration the state advances, and state 5 closes the
// window. A key press, a left click and a right click all end the current
// screen early.
//
// | State | Screen         | Art or strings      | Font    | Duration |
// | 0     | Toolworks logo | STLGO16             | none    | 3000 ms  |
// | 1     | Legal notice   | LEGAL               | none    | 3000 ms  |
// | 2     | Opening crawl  | ids 14001 upward    | LEGFONT | 60000 ms |
// | 3     | Title          | STARTITL            | none    | 5000 ms  |
// | 4     | Credit roll    | ids 15001 upward    | LEGFONT | 140000 ms|
//
// The crawl and the credit roll share their layout. Id 14000 and id 15000 hold
// the line count. Each following id is one line, rendered through LEGFONT, and
// the whole block shifts right by (640 - widest line) / 2, so the block is
// centred and the individual lines are not. The block scrolls
// 42 * lines + 240 pixels, where 42 is the LEGFONT cell height.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets/bmp.h"
#include "text/font.h"
#include "text/strings.h"
#include "ui/draw.h"
#include "ui/res_bitmap.h"

namespace swchess::ui {

enum class TitleState {
    ToolworksLogo = 0,
    Legal = 1,
    Crawl = 2,
    Title = 3,
    Credits = 4,
    Finished = 5,
};

inline constexpr int kTitleStateCount = 5;

// The window FUN_1070_01f8 creates.
inline constexpr int kTitleWidth = 674;
inline constexpr int kTitleHeight = 512;

// The 640 by 480 art sits at the top-left corner of that window.
inline constexpr int kTitleArtWidth = 640;
inline constexpr int kTitleArtHeight = 480;

// The LEGFONT cell height, which is the pitch between two scrolling lines.
inline constexpr int kCrawlLinePitch = 42;

// The head start the scroll distance 42 * lines + 240 gives the first line.
inline constexpr int kScrollHeadStart = 240;

// How long each state runs, from the table at 11d8:32e4.
std::int64_t titleDurationMs(TitleState state);

// The bitmap a state draws, or an empty string for the two scrolling screens.
const char* titleBitmapName(TitleState state);

// The WAV the state starts, or nullptr when it starts none. State 2 starts two,
// so ask for both with titleCues.
const char* titleMusicCue(TitleState state);

// Every sound entering `state` starts, in play order.
std::vector<std::string> titleCues(TitleState state);

// Drives the five screens and draws whichever one is showing.
class TitleSequence {
public:
    // Reads the three bitmaps and LEGFONT from `cdDir`/TITLERES.DLL and the
    // crawl and credit lines from the language DLL. Throws std::runtime_error
    // when a file is missing.
    TitleSequence(const std::string& cdDir, text::Language language);

    // Starts at `first` with the clock reading `nowMs`. The second title window
    // the original creates starts at TitleState::Credits, because WM_DESTROY
    // left the state word at 4.
    void start(std::int64_t nowMs, TitleState first = TitleState::ToolworksLogo);

    // Moves the clock to `nowMs` and advances through every state whose
    // duration has run out.
    void advance(std::int64_t nowMs);

    // A key press or either mouse button ends the current screen.
    void skip();

    TitleState state() const { return state_; }
    bool finished() const { return state_ == TitleState::Finished; }

    // How long the current screen has been showing.
    std::int64_t elapsedMs() const { return nowMs_ - stateStartMs_; }

    // The sounds that have come due since the last call, in play order. Taking
    // them empties the list.
    std::vector<std::string> takeCues();

    // How far the block of the current scrolling screen travels, and how far it
    // has travelled. Both read 0 on the three still screens.
    int scrollDistance() const;
    int scrollOffset() const;

    // The lines of the current scrolling screen, as the stored bytes LEGFONT
    // draws. Empty on the three still screens.
    const std::vector<std::string>& lines() const;

    // Draws the current screen onto `canvas`. The canvas keeps whatever size
    // the caller gave it, and the art lands at the top-left corner.
    void render(Image& canvas) const;

private:
    void enter(TitleState state);
    void loadBlock(int countId, std::vector<std::string>* lines, int* margin) const;
    void renderScroll(Image& canvas, const std::vector<std::string>& lines, int margin) const;

    ResourceBitmaps art_;
    text::BitmapFont font_;
    text::StringTable strings_;

    TitleState state_ = TitleState::ToolworksLogo;
    std::int64_t nowMs_ = 0;
    std::int64_t stateStartMs_ = 0;
    std::vector<std::string> cues_;

    std::vector<std::string> crawl_;
    std::vector<std::string> credits_;
    int crawlMargin_ = 0;
    int creditsMargin_ = 0;
};

}  // namespace swchess::ui
