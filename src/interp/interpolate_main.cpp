// swchess-interpolate, the native replacement for `python3 -m tools.interp`.
//
//   swchess-interpolate --assets assets --captures BBWB,BNWN
//   swchess-interpolate --capture BBWB --assets assets --out DIR
//   swchess-interpolate --check assets/captures/BBWB/interp60
//   swchess-interpolate --refresh-cues assets/captures/BBWB/interp60
//
// With no --captures the run covers every capture the asset cache holds. A
// capture whose finished manifest already passes the checks is skipped, so a
// run that stopped partway carries on where it left off. Ctrl-C stops the run
// after the capture in flight is thrown away, which leaves the frames already
// on disk exactly as they were.
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "interp/check.h"
#include "interp/pipeline.h"

namespace {

namespace fs = std::filesystem;

std::atomic<bool> gCancelled{false};

void onInterrupt(int) {
    gCancelled.store(true);
}

int usage() {
    std::cerr
        << "usage: swchess-interpolate [--assets DIR] [--captures A,B] [--fps 60]\n"
        << "                          [--rife BINARY] [--model DIR] [--jobs N]\n"
        << "                          [--keep-work] [--dry-run]\n"
        << "       swchess-interpolate --capture NAME --out DIR\n"
        << "       swchess-interpolate --check OUT_DIR\n"
        << "       swchess-interpolate --refresh-cues OUT_DIR [--resolved PATH]\n";
    return 2;
}

std::vector<std::string> splitOnCommas(const std::string& text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t comma = text.find(',', at);
        const std::string part =
            text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!part.empty()) {
            out.push_back(part);
        }
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
    return out;
}

// Every capture folder in the cache that carries a resolved timeline.
std::vector<std::string> capturesIn(const std::string& assetsDir) {
    std::vector<std::string> out;
    const fs::path root = fs::path(assetsDir) / "captures";
    if (!fs::exists(root)) {
        return out;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(root)) {
        if (entry.is_directory() && fs::exists(entry.path() / "resolved.json")) {
            out.push_back(entry.path().filename().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    swchess::interp::PipelineOptions options;
    options.assetsDir = "assets";
    options.rifeBinary = SWCHESS_RIFE_BINARY;
    options.modelDir = SWCHESS_RIFE_MODEL_DIR;

    std::string capturesFilter;
    std::string checkDir;
    std::string refreshDir;
    std::string singleOut;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool hasValue = i + 1 < argc;
        if (flag == "--assets" && hasValue) {
            options.assetsDir = argv[++i];
        } else if (flag == "--capture" && hasValue) {
            options.capture = argv[++i];
        } else if (flag == "--captures" && hasValue) {
            capturesFilter = argv[++i];
        } else if (flag == "--out" && hasValue) {
            singleOut = argv[++i];
        } else if (flag == "--resolved" && hasValue) {
            options.resolvedPath = argv[++i];
        } else if (flag == "--rife" && hasValue) {
            options.rifeBinary = argv[++i];
        } else if (flag == "--model" && hasValue) {
            options.modelDir = argv[++i];
        } else if (flag == "--fps" && hasValue) {
            options.fps = std::atoi(argv[++i]);
        } else if (flag == "--jobs" && hasValue) {
            options.jobs = std::atoi(argv[++i]);
        } else if (flag == "--keep-work") {
            options.keepWork = true;
        } else if (flag == "--dry-run") {
            options.dryRun = true;
        } else if (flag == "--check" && hasValue) {
            checkDir = argv[++i];
        } else if (flag == "--refresh-cues" && hasValue) {
            refreshDir = argv[++i];
        } else if (flag == "-h" || flag == "--help") {
            return usage();
        } else {
            std::cerr << "swchess-interpolate: unknown argument " << flag << "\n";
            return usage();
        }
    }

    std::signal(SIGINT, onInterrupt);

    try {
        if (!refreshDir.empty()) {
            const bool changed = swchess::interp::refreshCues(refreshDir, options.resolvedPath);
            std::cout << refreshDir << ": cues "
                      << (changed ? "rewritten" : "already current") << std::endl;
            return 0;
        }

        if (!checkDir.empty()) {
            swchess::interp::CheckReport report = swchess::interp::runChecks(checkDir, true);
            for (const std::string& note : report.notes) {
                std::cout << "ok   " << note << "\n";
            }
            for (const std::string& problem : report.problems) {
                std::cout << "FAIL " << problem << "\n";
            }
            std::cout << checkDir << ": " << (report.passed() ? "PASS" : "FAIL") << std::endl;
            return report.passed() ? 0 : 1;
        }

        // One capture into an explicit directory, or a list of captures into
        // the standard interp60 folders under the cache.
        std::vector<std::pair<std::string, std::string>> work;
        if (!options.capture.empty() && !singleOut.empty()) {
            work.emplace_back(options.capture, singleOut);
        } else {
            std::vector<std::string> names =
                capturesFilter.empty()
                    ? capturesIn(options.assetsDir)
                    : splitOnCommas(capturesFilter);
            if (!options.capture.empty()) {
                names = {options.capture};
            }
            for (const std::string& name : names) {
                work.emplace_back(name, (fs::path(options.assetsDir) / "captures" / name /
                                         "interp60")
                                            .string());
            }
        }
        if (work.empty()) {
            std::cerr << "swchess-interpolate: no captures to work on\n";
            return usage();
        }

        int done = 0;
        int skipped = 0;
        for (const auto& job : work) {
            if (gCancelled.load()) {
                std::cout << "stopped after " << done << " captures, the rest are untouched"
                          << std::endl;
                return 1;
            }
            swchess::interp::PipelineOptions one = options;
            one.capture = job.first;
            one.outDir = job.second;

            // A capture whose finished manifest already passes is left alone,
            // so an interrupted run carries on where it stopped.
            if (!one.dryRun && fs::exists(fs::path(one.outDir) / "manifest.json")) {
                try {
                    if (swchess::interp::runChecks(one.outDir, false).passed()) {
                        std::cout << one.capture << ": already done" << std::endl;
                        ++skipped;
                        continue;
                    }
                } catch (const std::exception&) {
                    // An unreadable manifest just means the capture runs again.
                }
            }

            swchess::interp::Json manifest = swchess::interp::generate(one, &gCancelled);
            if (!manifest.is_null()) {
                std::cout << one.capture << ": wrote " << manifest.at("frames").size()
                          << " frames to " << one.outDir << " in "
                          << manifest.at("wall_seconds").get<double>() << " s" << std::endl;
            }
            ++done;
        }
        if (skipped != 0) {
            std::cout << skipped << " captures were already done" << std::endl;
        }
        return 0;
    } catch (const std::exception& error) {
        if (gCancelled.load()) {
            std::cerr << "swchess-interpolate: stopped, nothing on disk changed" << std::endl;
            return 1;
        }
        std::cerr << "swchess-interpolate: " << error.what() << std::endl;
        return 1;
    }
}
