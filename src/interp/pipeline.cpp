#include "interp/pipeline.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <vector>

#include "export/png_write.h"
#include "export/sha256.h"
#include "interp/images.h"
#include "interp/png_io.h"
#include "interp/process.h"
#include "interp/timeline.h"

namespace swchess::interp {
namespace {

namespace fs = std::filesystem;
using swchess::exporter::encodePng;
using swchess::exporter::kPngGray;
using swchess::exporter::kPngRgb;
using swchess::exporter::kPngRgba;
using swchess::exporter::sha256File;

std::string numbered(const char* pattern, long long value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), pattern, value);
    return std::string(buffer);
}

void throwIfCancelled(const std::atomic<bool>* cancelled) {
    if (cancelled != nullptr && cancelled->load()) {
        throw std::runtime_error("cancelled");
    }
}

Json readJsonFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return Json::parse(file);
}

void writeFileBytes(const fs::path& path, const std::vector<std::uint8_t>& blob) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot write " + path.string());
    }
    const std::size_t written = std::fwrite(blob.data(), 1, blob.size(), file);
    std::fclose(file);
    if (written != blob.size()) {
        throw std::runtime_error("short write on " + path.string());
    }
}

// The commit of the repository the helper is running in, or nothing outside
// a git checkout.
Json toolCommit() {
    try {
        RunResult result = runProgram({"git", "-C", fs::current_path().string(), "rev-parse",
                                       "HEAD"});
        if (result.exitCode != 0) {
            return nullptr;
        }
        std::string text = result.output;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' ||
                                 text.back() == ' ')) {
            text.pop_back();
        }
        if (text.empty()) {
            return nullptr;
        }
        return text;
    } catch (const std::exception&) {
        return nullptr;
    }
}

// Where each interpolated sample's colour and alpha picture will land.
struct RifePlan {
    std::vector<std::vector<std::string>> commands;
    std::map<std::size_t, std::pair<std::string, std::string>> paths;
};

RifePlan planRife(const std::vector<PlannedFrame>& frames, const fs::path& workDir,
                  const std::string& binary, const std::string& model) {
    // One list of samples per transition, in the order the frames ask for them.
    std::map<std::size_t, std::vector<std::pair<std::size_t, Fraction>>> wanted;
    for (std::size_t n = 0; n < frames.size(); ++n) {
        if (frames[n].kind == FrameKind::Interp) {
            wanted[frames[n].pose].emplace_back(n, frames[n].step);
        }
    }

    RifePlan plan;
    for (const auto& transition : wanted) {
        const std::size_t k = transition.first;
        std::int64_t denominator = 1;
        for (const auto& sample : transition.second) {
            const std::int64_t other = sample.second.denominator();
            denominator = denominator / std::gcd(denominator, other) * other;
        }

        if (denominator <= kMaxDenominator) {
            const std::string pair = numbered("pair%04lld", static_cast<long long>(k));
            for (const char* channel : {"rgb", "alpha"}) {
                const fs::path pairDir = workDir / channel / pair;
                const fs::path outDir = workDir / (std::string(channel) + "_out") / pair;
                fs::remove_all(pairDir);
                fs::remove_all(outDir);
                fs::create_directories(pairDir);
                fs::create_directories(outDir);
                for (int j = 0; j < 2; ++j) {
                    const fs::path source =
                        workDir / channel /
                        numbered("%05lld.png", static_cast<long long>(k + j));
                    fs::create_hard_link(source, pairDir / (std::to_string(j) + ".png"));
                }
                plan.commands.push_back({binary, "-m", model, "-i", pairDir.string(), "-o",
                                         outDir.string(), "-n",
                                         std::to_string(2 * denominator)});
            }
            for (const auto& sample : transition.second) {
                const std::int64_t step =
                    sample.second.numerator() * (denominator / sample.second.denominator());
                const std::string file = numbered("%08lld.png", static_cast<long long>(step + 1));
                plan.paths[sample.first] = {(workDir / "rgb_out" / pair / file).string(),
                                            (workDir / "alpha_out" / pair / file).string()};
            }
        } else {
            for (const auto& sample : transition.second) {
                std::string outputs[2];
                int slot = 0;
                for (const char* channel : {"rgb", "alpha"}) {
                    const fs::path outDir = workDir / (std::string(channel) + "_out");
                    fs::create_directories(outDir);
                    const fs::path outPath =
                        outDir / numbered("%08lld.png", static_cast<long long>(sample.first));
                    char step[32];
                    std::snprintf(step, sizeof(step), "%.9f", sample.second.toDouble());
                    plan.commands.push_back(
                        {binary, "-m", model, "-0",
                         (workDir / channel / numbered("%05lld.png", static_cast<long long>(k)))
                             .string(),
                         "-1",
                         (workDir / channel /
                          numbered("%05lld.png", static_cast<long long>(k + 1)))
                             .string(),
                         "-s", step, "-o", outPath.string()});
                    outputs[slot++] = outPath.string();
                }
                plan.paths[sample.first] = {outputs[0], outputs[1]};
            }
        }
    }
    return plan;
}

