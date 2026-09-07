#include "interp/check.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

#include "export/json_write.h"
#include "interp/fraction.h"
#include "interp/images.h"
#include "interp/png_io.h"
#include "interp/timeline.h"

namespace swchess::interp {
namespace {

namespace fs = std::filesystem;
using swchess::exporter::dumpPythonJson;
using swchess::exporter::Json;
using swchess::exporter::pythonFloatRepr;

Json readJsonFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return Json::parse(file);
}

// Rebuilds the exact decimal the manifest holds. A whole number arrives as an
// integer. A fraction arrives as the shortest decimal that reads back as the
// same double, which is exactly the text the writer put in the file, so
// reading it as a fraction of a power of ten loses nothing.
Fraction exactNumber(const Json& value) {
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return Fraction(value.get<std::int64_t>());
    }
    const std::string text = pythonFloatRepr(value.get<double>());
    std::size_t at = 0;
    bool negative = false;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        negative = text[at] == '-';
        ++at;
    }
    std::int64_t digits = 0;
    std::int64_t scale = 1;
    bool afterPoint = false;
    for (; at < text.size(); ++at) {
        if (text[at] == '.') {
            afterPoint = true;
            continue;
        }
        if (text[at] == 'e' || text[at] == 'E') {
            const int exponent = std::atoi(text.c_str() + at + 1);
            for (int i = 0; i < exponent; ++i) {
                digits *= 10;
            }
            for (int i = 0; i > exponent; --i) {
                scale *= 10;
            }
            break;
        }
        digits = digits * 10 + (text[at] - '0');
        if (afterPoint) {
            scale *= 10;
        }
    }
    return Fraction(negative ? -digits : digits, scale);
}

std::string decimalText(const Fraction& value) {
    return pythonFloatRepr(value.toDouble());
}

}  // namespace

