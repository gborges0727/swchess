// Checks the case-insensitive CD lookup against a temporary directory that
// holds mixed-case names.

#include "assets/cdfs.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL " << what << "\n";
        ++failures;
    }
}

void touch(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << "x";
}

}  // namespace

int main() {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("cdfs_test_" + std::to_string(static_cast<long long>(std::rand())));
    std::filesystem::remove_all(root);

    std::filesystem::path plain = root / "plain";
    std::filesystem::create_directories(plain);
    touch(plain / "cm.ini");
    touch(plain / "SwcAudio.dll");
    touch(plain / "WHTBTM_P.BMP");

    swchess::CdDir dir(plain);

    check(dir.resolve("CM.INI") == plain / "cm.ini", "lowercase file answers an uppercase name");
    check(dir.resolve("SWCAUDIO.DLL") == plain / "SwcAudio.dll", "mixed case answers");
    check(dir.resolve("WHTBTM_P.BMP") == plain / "WHTBTM_P.BMP", "exact name answers");
    check(dir.resolve("whtbtm_p.bmp") == plain / "WHTBTM_P.BMP", "lowercase query answers");
    check(dir.has("CM.INI"), "has finds a file under another spelling");
    check(!dir.has("XCHESS.EXE"), "has reports a missing file");

    bool threw = false;
    try {
        dir.resolve("XCHESS.EXE");
    } catch (const std::runtime_error& error) {
        threw = true;
        check(std::string(error.what()).find("XCHESS.EXE") != std::string::npos,
              "the missing-file message names the file");
    }
    check(threw, "resolve throws for a missing file");

    // A copy that holds both spellings of one name has no single right answer.
    // macOS and Windows cannot hold both at once, so the index is built from a
    // name list here and the same code runs on every platform.
    swchess::CdDir clash(plain, {"CM.INI", "cm.ini", "XCHESS.EXE"});
    bool clashThrew = false;
    try {
        clash.resolve("CM.INI");
    } catch (const std::runtime_error& error) {
        clashThrew = true;
        check(std::string(error.what()).find("case") != std::string::npos,
              "the ambiguous message explains the case clash");
    }
    check(clashThrew, "resolve throws when two names differ only by case");
    check(clash.resolve("XCHESS.EXE") == plain / "XCHESS.EXE",
          "an unambiguous name still resolves next to a clashing one");

    check(swchess::resolveCdFile(plain.string(), "CM.INI") == plain / "cm.ini",
          "resolveCdFile answers from the cached index");

    std::filesystem::remove_all(root);
    if (failures == 0) {
        std::cout << "cdfs ok\n";
    }
    return failures == 0 ? 0 : 1;
}
