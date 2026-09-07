// Checks the button bar, the title sequence and the settings file against the
// original CD.
//
// Run it as: ui_test <cd dir> <output dir>
// It writes one PPM per title state and one per rendered button page into the
// output directory, so a human can look at what the module draws.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "ui/buttons.h"
#include "ui/res_bitmap.h"
#include "ui/settings.h"
#include "ui/title.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
        ++failures;
    }
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

void writePpm(const std::string& path, const swchess::Image& image) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        check(false, "cannot write " + path);
        return;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", image.width, image.height);
    std::vector<unsigned char> row(static_cast<std::size_t>(image.width) * 3u);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const unsigned char* pixel =
                &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                             static_cast<std::size_t>(x)) *
                            4u];
            row[static_cast<std::size_t>(x) * 3u + 0] = pixel[0];
            row[static_cast<std::size_t>(x) * 3u + 1] = pixel[1];
            row[static_cast<std::size_t>(x) * 3u + 2] = pixel[2];
        }
        std::fwrite(row.data(), 1, row.size(), file);
    }
    std::fclose(file);
}

// The colour the canvas starts as, so an unpainted pixel stands out.
constexpr unsigned char kGroundRed = 255;
constexpr unsigned char kGroundGreen = 0;
constexpr unsigned char kGroundBlue = 255;

swchess::Image groundCanvas(int width, int height) {
    swchess::Image image = swchess::ui::makeCanvas(width, height);
    swchess::ui::fillRect(image, swchess::ui::Rect{0, 0, width, height}, kGroundRed, kGroundGreen,
                          kGroundBlue);
    return image;
}

bool pixelIs(const swchess::Image& image, int x, int y, const unsigned char* rgb) {
    const unsigned char* pixel =
        &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(x)) *
                    4u];
    return pixel[0] == rgb[0] && pixel[1] == rgb[1] && pixel[2] == rgb[2];
}

// Every pixel of one button rectangle must equal the face bitmap the slot
// draws, and the pixel just outside the row must still be the ground colour.
void checkButtonRect(const swchess::Image& canvas, const swchess::ui::ResourceBitmaps& art,
                     int page, int slot, swchess::ui::ButtonFace face, const std::string& label) {
    const swchess::ui::ButtonSpec& spec = swchess::ui::buttonSpec(page, slot);
    std::string name = spec.bitmapBase;
    name += face == swchess::ui::ButtonFace::Down
                ? "_D"
                : (face == swchess::ui::ButtonFace::Disabled ? "_I" : "_U");
    const swchess::IndexedBitmap& bitmap = art.bitmap(name);
    const std::vector<std::uint8_t> pixels = art.rgba(name);
    const swchess::ui::Rect rect = swchess::ui::buttonRect(slot);
    check(rect.x == 15 + 41 * slot && rect.y == 445 && rect.width == 41 && rect.height == 25,
          label + " rectangle geometry");

    int wrong = 0;
    for (int y = 0; y < rect.height; ++y) {
        for (int x = 0; x < rect.width; ++x) {
            const unsigned char* want =
                &pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) +
                         static_cast<std::size_t>(x)) *
                        4u];
            if (!pixelIs(canvas, rect.x + x, rect.y + y, want)) {
                ++wrong;
            }
        }
    }
    check(wrong == 0, label + " draws " + name + " (" + std::to_string(wrong) + " wrong pixels)");
}

void checkPage(const swchess::Image& canvas, const swchess::ui::ResourceBitmaps& art,
               const swchess::ui::ButtonBar& bar, int page) {
    const std::string pageLabel = "page " + std::to_string(page);
    for (int slot = 0; slot < swchess::ui::kSlotCount; ++slot) {
        const swchess::ui::ButtonSpec& spec = swchess::ui::buttonSpec(page, slot);
        const swchess::ui::Rect rect = swchess::ui::buttonRect(slot);
        const std::string label = pageLabel + " slot " + std::to_string(slot);
        if (spec.empty()) {
            const unsigned char ground[3] = {kGroundRed, kGroundGreen, kGroundBlue};
            check(pixelIs(canvas, rect.x + rect.width / 2, rect.y + rect.height / 2, ground),
                  label + " stays empty");
            continue;
        }
        checkButtonRect(canvas, art, page, slot, bar.faceOf(slot), label);
    }

    // The status bar plate covers its whole rectangle, so no ground colour
    // survives inside it.
    const swchess::ui::Rect bar_rect = swchess::ui::statusRect();
    check(bar_rect.x == 278 && bar_rect.y == 453 && bar_rect.width == 342 && bar_rect.height == 12,
          pageLabel + " status bar geometry");
    int ground = 0;
    for (int y = 0; y < bar_rect.height; ++y) {
        for (int x = 0; x < bar_rect.width; ++x) {
            const unsigned char want[3] = {kGroundRed, kGroundGreen, kGroundBlue};
            if (pixelIs(canvas, bar_rect.x + x, bar_rect.y + y, want)) {
                ++ground;
            }
        }
    }
    check(ground == 0, pageLabel + " status bar is painted");
}

