#include "anim/interp.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace swchess::anim {
namespace {

using nlohmann::json;

InterpKind kindFromName(const std::string& name, const std::string& what) {
    if (name == "blank") {
        return InterpKind::Blank;
    }
    if (name == "copy") {
        return InterpKind::Copy;
    }
    if (name == "interp") {
        return InterpKind::Interp;
    }
    if (name == "hold") {
        return InterpKind::Hold;
    }
    throw std::runtime_error(what + ": unknown frame kind " + name);
}

CanvasRect rectFrom(const json& node) {
    CanvasRect rect;
    rect.x = node.at("x").get<int>();
    rect.y = node.at("y").get<int>();
    rect.width = node.at("w").get<int>();
    rect.height = node.at("h").get<int>();
    return rect;
}

// Reads the name, mode, duration and start time the tool copies out of the
// timeline. A sound carries its own t_ms, the millisecond the original starts
// it, which a sync sound reaches before its pose appears. A manifest written
// before that field existed leaves startMs at whatever the caller set.
void fillSound(const json& node, InterpSound* sound) {
    sound->name = node.at("name").get<std::string>();
    if (node.contains("t_ms") && !node.at("t_ms").is_null()) {
        sound->startMs = node.at("t_ms").get<std::int64_t>();
    }
    if (node.contains("mode") && !node.at("mode").is_null()) {
        sound->mode = node.at("mode").get<std::string>();
    }
    if (node.contains("duration_ms") && !node.at("duration_ms").is_null()) {
        sound->durationMs = node.at("duration_ms").get<std::int64_t>();
    }
}

bool fileExists(const std::string& path) {
    std::ifstream probe(path, std::ios::binary);
    return probe.good();
}

// Decodes frames [first, last) of `frames` from `directory`.
void decodeRange(const std::string& directory, std::vector<InterpFrame>* frames, std::size_t first,
                 std::size_t last, std::string* error) {
    try {
        for (std::size_t i = first; i < last; ++i) {
            (*frames)[i].image = readPng(directory + "/" + (*frames)[i].file);
        }
    } catch (const std::exception& problem) {
        *error = problem.what();
    }
}

}  // namespace

const char* interpKindName(InterpKind kind) {
    switch (kind) {
        case InterpKind::Copy:
            return "copy";
        case InterpKind::Interp:
            return "interp";
        case InterpKind::Hold:
            return "hold";
        case InterpKind::Blank:
            break;
    }
    return "blank";
}

const InterpFrame* InterpSequence::frameAt(double ms) const {
    if (frames.empty() || ms < frames.front().tMs ||
        ms >= static_cast<double>(endMs)) {
        return nullptr;
    }
    // The last frame whose time has already come.
    std::size_t low = 0;
    std::size_t high = frames.size();
    while (high - low > 1) {
        const std::size_t middle = low + (high - low) / 2;
        if (frames[middle].tMs <= ms) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return &frames[low];
}

std::optional<InterpSequence> loadInterp(const std::string& assetsDir,
                                         const std::string& captureName) {
    const std::string directory = assetsDir + "/captures/" + captureName + "/interp60";
    const std::string manifestPath = directory + "/manifest.json";
    if (!fileExists(manifestPath)) {
        return std::nullopt;
    }

    json manifest;
    {
        std::ifstream file(manifestPath, std::ios::binary);
        if (!file) {
            throw std::runtime_error("cannot open " + manifestPath);
        }
        try {
            file >> manifest;
        } catch (const std::exception& problem) {
            throw std::runtime_error(manifestPath + ": " + problem.what());
        }
    }

    InterpSequence sequence;
    try {
        sequence.name = manifest.at("capture").get<std::string>();
        sequence.directory = directory;
        sequence.fps = manifest.at("fps").get<int>();
        sequence.canvas = rectFrom(manifest.at("canvas"));
        sequence.frameRect = rectFrom(manifest.at("frame_rect"));
        sequence.endMs = manifest.at("end_ms").get<std::int64_t>();

        for (const json& node : manifest.at("sounds")) {
            InterpSound sound;
            sound.poseIndex = node.at("pose").get<std::size_t>();
            // The event's own t_ms is the pose time in an older manifest and
            // the sound's start time in a current one. The sound's own t_ms
            // wins when the tool wrote one.
            sound.startMs = node.at("t_ms").get<std::int64_t>();
            fillSound(node.at("sound"), &sound);
            sequence.sounds.push_back(sound);
        }
        for (const json& node : manifest.at("pre_sounds")) {
            InterpSound sound;
            sound.startMs = node.at("t_ms").get<std::int64_t>();
            fillSound(node, &sound);
            sequence.preSounds.push_back(sound);
        }
        const json& finalNode = manifest.at("final_wav");
        if (!finalNode.is_null()) {
            sequence.hasFinalSound = true;
            fillSound(finalNode, &sequence.finalSound);
        }

        const json& frames = manifest.at("frames");
        sequence.frames.resize(frames.size());
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const json& node = frames[i];
            InterpFrame& frame = sequence.frames[i];
            frame.file = node.at("file").get<std::string>();
            frame.tMs = node.at("t_ms").get<double>();
            frame.durationMs = node.at("duration_ms").get<double>();
            frame.kind = kindFromName(node.at("kind").get<std::string>(), manifestPath);
            frame.source = node.at("source").get<std::vector<int>>();
            const json& step = node.at("s");
            if (!step.is_null()) {
                frame.step = step.get<double>();
                frame.hasStep = true;
            }
        }
    } catch (const json::exception& problem) {
        throw std::runtime_error(manifestPath + ": " + problem.what());
    }

    if (sequence.frames.empty()) {
        throw std::runtime_error(manifestPath + ": the manifest lists no frames");
    }

    // Reading and inflating 653 pictures is the slow part, so split it across
    // the cores. Each worker owns its own slice of the vector and writes
    // nothing another worker reads.
    const std::size_t count = sequence.frames.size();
    unsigned int workers = std::thread::hardware_concurrency();
    if (workers == 0) {
        workers = 1;
    }
    workers = std::min<std::size_t>(workers, count);
    std::vector<std::string> errors(workers);
    std::vector<std::thread> threads;
    for (unsigned int w = 0; w < workers; ++w) {
        const std::size_t first = count * w / workers;
        const std::size_t last = count * (w + 1) / workers;
        threads.emplace_back(decodeRange, std::cref(directory), &sequence.frames, first, last,
                             &errors[w]);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const std::string& error : errors) {
        if (!error.empty()) {
            throw std::runtime_error(error);
        }
    }

    for (const InterpFrame& frame : sequence.frames) {
        if (frame.image.width != sequence.frameRect.width ||
            frame.image.height != sequence.frameRect.height) {
            throw std::runtime_error(directory + "/" + frame.file +
                                     " does not match the manifest frame rectangle");
        }
    }
    return sequence;
}

}  // namespace swchess::anim
