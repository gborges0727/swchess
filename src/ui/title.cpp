#include "ui/title.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "assets/cdfs.h"

namespace swchess::ui {
namespace {

constexpr std::int64_t kDurations[kTitleStateCount] = {3000, 3000, 60000, 5000, 140000};

// The first id of each block, and the id that holds its line count.
constexpr int kCrawlCountId = 14000;
constexpr int kCreditsCountId = 15000;

int parseCount(std::string_view bytes) {
    int value = 0;
    bool any = false;
    for (char byte : bytes) {
        if (byte < '0' || byte > '9') {
            break;
        }
        value = value * 10 + (byte - '0');
        any = true;
    }
    return any ? value : 0;
}

}  // namespace

std::int64_t titleDurationMs(TitleState state) {
    const int index = static_cast<int>(state);
    if (index < 0 || index >= kTitleStateCount) {
        return 0;
    }
    return kDurations[index];
}

const char* titleBitmapName(TitleState state) {
    switch (state) {
        case TitleState::ToolworksLogo:
            return "STLGO16";
        case TitleState::Legal:
            return "LEGAL";
        case TitleState::Title:
            return "STARTITL";
        default:
            return "";
    }
}

const char* titleMusicCue(TitleState state) {
    switch (state) {
        case TitleState::Crawl:
        case TitleState::Credits:
            return "SWTHEME.WAV";
        default:
            return nullptr;
    }
}

std::vector<std::string> titleCues(TitleState state) {
    switch (state) {
        case TitleState::Crawl:
            return {"STWPRES.WAV", "SWTHEME.WAV"};
        case TitleState::Credits:
            return {"SWTHEME.WAV"};
        case TitleState::Finished:
            // WM_DESTROY plays ENERGIZE.WAV out of SWCAUDIO.DLL as the title
            // window closes.
            return {"ENERGIZE.WAV"};
        default:
            return {};
    }
}

TitleSequence::TitleSequence(const std::string& cdDir, text::Language language)
    : art_(resolveCdFile(cdDir, "TITLERES.DLL").string()),
      font_(text::loadLegFont(cdDir)),
      strings_(text::loadStrings(cdDir, language)) {
    loadBlock(kCrawlCountId, &crawl_, &crawlMargin_);
    loadBlock(kCreditsCountId, &credits_, &creditsMargin_);
}

void TitleSequence::loadBlock(int countId, std::vector<std::string>* lines, int* margin) const {
    const int count = parseCount(strings_.get(countId));
    lines->clear();
    int widest = 0;
    for (int line = 0; line < count; ++line) {
        std::string bytes(strings_.get(countId + 1 + line));
        widest = std::max(widest, text::measure(font_, bytes));
        lines->push_back(std::move(bytes));
    }
    *margin = std::max(0, (kTitleArtWidth - widest) / 2);
}

void TitleSequence::start(std::int64_t nowMs, TitleState first) {
    nowMs_ = nowMs;
    cues_.clear();
    state_ = first;
    stateStartMs_ = nowMs;
    for (std::string& cue : titleCues(first)) {
        cues_.push_back(std::move(cue));
    }
}

void TitleSequence::enter(TitleState next) {
    state_ = next;
    stateStartMs_ = nowMs_;
    for (std::string& cue : titleCues(next)) {
        cues_.push_back(std::move(cue));
    }
}

void TitleSequence::advance(std::int64_t nowMs) {
    nowMs_ = std::max(nowMs_, nowMs);
    while (state_ != TitleState::Finished && elapsedMs() >= titleDurationMs(state_)) {
        enter(static_cast<TitleState>(static_cast<int>(state_) + 1));
    }
}

void TitleSequence::skip() {
    if (state_ == TitleState::Finished) {
        return;
    }
    enter(static_cast<TitleState>(static_cast<int>(state_) + 1));
}

std::vector<std::string> TitleSequence::takeCues() {
    std::vector<std::string> taken;
    taken.swap(cues_);
    return taken;
}

const std::vector<std::string>& TitleSequence::lines() const {
    static const std::vector<std::string> none;
    if (state_ == TitleState::Crawl) {
        return crawl_;
    }
    if (state_ == TitleState::Credits) {
        return credits_;
    }
    return none;
}

int TitleSequence::scrollDistance() const {
    const std::vector<std::string>& block = lines();
    if (block.empty()) {
        return 0;
    }
    return kCrawlLinePitch * static_cast<int>(block.size()) + kScrollHeadStart;
}

int TitleSequence::scrollOffset() const {
    const int distance = scrollDistance();
    if (distance == 0) {
        return 0;
    }
    const std::int64_t duration = titleDurationMs(state_);
    if (duration <= 0) {
        return distance;
    }
    const std::int64_t travelled = static_cast<std::int64_t>(distance) * elapsedMs() / duration;
    return static_cast<int>(std::clamp<std::int64_t>(travelled, 0, distance));
}

void TitleSequence::renderScroll(Image& canvas, const std::vector<std::string>& block,
                                 int margin) const {
    const Rect clip{0, 0, canvas.width, canvas.height};
    const int pitch = kCrawlLinePitch;
    const int offset = scrollOffset();
    for (std::size_t line = 0; line < block.size(); ++line) {
        if (block[line].empty()) {
            continue;
        }
        const int top = kScrollHeadStart + pitch * static_cast<int>(line) - offset;
        if (top + pitch <= 0 || top >= canvas.height) {
            continue;
        }
        const text::TextImage image = text::render(font_, block[line]);
        blitClipped(canvas, image.rgba.data(), image.width, image.height, margin, top, clip);
    }
}

void TitleSequence::render(Image& canvas) const {
    fillRect(canvas, Rect{0, 0, canvas.width, canvas.height}, 0, 0, 0);
    if (state_ == TitleState::Finished) {
        return;
    }
    const char* name = titleBitmapName(state_);
    if (name[0] != '\0') {
        const IndexedBitmap& bitmap = art_.bitmap(name);
        const std::vector<std::uint8_t> pixels = art_.rgba(name);
        blitClipped(canvas, pixels.data(), bitmap.width, bitmap.height, 0, 0,
                    Rect{0, 0, canvas.width, canvas.height});
        return;
    }
    if (state_ == TitleState::Crawl) {
        renderScroll(canvas, crawl_, crawlMargin_);
    } else if (state_ == TitleState::Credits) {
        renderScroll(canvas, credits_, creditsMargin_);
    }
}

}  // namespace swchess::ui