void testButtons(const std::string& cdDir, const std::string& outDir) {
    swchess::ui::ResourceBitmaps art(joinPath(cdDir, "XCHESS.EXE"));
    swchess::ui::Settings settings = swchess::ui::shippedSettings();
    swchess::ui::ButtonBar bar(cdDir, swchess::text::Language::English, settings);

    check(bar.page() == 0, "the bar starts on page 0");
    check(swchess::ui::buttonSpec(0, 0).command == 1010, "page 0 slot 0 pushes page 1");
    check(swchess::ui::buttonSpec(8, 5).command == 452, "page 8 BACK carries command 452");

    // Page 0 with the mouse over slot 2, the ACTIONS MENU button.
    const swchess::ui::Rect slot2 = swchess::ui::buttonRect(2);
    bar.onMouseMove(slot2.x + 5, slot2.y + 5);
    check(bar.hoveredSlot() == 2, "the mouse hovers slot 2");
    check(bar.statusIdFor(0, 2) == 2, "slot 2 of page 0 shows string id 2");
    check(bar.statusText() == "ACTIONS MENU", "the bar reads ACTIONS MENU, not " + bar.statusText());

    swchess::Image page0 = groundCanvas(674, 512);
    bar.render(page0);
    checkPage(page0, art, bar, 0);
    writePpm(joinPath(outDir, "buttons_page0.ppm"), page0);

    // The mouse leaving a button puts the previous message back.
    bar.setMessageId(32776);
    bar.onMouseMove(0, 0);
    check(bar.statusText() == "Check!", "the bar falls back to the engine message");

    // Page 7, the look and feel toggles.
    bar.setPage(7);
    const swchess::ui::Rect slot1 = swchess::ui::buttonRect(1);
    bar.onMouseMove(slot1.x + 5, slot1.y + 5);
    check(bar.statusIdFor(7, 1) == 71, "music on shows string id 71");
    check(bar.statusText() == "MUSIC OFF", "the bar offers to switch music off");
    check(bar.faceOf(1) == swchess::ui::ButtonFace::Down, "the music toggle sits pressed");

    swchess::Image page7 = groundCanvas(674, 512);
    bar.render(page7);
    checkPage(page7, art, bar, 7);
    writePpm(joinPath(outDir, "buttons_page7.ppm"), page7);

    // Clicking the music toggle flips the setting and the wording with it.
    bar.onMouseDown(slot1.x + 5, slot1.y + 5);
    check(bar.faceOf(1) == swchess::ui::ButtonFace::Down, "the armed toggle draws pressed");
    const int fired = bar.onMouseUp(slot1.x + 5, slot1.y + 5);
    check(fired == swchess::ui::command::kMusicToggle, "the music toggle fires command 157");
    check(bar.settings().sounds == 0, "the music setting turns off");
    check(bar.statusIdFor(7, 1) == 77, "music off shows string id 77");
    check(bar.statusText() == "MUSIC ON", "the bar offers to switch music back on");
    check(bar.faceOf(1) == swchess::ui::ButtonFace::Up, "the music toggle sits released");

    // A press that comes up somewhere else fires nothing.
    bar.onMouseDown(slot1.x + 5, slot1.y + 5);
    check(bar.onMouseUp(slot2.x + 5, slot2.y + 5) == swchess::ui::kNoCommand,
          "a press dragged off the button fires nothing");
    check(bar.settings().sounds == 0, "the dragged press changes no setting");

    // The white on bottom toggle reads the board rotation.
    check(bar.statusIdFor(7, 0) == 76, "turn 0 offers WHITE ON TOP");
    const swchess::ui::Rect slot0 = swchess::ui::buttonRect(0);
    bar.onMouseDown(slot0.x + 5, slot0.y + 5);
    check(bar.onMouseUp(slot0.x + 5, slot0.y + 5) == swchess::ui::command::kWhiteOnBottom,
          "the white on bottom toggle fires command 296");
    check(bar.settings().whiteOnTop(), "the board rotation puts white on top");
    check(bar.statusIdFor(7, 0) == 70, "white on top offers WHITE ON BOTTOM");

    // The menu tree pushes and pops.
    bar.setPage(0);
    bar.onMouseDown(slot0.x + 5, slot0.y + 5);
    check(bar.onMouseUp(slot0.x + 5, slot0.y + 5) == 1010, "GAME MENU fires command 1010");
    check(bar.page() == 1, "GAME MENU shows page 1");
    const swchess::ui::Rect slot5 = swchess::ui::buttonRect(5);
    bar.onMouseDown(slot5.x + 5, slot5.y + 5);
    check(bar.onMouseUp(slot5.x + 5, slot5.y + 5) == swchess::ui::command::kPop,
          "BACK fires command 1200");
    check(bar.page() == 0 && bar.pageStack().empty(), "BACK returns to page 0");

    // The five difficulty buttons keep exactly one pressed.
    bar.setPage(9);
    check(bar.faceOf(4) == swchess::ui::ButtonFace::Down, "EXPERT starts pressed");
    const swchess::ui::Rect slot3 = swchess::ui::buttonRect(3);
    bar.onMouseDown(slot3.x + 5, slot3.y + 5);
    check(bar.onMouseUp(slot3.x + 5, slot3.y + 5) == 463, "HARD fires command 463");
    check(bar.settings().playLevel == 463, "HARD becomes the play level");
    check(bar.faceOf(3) == swchess::ui::ButtonFace::Down, "HARD sits pressed");
    check(bar.faceOf(4) == swchess::ui::ButtonFace::Up, "EXPERT sits released");
}