CheckReport runChecks(const std::string& outDir, bool full) {
    CheckReport report;
    const fs::path root(outDir);
    const Json manifest = readJsonFile(root / "manifest.json");
    const Json& frames = manifest.at("frames");
    const int fps = manifest.at("fps").get<int>();
    const std::int64_t endMs = manifest.at("end_ms").get<std::int64_t>();

    const std::int64_t expected = sampleCount(endMs, fps);
    if (static_cast<std::int64_t>(frames.size()) != expected) {
        report.problems.push_back("frame count is " + std::to_string(frames.size()) +
                                  ", expected ceil(" + std::to_string(endMs) + " * " +
                                  std::to_string(fps) + " / 1000) = " + std::to_string(expected));
    } else {
        report.notes.push_back("frame count " + std::to_string(expected) + " matches ceil(" +
                               std::to_string(endMs) + " ms * " + std::to_string(fps) + " / 1000)");
    }

    std::vector<Fraction> times;
    times.reserve(frames.size());
    for (const Json& frame : frames) {
        times.push_back(exactNumber(frame.at("t_ms")));
    }
    std::vector<std::size_t> backwards;
    for (std::size_t i = 1; i < times.size(); ++i) {
        if (times[i] <= times[i - 1]) {
            backwards.push_back(i);
        }
    }
    if (!backwards.empty()) {
        report.problems.push_back("timestamps do not increase at frame " +
                                  std::to_string(backwards.front()));
    } else if (!times.empty()) {
        report.notes.push_back("timestamps increase from " + decimalText(times.front()) + " to " +
                               decimalText(times.back()));
    }

    Fraction total(0);
    for (const Json& frame : frames) {
        total = total + exactNumber(frame.at("duration_ms"));
    }
    if (total != Fraction(endMs)) {
        report.problems.push_back("durations add up to " + decimalText(total) + ", end_ms is " +
                                  std::to_string(endMs));
    } else {
        report.notes.push_back("durations add up to " + std::to_string(endMs) + " ms");
    }

    const int width = manifest.at("frame_rect").at("w").get<int>();
    const int height = manifest.at("frame_rect").at("h").get<int>();
    std::vector<std::string> wrongSize;
    for (const Json& frame : frames) {
        const fs::path path = root / frame.at("file").get<std::string>();
        if (!fs::exists(path)) {
            report.problems.push_back("missing frame file " + frame.at("file").get<std::string>());
            continue;
        }
        int fileWidth = 0;
        int fileHeight = 0;
        readPngSize(path.string(), &fileWidth, &fileHeight);
        if (fileWidth != width || fileHeight != height) {
            wrongSize.push_back(frame.at("file").get<std::string>());
        }
    }
    if (!wrongSize.empty()) {
        report.problems.push_back(std::to_string(wrongSize.size()) + " frames are not " +
                                  std::to_string(width) + "x" + std::to_string(height) +
                                  ", first " + wrongSize.front());
    } else {
        report.notes.push_back("all " + std::to_string(frames.size()) + " frames are " +
                               std::to_string(width) + "x" + std::to_string(height));
    }

    // Every authored pose time that lands on a sample has to copy its pose.
    // Two poses can share one millisecond, and only the later one has a sample
    // of its own, so that is the one to check.
    const std::string sourceDir = manifest.at("input").at("source_dir").get<std::string>();
    Rect rect;
    rect.x = manifest.at("frame_rect").at("x").get<int>();
    rect.y = manifest.at("frame_rect").at("y").get<int>();
    rect.width = width;
    rect.height = height;

    std::map<std::string, std::size_t> firstFrameAt;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        firstFrameAt.emplace(decimalText(times[i]), i);
    }
    std::map<std::string, std::vector<const Json*>> posesByTime;
    for (const Json& pose : manifest.at("input").at("poses")) {
        posesByTime[decimalText(exactNumber(pose.at("t_ms")))].push_back(&pose);
    }
    int copied = 0;
    for (const auto& group : posesByTime) {
        auto found = firstFrameAt.find(group.first);
        if (found == firstFrameAt.end()) {
            continue;
        }
        const Json& pose = *group.second.back();
        const Json& frame = frames[found->second];
        if (frame.at("kind").get<std::string>() != "copy" || frame.at("source").size() != 1 ||
            frame.at("source")[0].get<int>() != pose.at("index").get<int>()) {
            report.problems.push_back("pose " + std::to_string(pose.at("index").get<int>()) +
                                      " at " + group.first + " ms is not copied, frame " +
                                      frame.at("file").get<std::string>() + " is " +
                                      frame.at("kind").get<std::string>());
            continue;
        }
        if (!full) {
            ++copied;
            continue;
        }
        std::vector<std::uint8_t> want =
            compose((fs::path(sourceDir) / pose.at("image").get<std::string>()).string(), pose,
                    rect);
        Picture got = readPng((root / frame.at("file").get<std::string>()).string());
        if (got.pixels != want) {
            report.problems.push_back("frame " + frame.at("file").get<std::string>() +
                                      " does not match pose " +
                                      std::to_string(pose.at("index").get<int>()) +
                                      " pixel for pixel");
        } else {
            ++copied;
        }
    }
    report.notes.push_back(std::to_string(copied) +
                           " authored pose times land on a sample and all copy their pose");

    // Every empty frame has to sit before the first pose.
    Fraction firstPose(0);
    bool havePose = false;
    for (const Json& pose : manifest.at("input").at("poses")) {
        const Fraction t = exactNumber(pose.at("t_ms"));
        if (!havePose || t < firstPose) {
            firstPose = t;
            havePose = true;
        }
    }
    std::vector<std::string> lateBlanks;
    int blanks = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].at("kind").get<std::string>() != "blank") {
            continue;
        }
        ++blanks;
        if (havePose && !(times[i] < firstPose)) {
            lateBlanks.push_back(frames[i].at("file").get<std::string>());
        }
    }
    if (!lateBlanks.empty()) {
        report.problems.push_back(std::to_string(lateBlanks.size()) +
                                  " empty frames sit at or after the first pose, first " +
                                  lateBlanks.front());
    } else {
        report.notes.push_back(std::to_string(blanks) + " empty frames, all before the first"
                               " pose at " + decimalText(firstPose) + " ms");
    }

    // The sound cues have to survive from the input, each at the time the
    // input gives it.
    const Json& resolved = manifest.at("input").at("resolved");
    if (!resolved.is_null() && fs::exists(resolved.get<std::string>())) {
        const std::string path = resolved.get<std::string>();
        const Json spec = readJsonFile(path);
        const Json wantPre = spec.contains("pre_sounds") && !spec.at("pre_sounds").is_null()
                                 ? spec.at("pre_sounds")
                                 : Json::array();
        if (dumpPythonJson(wantPre, true) != dumpPythonJson(manifest.at("pre_sounds"), true)) {
            report.problems.push_back("the manifest pre_sounds differ from " + path);
        } else {
            report.notes.push_back(std::to_string(wantPre.size()) + " pre_sounds kept from " + path);
        }
        const Json wantFinal = spec.contains("final_wav") ? spec.at("final_wav") : Json(nullptr);
        if (dumpPythonJson(wantFinal, true) != dumpPythonJson(manifest.at("final_wav"), true)) {
            report.problems.push_back("the manifest final_wav differs from " + path);
        } else {
            report.notes.push_back("the final sound matches " + path);
        }
        const Json wantSounds = soundEvents(spec);
        if (dumpPythonJson(wantSounds, true) != dumpPythonJson(manifest.at("sounds"), true)) {
            report.problems.push_back("the manifest sound events differ from " + path);
        } else {
            report.notes.push_back("all " + std::to_string(wantSounds.size()) +
                                   " sound events start at the times " + path + " gives them");
        }
    } else {
        report.notes.push_back(std::to_string(manifest.at("sounds").size()) +
                               " sound events in the manifest, the input file was not on disk"
                               " to compare");
    }

    return report;
}

}  // namespace swchess::interp
