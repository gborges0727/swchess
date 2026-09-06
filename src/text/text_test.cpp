// Checks the four string tables and the two bitmap fonts against the CD.
//
// Run it as: text_test <cd dir> <output dir> [python] [width_oracle.py]
// The last two arguments turn on the cross-check against the Python that first
// read the fonts. Without them the layout still gets checked against itself,
// and the test says so.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "text/font.h"
#include "text/strings.h"

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

std::string toHex(std::string_view raw) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (char byte : raw) {
        const auto value = static_cast<unsigned char>(byte);
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0x0f]);
    }
    return out;
}

// Writes one rendered line as a binary PPM over a black background.
void writePpm(const std::string& path, const swchess::text::TextImage& image) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "FAIL cannot write %s\n", path.c_str());
        ++failures;
        return;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", image.width, image.height);
    std::vector<unsigned char> row(static_cast<std::size_t>(image.width) * 3);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::uint8_t* pixel =
                &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
            const bool solid = pixel[3] != 0;
            row[static_cast<std::size_t>(x) * 3 + 0] = solid ? pixel[0] : 0;
            row[static_cast<std::size_t>(x) * 3 + 1] = solid ? pixel[1] : 0;
            row[static_cast<std::size_t>(x) * 3 + 2] = solid ? pixel[2] : 0;
        }
        std::fwrite(row.data(), 1, row.size(), file);
    }
    std::fclose(file);
}