void testTitle(const std::string& cdDir, const std::string& outDir) {
    swchess::ui::TitleSequence title(cdDir, swchess::text::Language::English);
    title.start(0);
    check(title.state() == swchess::ui::TitleState::ToolworksLogo, "the sequence starts on state 0");

    const char* names[5] = {"title_logo.ppm", "title_legal.ppm", "title_crawl.ppm",
                            "title_title.ppm", "title_credits.ppm"};
    const std::int64_t offsets[5] = {0, 3000, 6000, 66000, 71000};
    for (int state = 0; state < 5; ++state) {
        title.advance(offsets[state]);
        check(static_cast<int>(title.state()) == state,
              "state " + std::to_string(state) + " shows at " + std::to_string(offsets[state]) +
                  " ms");
        swchess::Image canvas = swchess::ui::makeCanvas(674, 512);
        title.render(canvas);
        writePpm(joinPath(outDir, names[state]), canvas);
    }

    check(title.scrollDistance() == 42 * static_cast<int>(title.lines().size()) + 240,
          "the credit roll travels 42 lines plus 240 pixels");
    check(title.lines().size() == 189, "the credit roll holds 189 lines");
    title.advance(71000 + 140000);
    check(title.finished(), "the sequence ends after the credit roll");

    // The cues each screen starts.
    swchess::ui::TitleSequence again(cdDir, swchess::text::Language::English);
    again.start(0);
    check(again.takeCues() == std::vector<std::string>{"STWPRES.WAV"},
          "the Toolworks logo starts STWPRES");
    again.advance(3000);
    again.advance(6000);
    const std::vector<std::string> cues = again.takeCues();
    check(cues == std::vector<std::string>{"SWTHEME.WAV"}, "the crawl starts SWTHEME");
    again.skip();
    check(again.state() == swchess::ui::TitleState::Title, "a click ends the crawl early");
    again.skip();
    check(again.takeCues() == std::vector<std::string>{"SWTHEME.WAV"},
          "the credit roll starts SWTHEME");

    // The crawl holds the 27 lines id 14000 counts in English.
    swchess::ui::TitleSequence crawl(cdDir, swchess::text::Language::English);
    crawl.start(0, swchess::ui::TitleState::Crawl);
    check(crawl.lines().size() == 27, "the English crawl holds 27 lines");
    check(crawl.scrollOffset() == 0, "the crawl starts unscrolled");
    crawl.advance(60000 / 2);
    check(crawl.scrollOffset() == crawl.scrollDistance() / 2, "the crawl scrolls with the clock");
}

void testSettings(const std::string& outDir) {
    const swchess::ui::Settings defaults = swchess::ui::defaultSettings();
    check(defaults.playLevel == 464 && defaults.language == 0 && defaults.turn == 0,
          "the defaults match the values GetPrivateProfileInt falls back to");
    const swchess::ui::Settings shipped = swchess::ui::shippedSettings();
    check(shipped.walking == 1 && shipped.captures == 1 && shipped.sounds == 1,
          "the shipped file turns walking, captures and sounds on");

    const std::string path = joinPath(outDir, "SWC.INI");
    std::remove(path.c_str());
    check(swchess::ui::loadSettings(path) == defaults, "a missing file reads as the defaults");

    swchess::ui::Settings written;
    written.language = 2;
    written.turn = 180;
    written.walking = 1;
    written.captures = 0;
    written.sounds = 1;
    written.playLevel = 461;
    written.board = 1;
    swchess::ui::saveSettings(path, written);
    check(swchess::ui::loadSettings(path) == written, "the settings survive a round trip");

    // The file the port writes looks like the one the game reads.
    std::FILE* file = std::fopen(path.c_str(), "rb");
    check(file != nullptr, "the settings file exists");
    if (file != nullptr) {
        std::string text;
        char buffer[256];
        std::size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
            text.append(buffer, got);
        }
        std::fclose(file);
        check(text.find("[look_feel]") == 0, "the file opens with [look_feel]");
        check(text.find("play_level=461") != std::string::npos, "the file carries play_level=461");
        check(text.find("language=2") < text.find("turn=180"), "language comes before turn");
    }

    check(written.whiteOnTop(), "turn 180 puts white on top");
    swchess::ui::Settings flipped = written;
    flipped.setWhiteOnTop(false);
    check(flipped.turn == 0 && !flipped.whiteOnTop(), "clearing white on top zeroes the rotation");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <cd dir> <output dir>\n", argv[0]);
        return 2;
    }
    const std::string cdDir = argv[1];
    const std::string outDir = argv[2];

    try {
        testButtons(cdDir, outDir);
        testTitle(cdDir, outDir);
        testSettings(outDir);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL threw: %s\n", error.what());
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_test passed\n");
    return 0;
}