void runCommands(const std::vector<std::vector<std::string>>& commands,
                 const std::atomic<bool>* cancelled) {
    std::atomic<std::size_t> next{0};
    std::mutex lock;
    std::string failure;

    auto worker = [&]() {
        while (true) {
            const std::size_t at = next.fetch_add(1);
            if (at >= commands.size()) {
                return;
            }
            {
                std::lock_guard<std::mutex> guard(lock);
                if (!failure.empty()) {
                    return;
                }
            }
            if (cancelled != nullptr && cancelled->load()) {
                return;
            }
            RunResult result = runProgram(commands[at]);
            if (result.exitCode != 0) {
                std::lock_guard<std::mutex> guard(lock);
                if (failure.empty()) {
                    std::string tail = result.output;
                    if (tail.size() > 2000) {
                        tail = tail.substr(tail.size() - 2000);
                    }
                    failure = "rife failed: " + joinArguments(commands[at]) + "\n" + tail;
                }
                return;
            }
        }
    };

    const int workers = std::min<int>(kRifeProcesses, static_cast<int>(commands.size()));
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i) {
        threads.emplace_back(worker);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    throwIfCancelled(cancelled);
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
}

// Runs `work` over the numbers 0 to count, on `jobs` threads.
void parallelFor(std::size_t count, int jobs,
                 const std::function<void(std::size_t)>& work) {
    if (count == 0) {
        return;
    }
    std::atomic<std::size_t> next{0};
    std::mutex lock;
    std::string failure;
    auto worker = [&]() {
        while (true) {
            const std::size_t at = next.fetch_add(1);
            if (at >= count) {
                return;
            }
            try {
                work(at);
            } catch (const std::exception& error) {
                std::lock_guard<std::mutex> guard(lock);
                if (failure.empty()) {
                    failure = error.what();
                }
                return;
            }
        }
    };
    const int workers = std::max(1, std::min<int>(jobs, static_cast<int>(count)));
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i) {
        threads.emplace_back(worker);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
}

void describe(const Json& spec, const std::vector<PlannedFrame>& frames, int fps,
              const Rect& rect) {
    std::map<std::string, int> byKind;
    for (const PlannedFrame& frame : frames) {
        byKind[frameKindName(frame.kind)] += 1;
    }
    std::cout << "capture " << spec.at("capture").get<std::string>() << ", "
              << spec.at("poses").size() << " poses, end "
              << spec.at("end_ms").get<std::int64_t>() << " ms, " << frames.size()
              << " frames at " << fps << " fps\n";
    std::cout << "canvas " << rect.width << "x" << rect.height << " at (" << rect.x << ", "
              << rect.y << ")\n";
    std::cout << "frames by kind: ";
    bool first = true;
    for (const auto& kind : byKind) {
        if (!first) {
            std::cout << ", ";
        }
        first = false;
        std::cout << kind.first << " " << kind.second;
    }
    std::cout << std::endl;
}

}  // namespace