// Asks width_oracle.py how wide each line draws. Returns an empty vector when
// the script cannot run.
std::vector<int> askOracle(const std::string& python, const std::string& script,
                           const std::string& cdDir,
                           const std::vector<std::string>& requests) {
    std::string command = "\"" + python + "\" \"" + script + "\" --cd \"" + cdDir + "\"";
    for (const std::string& request : requests) {
        command += " " + request;
    }
    command += " 2>/dev/null";
    std::FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    std::vector<int> widths;
    char line[64];
    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        widths.push_back(std::atoi(line));
    }
    if (pclose(pipe) != 0) {
        return {};
    }
    return widths;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <cd dir> <output dir> [python] [width_oracle.py]\n",
                     argv[0]);
        return 2;
    }
    const std::string cdDir = argv[1];
    const std::string outDir = argv[2];
    const std::string python = argc > 3 ? argv[3] : "";
    const std::string oracle = argc > 4 ? argv[4] : "";

    using namespace swchess::text;

    try {
        // Every language carries the same 528 ids.
        const Language languages[] = {Language::English, Language::French, Language::German,
                                      Language::Spanish};
        StringTable tables[4];
        for (int index = 0; index < 4; ++index) {
            tables[index] = loadStrings(cdDir, languages[index]);
            check(tables[index].size() == 528,
                  std::string(languageName(languages[index])) + " carries 528 ids, got " +
                      std::to_string(tables[index].size()));
        }
        const StringTable& english = tables[0];
        const StringTable& french = tables[1];
        const StringTable& german = tables[2];
        const StringTable& spanish = tables[3];

        // A menu label straight out of tools/fonts/strings.csv.
        check(english.get(0) == "GAME MENU",
              "English id 0 reads GAME MENU, got " + std::string(english.get(0)));
        check(english.get(33035) == "King moved - No castle",
              "English id 33035 reads King moved - No castle");

        // German id 33035 stores the backslash where the reader sees O with a
        // dieresis, and GUITEXT draws it.
        const std::string germanLine(german.get(33035));
        check(germanLine == "K\\NIG BEWEGT - KEINE ROCHADE",
              "German id 33035 stores K\\NIG BEWEGT - KEINE ROCHADE, got " + germanLine);
        check(toUtf8(FontKind::Guitext, germanLine).rfind("KÖNIG BEWEGT", 0) == 0,
              "German id 33035 reads KÖNIG BEWEGT through GUITEXT");

        // French id 14001 stores a digit 2 where the reader sees e with a
        // grave, and LEGFONT draws it.
        const std::string frenchLine(french.get(14001));
        check(frenchLine == "Il y a tr2s longtemps dans une",
              "French id 14001 stores tr2s, got " + frenchLine);
        check(toUtf8(FontKind::Legfont, frenchLine).find("très") != std::string::npos,
              "French id 14001 reads très through LEGFONT");

        // Round trip one string back to the bytes the file holds.
        check(fromUtf8(FontKind::Legfont, toUtf8(FontKind::Legfont, frenchLine)) == frenchLine,
              "French id 14001 survives a trip through UTF-8 and back");

        const BitmapFont leg = loadLegFont(cdDir);
        const BitmapFont gui = loadGuiFont(cdDir);
        check(leg.glyphs.size() == 96 && gui.glyphs.size() == 96, "both fonts hold 96 cells");
        check(leg.cellHeight == 41 && gui.cellHeight == 16, "cell heights are 41 and 16");
        check(leg.glyph(' ').advance == 10 && leg.glyph('M').advance == 28 &&
                  leg.glyph('W').advance == 34 && leg.glyph('i').advance == 9,
              "LEGFONT advances match the table in XCHESS.EXE");
        check(!leg.has('/') && !gui.has(','), "LEGFONT has no slash and GUITEXT has no comma");

        // Draw both sample strings with both fonts and keep the pictures.
        struct Sample {
            const char* name;
            const BitmapFont* font;
            const char* fontName;
            std::string raw;
        };
        const std::vector<Sample> samples = {
            {"guitext_konig_bewegt.ppm", &gui, "guitext", germanLine},
            {"legfont_konig_bewegt.ppm", &leg, "legfont", germanLine},
            {"legfont_tres.ppm", &leg, "legfont", frenchLine},
            {"guitext_tres.ppm", &gui, "guitext", frenchLine},
        };
        std::vector<std::string> requests;
        std::vector<int> mine;
        for (const Sample& sample : samples) {
            const TextImage image = render(*sample.font, sample.raw);
            check(image.width == measure(*sample.font, sample.raw),
                  std::string(sample.name) + " renders at the measured width");
            check(image.unmapped.empty(), std::string(sample.name) + " draws every byte");
            writePpm(joinPath(outDir, sample.name), image);
            requests.push_back(std::string(sample.fontName) + ":" + toHex(sample.raw));
            mine.push_back(image.width);
            std::printf("%s is %d by %d pixels\n", sample.name, image.width, image.height);
        }

        if (!python.empty() && !oracle.empty()) {
            const std::vector<int> theirs = askOracle(python, oracle, cdDir, requests);
            check(theirs.size() == samples.size(),
                  "width_oracle.py answered for every sample, got " +
                      std::to_string(theirs.size()) + " lines");
            for (std::size_t index = 0; index < theirs.size() && index < mine.size(); ++index) {
                check(theirs[index] == mine[index],
                      std::string(samples[index].name) + " is " + std::to_string(mine[index]) +
                          " pixels wide, width_oracle.py says " + std::to_string(theirs[index]));
            }
        } else {
            std::printf("no python given, skipping the width_oracle.py cross-check\n");
        }

        // Every English string draws through the font its id picks. Only the
        // credit line at 15081 holds a byte LEGFONT cannot draw, the slash in
        // "Effects/Touch-up Artists".
        for (int id : english.ids()) {
            if (!english.has(id)) {
                continue;
            }
            const std::string_view raw = english.get(id);
            const FontKind kind = fontForId(id);
            const BitmapFont& font = kind == FontKind::Legfont ? leg : gui;
            const TextImage image = render(font, raw);
            const std::size_t expected = id == 15081 ? 1u : 0u;
            check(image.unmapped.size() == expected,
                  "English id " + std::to_string(id) + " leaves " +
                      std::to_string(image.unmapped.size()) + " bytes undrawn");
        }

        // Spanish is the language that runs into the missing GUITEXT comma.
        // Seven ids use one, and the original draws a blank there.
        const std::vector<int> commaIds = {32773, 32774, 32786, 33031, 33035, 33036, 33037};
        std::vector<int> found;
        for (int id : spanish.ids()) {
            if (!spanish.has(id)) {
                continue;
            }
            const std::string_view raw = spanish.get(id);
            const FontKind kind = fontForId(id);
            const BitmapFont& font = kind == FontKind::Legfont ? leg : gui;
            const TextImage image = render(font, raw);
            if (!image.unmapped.empty()) {
                if (id == 15081) {
                    continue;  // the same slash the English credit line holds
                }
                found.push_back(id);
                for (std::size_t at : image.unmapped) {
                    check(raw[at] == ',',
                          "Spanish id " + std::to_string(id) + " misses only a comma");
                }
            }
        }
        check(found == commaIds, "the seven Spanish comma ids are the only GUITEXT gaps");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL threw %s\n", error.what());
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::printf("all text checks passed\n");
    return 0;
}
