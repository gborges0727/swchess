#include "interp/timeline.h"

#include <set>

namespace swchess::interp {

const char* frameKindName(FrameKind kind) {
    switch (kind) {
        case FrameKind::Blank: return "blank";
        case FrameKind::Copy: return "copy";
        case FrameKind::Hold: return "hold";
        case FrameKind::Interp: return "interp";
    }
    return "blank";
}

std::int64_t sampleCount(std::int64_t endMs, int fps) {
    return (Fraction(endMs) * Fraction(fps, 1000)).ceiling();
}

std::vector<PlannedFrame> planFrames(const Json& spec, int fps) {
    const Json& poses = spec.at("poses");
    std::vector<Fraction> times;
    times.reserve(poses.size());
    for (const Json& pose : poses) {
        times.emplace_back(pose.at("t_ms").get<std::int64_t>());
    }
    std::set<int> cuts;
    if (spec.contains("cuts") && !spec.at("cuts").is_null()) {
        for (const Json& cut : spec.at("cuts")) {
            cuts.insert(cut.get<int>());
        }
    }
    const std::int64_t endMs = spec.at("end_ms").get<std::int64_t>();
    const Fraction step(1000, fps);

    std::vector<PlannedFrame> frames;
    std::size_t k = 0;
    const std::int64_t total = sampleCount(endMs, fps);
    for (std::int64_t n = 0; n < total; ++n) {
        const Fraction t = Fraction(n) * step;
        PlannedFrame frame;
        frame.t = t;
        if (times.empty() || t < times[0]) {
            frame.kind = FrameKind::Blank;
            frames.push_back(frame);
            continue;
        }
        while (k + 1 < poses.size() && times[k + 1] <= t) {
            ++k;
        }
        const int currentIndex = poses[k].at("index").get<int>();
        if (t == times[k]) {
            frame.kind = FrameKind::Copy;
            frame.source = {currentIndex};
        } else if (k + 1 >= poses.size()) {
            frame.kind = FrameKind::Hold;
            frame.source = {currentIndex};
        } else if (cuts.count(poses[k + 1].at("index").get<int>()) != 0) {
            frame.kind = FrameKind::Hold;
            frame.source = {currentIndex};
        } else {
            frame.kind = FrameKind::Interp;
            frame.source = {currentIndex, poses[k + 1].at("index").get<int>()};
            frame.step = (t - times[k]) / (times[k + 1] - times[k]);
            frame.hasStep = true;
            frame.pose = k;
        }
        frames.push_back(frame);
    }

    const Fraction end(endMs);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const Fraction next = i + 1 < frames.size() ? frames[i + 1].t : end;
        frames[i].duration = next - frames[i].t;
    }
    return frames;
}

Json soundEvents(const Json& spec) {
    Json out = Json::array();
    for (const Json& pose : spec.at("poses")) {
        if (!pose.contains("sound") || pose.at("sound").is_null()) {
            continue;
        }
        const Json& sound = pose.at("sound");
        Json entry = Json::object();
        entry["pose"] = pose.at("index");
        // Older files wrote no t_ms inside the sound, and for those the pose
        // time is the best guess left.
        if (sound.contains("t_ms") && !sound.at("t_ms").is_null()) {
            entry["t_ms"] = sound.at("t_ms");
        } else {
            entry["t_ms"] = pose.at("t_ms");
        }
        entry["sound"] = sound;
        out.push_back(entry);
    }
    return out;
}

}  // namespace swchess::interp