const char* const kModelName = "rife-v4.6";

Json generate(const PipelineOptions& options, const std::atomic<bool>* cancelled) {
    const auto started = std::chrono::steady_clock::now();

    const fs::path sourceDir = fs::path(options.assetsDir) / "captures" / options.capture;
    const std::string specPath =
        options.resolvedPath.empty() ? (sourceDir / "resolved.json").string()
                                     : options.resolvedPath;
    if (!fs::exists(specPath)) {
        throw std::runtime_error("no timeline at " + specPath);
    }
    const Json spec = readJsonFile(specPath);
    const std::string specHash = sha256File(specPath);
    const std::vector<PlannedFrame> frames = planFrames(spec, options.fps);
    const Rect rect = unionRect(spec.at("poses"));

    if (options.dryRun) {
        describe(spec, frames, options.fps, rect);
        return nullptr;
    }

    // Everything lands beside the requested directory and moves into place at
    // the end, so a run that stops halfway leaves the old frames alone.
    const fs::path finalDir(options.outDir);
    const std::string stamp = std::to_string(static_cast<long>(::getpid()));
    const fs::path stagingDir =
        finalDir.parent_path() / (finalDir.filename().string() + ".new-" + stamp);
    fs::remove_all(stagingDir);
    fs::create_directories(stagingDir);
    const fs::path workDir = stagingDir / "work";

    try {
        // Compose every pose and write the two pictures RIFE reads.
        std::vector<std::vector<std::uint8_t>> composed;
        composed.reserve(spec.at("poses").size());
        for (const char* name : {"rgb", "alpha"}) {
            fs::remove_all(workDir / name);
            fs::create_directories(workDir / name);
        }
        for (std::size_t i = 0; i < spec.at("poses").size(); ++i) {
            const Json& pose = spec.at("poses")[i];
            std::vector<std::uint8_t> rgba =
                compose((sourceDir / pose.at("image").get<std::string>()).string(), pose, rect);
            std::vector<std::uint8_t> rgb;
            std::vector<std::uint8_t> alpha;
            split(rgba, &rgb, &alpha);
            const std::string name = numbered("%05lld.png", static_cast<long long>(i));
            writeFileBytes(workDir / "rgb" / name,
                           encodePng(rect.width, rect.height, kPngRgb, rgb, 1));
            writeFileBytes(workDir / "alpha" / name,
                           encodePng(rect.width, rect.height, kPngGray, alpha, 1));
            composed.push_back(std::move(rgba));
        }
        throwIfCancelled(cancelled);

        RifePlan plan = planRife(frames, workDir, options.rifeBinary, options.modelDir);
        runCommands(plan.commands, cancelled);
        throwIfCancelled(cancelled);

        // Where each pose index sits in the pose list.
        std::map<int, std::size_t> byIndex;
        for (std::size_t i = 0; i < spec.at("poses").size(); ++i) {
            byIndex[spec.at("poses")[i].at("index").get<int>()] = i;
        }
        const std::vector<std::uint8_t> empty(
            static_cast<std::size_t>(rect.width) * rect.height * 4, 0);

        std::vector<std::string> names;
        names.reserve(frames.size());
        for (std::size_t n = 0; n < frames.size(); ++n) {
            names.push_back(numbered("frame%05lld.png", static_cast<long long>(n)));
        }

        const int jobs = options.jobs > 0
                             ? options.jobs
                             : std::max<int>(1, static_cast<int>(
                                                    std::thread::hardware_concurrency()));
        parallelFor(frames.size(), jobs, [&](std::size_t n) {
            const fs::path outPath = stagingDir / names[n];
            if (frames[n].kind == FrameKind::Interp) {
                const auto& pair = plan.paths.at(n);
                Picture colour = readPng(pair.first);
                Picture alpha = readPng(pair.second);
                if (colour.width != rect.width || colour.height != rect.height ||
                    alpha.width != rect.width || alpha.height != rect.height) {
                    throw std::runtime_error("rife returned the wrong size for " + pair.first);
                }
                std::vector<std::uint8_t> rgb;
                if (colour.channels == 3) {
                    rgb = std::move(colour.pixels);
                } else {
                    rgb.resize(static_cast<std::size_t>(rect.width) * rect.height * 3);
                    for (std::size_t i = 0; i < rgb.size() / 3; ++i) {
                        for (int c = 0; c < 3; ++c) {
                            rgb[i * 3 + c] = colour.pixels[i * colour.channels + c];
                        }
                    }
                }
                std::vector<std::uint8_t> grey;
                if (alpha.channels == 1) {
                    grey = std::move(alpha.pixels);
                } else {
                    grey.resize(static_cast<std::size_t>(rect.width) * rect.height);
                    for (std::size_t i = 0; i < grey.size(); ++i) {
                        grey[i] = alpha.pixels[i * alpha.channels];
                    }
                }
                writeFileBytes(outPath, encodePng(rect.width, rect.height, kPngRgba,
                                                  combine(rgb, grey, kAlphaCutoff), 6));
            } else if (frames[n].kind == FrameKind::Blank) {
                writeFileBytes(outPath, encodePng(rect.width, rect.height, kPngRgba, empty, 6));
            } else {
                writeFileBytes(outPath,
                               encodePng(rect.width, rect.height, kPngRgba,
                                         composed[byIndex.at(frames[n].source[0])], 6));
            }
        });
        throwIfCancelled(cancelled);

        // The manifest.
        Json tool = Json::object();
        tool["commit"] = toolCommit();
        tool["model"] = kModelName;
        tool["model_sha256"] = sha256File((fs::path(options.modelDir) / "flownet.bin").string());
        tool["binary_sha256"] = sha256File(options.rifeBinary);
        tool["alpha_cutoff"] = kAlphaCutoff;
        tool["rife_commands"] = static_cast<std::uint64_t>(plan.commands.size());
        tool["rife_example"] = plan.commands.empty()
                                   ? Json(nullptr)
                                   : Json(joinArguments(plan.commands.front()));

        std::map<std::string, std::string> hashes;
        Json posesOut = Json::array();
        for (const Json& pose : spec.at("poses")) {
            const std::string image = pose.at("image").get<std::string>();
            auto found = hashes.find(image);
            if (found == hashes.end()) {
                found = hashes.emplace(image, sha256File((sourceDir / image).string())).first;
            }
            Json entry = Json::object();
            entry["index"] = pose.at("index");
            entry["t_ms"] = pose.at("t_ms");
            entry["image"] = image;
            entry["x"] = pose.at("x");
            entry["y"] = pose.at("y");
            entry["w"] = pose.at("w");
            entry["h"] = pose.at("h");
            entry["sha256"] = found->second;
            posesOut.push_back(entry);
        }

        Json input = Json::object();
        input["resolved"] = specPath;
        input["resolved_sha256"] = specHash;
        input["source_dir"] = sourceDir.string();
        input["from_fixture"] = false;
        input["poses"] = posesOut;

        Json frameList = Json::array();
        const Fraction end(spec.at("end_ms").get<std::int64_t>());
        std::vector<Fraction> starts;
        starts.reserve(frames.size());
        for (const PlannedFrame& frame : frames) {
            starts.push_back(frame.t.roundTo6());
        }
        for (std::size_t n = 0; n < frames.size(); ++n) {
            const Fraction stop = n + 1 < starts.size() ? starts[n + 1] : end;
            Json entry = Json::object();
            entry["file"] = names[n];
            entry["t_ms"] = starts[n].toDouble();
            entry["duration_ms"] = (stop - starts[n]).toDouble();
            entry["kind"] = frameKindName(frames[n].kind);
            Json source = Json::array();
            for (int index : frames[n].source) {
                source.push_back(index);
            }
            entry["source"] = source;
            entry["s"] = frames[n].hasStep ? Json(frames[n].step.toDouble()) : Json(nullptr);
            frameList.push_back(entry);
        }

        Json manifest = Json::object();
        manifest["capture"] = options.capture;
        manifest["fps"] = options.fps;
        manifest["tool"] = tool;
        manifest["input"] = input;
        manifest["canvas"] = spec.at("canvas");
        Json frameRect = Json::object();
        frameRect["x"] = rect.x;
        frameRect["y"] = rect.y;
        frameRect["w"] = rect.width;
        frameRect["h"] = rect.height;
        manifest["frame_rect"] = frameRect;
        manifest["end_ms"] = spec.at("end_ms");
        manifest["final_wav"] = spec.contains("final_wav") ? spec.at("final_wav") : Json(nullptr);
        manifest["cuts"] = spec.contains("cuts") && !spec.at("cuts").is_null()
                               ? spec.at("cuts")
                               : Json::array();
        manifest["pre_sounds"] = spec.contains("pre_sounds") && !spec.at("pre_sounds").is_null()
                                     ? spec.at("pre_sounds")
                                     : Json::array();
        manifest["sounds"] = soundEvents(spec);
        manifest["frames"] = frameList;

        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        char rounded[32];
        std::snprintf(rounded, sizeof(rounded), "%.2f", seconds);
        manifest["wall_seconds"] = std::strtod(rounded, nullptr);
        swchess::exporter::writeJsonFile((stagingDir / "manifest.json").string(), manifest, true,
                                         true);

        if (!options.keepWork) {
            fs::remove_all(workDir);
        }

        const fs::path retired =
            finalDir.parent_path() / (finalDir.filename().string() + ".old-" + stamp);
        if (fs::exists(finalDir)) {
            fs::rename(finalDir, retired);
        }
        fs::create_directories(finalDir.parent_path());
        fs::rename(stagingDir, finalDir);
        fs::remove_all(retired);
        return manifest;
    } catch (...) {
        fs::remove_all(stagingDir);
        throw;
    }
}

