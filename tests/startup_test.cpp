// Checks how startup picks the CD folder and the artwork folder. It builds
// its own temporary directories, so it needs no CD and it opens no dialog.

#include "app/startup.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

// Builds a folder that passes the CD check. `lowercase` writes the names the
// way a copy made on a case-preserving filesystem often does.
void makeCd(const std::filesystem::path& dir, bool lowercase) {
    std::filesystem::create_directories(dir);
    if (lowercase) {
        touch(dir / "xchess.exe");
        touch(dir / "cc256.dll");
        touch(dir / "cmwin.dat");
    } else {
        touch(dir / "XCHESS.EXE");
        touch(dir / "CC256.DLL");
        touch(dir / "CMWIN.DAT");
    }
}

}  // namespace

int main() {
    using swchess::app::StartupRequest;
    using swchess::app::StartupResult;
    using swchess::app::StartupStatus;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("startup_test_" + std::to_string(static_cast<long long>(std::rand())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::filesystem::path upperCd = root / "cd-upper";
    const std::filesystem::path lowerCd = root / "cd-lower";
    const std::filesystem::path shortCd = root / "cd-short";
    const std::filesystem::path configHome = root / "config";
    // A folder with no cd and no assets in it. A request pointed here stands
    // for the public build, which carries no data of its own.
    const std::filesystem::path emptyBase = root / "empty-base";
    std::filesystem::create_directories(emptyBase);
    makeCd(upperCd, false);
    makeCd(lowerCd, true);
    std::filesystem::create_directories(shortCd);
    touch(shortCd / "XCHESS.EXE");

    // The three required files, whatever case the copy spells them in.
    check(swchess::app::missingCdFiles(upperCd.string()).empty(),
          "an uppercase copy holds every required file");
    check(swchess::app::missingCdFiles(lowerCd.string()).empty(),
          "a lowercase copy holds every required file");
    const std::vector<std::string> missing = swchess::app::missingCdFiles(shortCd.string());
    check(missing.size() == 2 && missing[0] == "CC256.DLL" && missing[1] == "CMWIN.DAT",
          "a folder with only XCHESS.EXE reports the other two names");
    check(swchess::app::missingCdFiles((root / "nowhere").string()).size() == 3,
          "a folder that is not there reports all three names");

    // Nothing names a CD folder, so the game cannot start without a dialog.
    {
        StartupRequest request;
        request.basePath = emptyBase.string();
        request.configDir = configHome.string();
        request.readEnvironment = false;
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Failed, "an empty request fails");
        check(!result.message.empty(), "the failure carries a message");
        check(!result.assetsDir.empty(), "the artwork folder falls back to the cache directory");
    }

    // The command line wins over everything else.
    {
        StartupRequest request;
        request.basePath = emptyBase.string();
        request.cdDir = upperCd.string() + "/";
        request.assetsDir = (root / "art").string();
        request.configDir = configHome.string();
        request.readEnvironment = false;
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Ready, "--cd names a usable folder");
        check(result.cdDir == upperCd.string(), "the trailing separator comes off the path");
        check(result.assetsDir == (root / "art").string(), "--assets wins for the artwork");
    }

    // The environment answers when the command line does not.
    {
        setenv("SWCHESS_CD", lowerCd.string().c_str(), 1);
        setenv("SWCHESS_ASSETS", (root / "env-art").string().c_str(), 1);
        StartupRequest request;
        request.basePath = emptyBase.string();
        request.configDir = configHome.string();
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Ready, "SWCHESS_CD names a usable folder");
        check(result.cdDir == lowerCd.string(), "the environment sets the CD folder");
        check(result.assetsDir == (root / "env-art").string(),
              "the environment sets the artwork folder");

        // The command line still wins while those variables are set.
        StartupRequest overridden = request;
        overridden.cdDir = upperCd.string();
        const StartupResult beats = swchess::app::resolveWithoutAsking(overridden);
        check(beats.cdDir == upperCd.string(), "--cd beats SWCHESS_CD");

        unsetenv("SWCHESS_CD");
        unsetenv("SWCHESS_ASSETS");
    }

    // The versioned file the game writes reads back the same way.
    {
        swchess::app::StartupConfig written;
        written.cdDir = upperCd.string();
        written.assetsDir = (root / "saved-art").string();
        check(swchess::app::writeStartupConfig(configHome.string(), written),
              "the configuration file is written");

        StartupRequest request;
        request.basePath = emptyBase.string();
        request.configDir = configHome.string();
        request.readEnvironment = false;
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Ready, "the saved folder starts the game");
        check(result.cdDir == upperCd.string(), "the saved CD folder comes back");
        check(result.assetsDir == written.assetsDir, "the saved artwork folder comes back");
        check(!result.shouldSave, "a complete configuration file needs no rewrite");
    }

    // A saved folder that has moved fails, so the window asks again.
    {
        swchess::app::StartupConfig written;
        written.cdDir = (root / "moved").string();
        written.assetsDir = (root / "saved-art").string();
        swchess::app::writeStartupConfig(configHome.string(), written);
        StartupRequest request;
        request.basePath = emptyBase.string();
        request.configDir = configHome.string();
        request.readEnvironment = false;
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Failed, "a folder that moved fails");
        check(result.message.find("XCHESS.EXE") != std::string::npos,
              "the message names the missing file");
    }

    // The two-line file the old macOS launcher wrote still reads.
    {
        const std::filesystem::path legacyHome = root / "legacy";
        std::filesystem::create_directories(legacyHome);
        std::ofstream file(legacyHome / "config");
        file << upperCd.string() << "\n" << (root / "legacy-art").string() << "\n";
        file.close();

        StartupRequest request;
        request.basePath = emptyBase.string();
        request.configDir = legacyHome.string();
        request.readEnvironment = false;
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Ready, "the old two-line file starts the game");
        check(result.cdDir == upperCd.string(), "the old file names the CD folder");
        check(result.assetsDir == (root / "legacy-art").string(),
              "the old file names the artwork folder");

        // Writing the new file leaves the old one alone and wins from then on.
        swchess::app::StartupConfig moved;
        moved.cdDir = lowerCd.string();
        moved.assetsDir = (root / "legacy-art").string();
        swchess::app::writeStartupConfig(legacyHome.string(), moved);
        const StartupResult again = swchess::app::resolveWithoutAsking(request);
        check(again.cdDir == lowerCd.string(), "the versioned file wins over the old one");
        check(std::filesystem::exists(legacyHome / "config"), "the old file stays where it was");
    }

    // A build with the data inside it reads that data and nothing else.
    {
        const std::filesystem::path base = root / "bundle-base";
        makeCd(base / "cd", false);
        std::filesystem::create_directories(base / "assets");
        touch(base / "assets" / "catalog.json");

        const swchess::app::BundleData found = swchess::app::findBundleData(base.string());
        check(found.present, "the bundle holds its own data");
        check(found.missing.empty(), "the bundled data is complete");
        check(!swchess::app::findBundleData(emptyBase.string()).present,
              "a folder with no cd and no assets holds no bundled data");

        // The command line, the environment and the saved file all name the
        // lowercase copy. The bundled copy wins over all three.
        setenv("SWCHESS_CD", lowerCd.string().c_str(), 1);
        setenv("SWCHESS_ASSETS", (root / "env-art").string().c_str(), 1);
        swchess::app::StartupConfig written;
        written.cdDir = lowerCd.string();
        written.assetsDir = (root / "saved-art").string();
        swchess::app::writeStartupConfig(configHome.string(), written);

        StartupRequest request;
        request.basePath = base.string();
        request.cdDir = lowerCd.string();
        request.assetsDir = (root / "art").string();
        request.configDir = configHome.string();
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Ready, "the bundled data starts the game");
        check(result.cdDir == (base / "cd").string(), "the bundled CD folder wins over --cd");
        check(result.assetsDir == (base / "assets").string(),
              "the bundled artwork folder wins over --assets");
        check(!result.shouldSave, "a bundled build writes no startup.conf");

        unsetenv("SWCHESS_CD");
        unsetenv("SWCHESS_ASSETS");
    }

    // A bundled build with a file missing stops and names it. It falls back
    // to no other folder, even when the command line names a good one.
    {
        const std::filesystem::path base = root / "broken-base";
        makeCd(base / "cd", false);
        std::filesystem::remove(base / "cd" / "CC256.DLL");
        std::filesystem::create_directories(base / "assets");

        const swchess::app::BundleData found = swchess::app::findBundleData(base.string());
        check(found.present, "a build with a cd folder has its own data");
        check(found.missing.size() == 2 && found.missing[0] == "CC256.DLL" &&
                  found.missing[1] == "assets/catalog.json",
              "the missing list names the file and the catalog");

        StartupRequest request;
        request.basePath = base.string();
        request.cdDir = upperCd.string();
        request.configDir = configHome.string();
        request.readEnvironment = false;
        const StartupResult result = swchess::app::resolveWithoutAsking(request);
        check(result.status == StartupStatus::Failed, "a broken bundled build fails");
        check(result.message.find("CC256.DLL") != std::string::npos,
              "the message names the missing CD file");
        check(result.message.find("catalog.json") != std::string::npos,
              "the message names the missing catalog");
        check(result.cdDir != upperCd.string(), "the broken build does not use --cd instead");
    }

    std::filesystem::remove_all(root);
    if (failures != 0) {
        std::cerr << failures << " checks failed\n";
        return 1;
    }
    std::cout << "startup ok\n";
    return 0;
}