bool refreshCues(const std::string& outDir, const std::string& resolvedPath) {
    const fs::path manifestPath = fs::path(outDir) / "manifest.json";
    Json manifest = readJsonFile(manifestPath);
    std::string path = resolvedPath;
    if (path.empty() && manifest.contains("input") && manifest["input"].contains("resolved") &&
        !manifest["input"]["resolved"].is_null()) {
        path = manifest["input"]["resolved"].get<std::string>();
    }
    if (path.empty() || !fs::exists(path)) {
        throw std::runtime_error(manifestPath.string() + " names no resolved.json to read cues from");
    }
    const Json spec = readJsonFile(path);

    const std::string before =
        swchess::exporter::dumpPythonJson(manifest["sounds"], true) +
        swchess::exporter::dumpPythonJson(manifest["pre_sounds"], true) +
        swchess::exporter::dumpPythonJson(manifest["final_wav"], true);

    manifest["sounds"] = soundEvents(spec);
    manifest["pre_sounds"] = spec.contains("pre_sounds") && !spec.at("pre_sounds").is_null()
                                 ? spec.at("pre_sounds")
                                 : Json::array();
    manifest["final_wav"] = spec.contains("final_wav") ? spec.at("final_wav") : Json(nullptr);
    manifest["input"]["resolved"] = path;
    manifest["input"]["resolved_sha256"] = sha256File(path);

    const std::string after =
        swchess::exporter::dumpPythonJson(manifest["sounds"], true) +
        swchess::exporter::dumpPythonJson(manifest["pre_sounds"], true) +
        swchess::exporter::dumpPythonJson(manifest["final_wav"], true);

    swchess::exporter::writeJsonFile(manifestPath.string(), manifest, true, true);
    return before != after;
}

}  // namespace swchess::interp
