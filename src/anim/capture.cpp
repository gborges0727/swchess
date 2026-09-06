#include "anim/capture.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <stdexcept>

#include "assets/ini.h"
#include "assets/wav.h"

namespace swchess::anim {
namespace {

// Table 2 of the ANX header starts here and holds 150 slots of four bytes.
// Each slot is a signed 16 bit x followed by a signed 16 bit y.
constexpr std::size_t kPositionTable = 0x25c;
constexpr std::size_t kPositionSlots = 150;

// The built in defaults the loader passes to the profile calls.
constexpr int kDefaultFrameDelay = 180;
constexpr int kDefaultOffsetX = 215;
constexpr int kDefaultOffsetY = 100;
constexpr int kDefaultHold = 1000;

std::string uppercased(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        throw std::runtime_error("cannot size " + path);
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty() && std::fread(data.data(), 1, data.size(), file) != data.size()) {
        std::fclose(file);
        throw std::runtime_error("short read on " + path);
    }
    std::fclose(file);
    return data;
}

std::int16_t readI16(const std::vector<std::uint8_t>& data, std::size_t at) {
    std::uint16_t raw = static_cast<std::uint16_t>(data[at]) |
                        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[at + 1]) << 8);
    return static_cast<std::int16_t>(raw);
}

// One entry of the ANX position table, before the section offset is added.
struct RawPosition {
    int x = 0;
    int y = 0;
};

std::vector<RawPosition> readPositions(const std::string& path) {
    std::vector<std::uint8_t> data = readFile(path);
    std::size_t need = kPositionTable + kPositionSlots * 4;
    if (data.size() < need) {
        throw std::runtime_error("ANX shorter than its position table: " + path);
    }
    std::vector<RawPosition> out;
    out.reserve(kPositionSlots);
    for (std::size_t i = 0; i < kPositionSlots; ++i) {
        std::size_t at = kPositionTable + i * 4;
        out.push_back(RawPosition{readI16(data, at), readI16(data, at + 2)});
    }
    return out;
}

// Reads one integer key, falling back the way GetPrivateProfileInt does.
int sectionInt(const IniSection* section, const char* key, int fallback) {
    if (section == nullptr || !section->has(key)) {
        return fallback;
    }
    return iniAsInt(section->get(key), fallback);
}

SoundMode modeFromPause(int pause) {
    if (pause == 1) {
        return SoundMode::Sync;
    }
    if (pause == 2) {
        return SoundMode::WaitPrevious;
    }
    return SoundMode::Async;
}

}  // namespace

const char* soundModeName(SoundMode mode) {
    switch (mode) {
        case SoundMode::Sync:
            return "sync";
        case SoundMode::WaitPrevious:
            return "wait_previous";
        case SoundMode::Async:
            break;
    }
    return "async";
}

SoundCatalog::SoundCatalog(const std::string& cdDir) {
    for (const WaveResource& resource : loadAudioDll(cdDir)) {
        std::int64_t ms = static_cast<std::int64_t>(resource.sound.durationSeconds() * 1000.0 + 0.5);
        // Two records share the name GRUNT1.WAV and hold identical audio, so
        // whichever wins gives the same answer.
        durations_[uppercased(resource.name)] = ms;
    }
}

bool SoundCatalog::find(const std::string& cue, std::string* resource,
                        std::int64_t* durationMs) const {
    std::string want = uppercased(cue);
    auto found = durations_.find(want);
    if (found == durations_.end()) {
        return false;
    }
    if (resource != nullptr) {
        *resource = found->first;
    }
    if (durationMs != nullptr) {
        *durationMs = found->second;
    }
    return true;
}

const SoundCatalog& sharedSoundCatalog(const std::string& cdDir) {
    static std::mutex guard;
    static std::map<std::string, SoundCatalog> cache;
    std::lock_guard<std::mutex> lock(guard);
    auto found = cache.find(cdDir);
    if (found == cache.end()) {
        found = cache.emplace(cdDir, SoundCatalog(cdDir)).first;
    }
    return found->second;
}

CaptureTimeline loadCapture(const std::string& cdDir, const std::string& name) {
    return loadCapture(cdDir, name, sharedSoundCatalog(cdDir));
}

CaptureTimeline loadCapture(const std::string& cdDir, const std::string& name,
                            const SoundCatalog& sounds) {
    if (name.size() < 4) {
        throw std::runtime_error("capture name must have four characters: " + name);
    }
    CaptureTimeline timeline;
    timeline.name = name;
    // The file holds every capture with the same attacker, so its name is the
    // first two characters of the code. The section uses all four.
    timeline.iniPath = cdDir + "/" + name.substr(0, 2) + ".INI";

    std::string anxPath = cdDir + "/" + name + ".ANX";
    timeline.anx = loadAnx(anxPath);
    std::vector<RawPosition> positions = readPositions(anxPath);

    IniFile cm(cdDir + "/CM.INI");
    timeline.frameDelayMs = sectionInt(cm.section("defaults"), "frame_delay", kDefaultFrameDelay);

    IniFile ini(timeline.iniPath);
    const IniSection* list = ini.section(name);
    if (list == nullptr) {
        throw std::runtime_error("no [" + name + "] section in " + timeline.iniPath);
    }
    const IniSection* offset = ini.section(name + "_OFFSET");
    timeline.offsetX = sectionInt(offset, "x", kDefaultOffsetX);
    timeline.offsetY = sectionInt(offset, "y", kDefaultOffsetY);
    timeline.holdMs = sectionInt(offset, "hold", kDefaultHold);

    std::string endCue = offset != nullptr ? offset->get("wav") : std::string();
    if (!endCue.empty()) {
        timeline.hasEndSound = true;
        timeline.endSound.cue = endCue;
        // The end cue always blocks. The original plays it with SND_SYNC.
        timeline.endSound.mode = SoundMode::Sync;
        timeline.endSound.resolved =
            sounds.find(endCue, &timeline.endSound.resource, &timeline.endSound.durationMs);
    }

    const std::vector<std::string>& keys = list->keys();
    timeline.entryCount = keys.size();
    if (timeline.entryCount > timeline.anx.timeline.size()) {
        throw std::runtime_error("[" + name + "] lists more poses than " + anxPath + " holds");
    }
    if (timeline.entryCount > kPositionSlots) {
        throw std::runtime_error("[" + name + "] lists more poses than the position table holds");
    }

    // Walk the loop in FUN_1058_0a0a. Each iteration starts at t0, may block on
    // a sound, then draws and waits until t0 plus the frame delay. Index 0 is
    // decoded and its sound starts, but it never reaches the screen.
    std::int64_t t0 = 0;
    bool waitPending = false;
    std::int64_t pendingEndMs = 0;
    std::int64_t lastStart = 0;
    std::int64_t lastBlock = 0;

    for (std::size_t i = 0; i < timeline.entryCount; ++i) {
        const IniSection* frame = ini.section(keys[i]);
        std::string cue = frame != nullptr ? frame->get("wav") : std::string();

        CaptureSound sound;
        bool hasSound = !cue.empty();
        std::int64_t block = 0;
        if (hasSound) {
            // A pause without a wav is ignored, so pause is only read here.
            sound.cue = cue;
            sound.mode = modeFromPause(sectionInt(frame, "pause", 0));
            sound.resolved = sounds.find(cue, &sound.resource, &sound.durationMs);
            if (waitPending) {
                block += std::max<std::int64_t>(0, pendingEndMs - t0);
                waitPending = false;
            }
            sound.startMs = t0 + block;
            if (sound.mode == SoundMode::Sync) {
                block += sound.durationMs;
            } else if (sound.mode == SoundMode::WaitPrevious) {
                waitPending = true;
                pendingEndMs = sound.startMs + sound.durationMs;
            }
        }

        if (i >= 1) {
            CapturePose pose;
            pose.index = i;
            pose.key = keys[i];
            pose.startMs = t0 + block;
            pose.recordOffset = timeline.anx.timeline[i];
            pose.x = positions[i].x + timeline.offsetX;
            pose.y = positions[i].y + timeline.offsetY;
            pose.hasSound = hasSound;
            pose.sound = sound;
            timeline.poses.push_back(pose);
        } else if (hasSound && sound.resolved) {
            // Pose 0 never reaches the screen, but its sound still starts.
            timeline.preSounds.push_back(sound);
        }

        lastStart = t0;
        lastBlock = block;
        std::int64_t step = std::max(block, timeline.frameDelayMs);
        timeline.blockedMs += step - timeline.frameDelayMs;
        t0 += step;
    }

    // Point every pose at its record and copy the size out of it.
    for (CapturePose& pose : timeline.poses) {
        auto found = timeline.anx.records.find(pose.recordOffset);
        if (found == timeline.anx.records.end()) {
            throw std::runtime_error("pose " + pose.key + " names a record " + anxPath +
                                     " does not hold");
        }
        pose.record = &found->second;
        pose.width = found->second.width;
        pose.height = found->second.height;
    }

    // The loop ends after the last iteration's own wait. The hold then runs
    // from that iteration's start, so a hold shorter than the frame delay adds
    // nothing.
    std::int64_t loopEnd = lastStart + std::max(lastBlock, timeline.frameDelayMs);
    timeline.holdEndMs = std::max(loopEnd, lastStart + timeline.holdMs);
    if (timeline.entryCount == 0) {
        timeline.holdEndMs = timeline.holdMs;
    }
    timeline.endMs = timeline.holdEndMs;
    if (timeline.hasEndSound) {
        timeline.blockedMs += timeline.endSound.durationMs;
        timeline.endSound.startMs = timeline.holdEndMs;
        timeline.endMs += timeline.endSound.durationMs;
    }
    return timeline;
}

std::vector<std::string> allCaptureNames() {
    const char* colours = "WB";
    const char* pieces = "KQRBNP";
    std::vector<std::string> names;
    names.reserve(72);
    for (int colour = 0; colour < 2; ++colour) {
        for (int attacker = 0; attacker < 6; ++attacker) {
            for (int defender = 0; defender < 6; ++defender) {
                std::string name;
                name += colours[colour];
                name += pieces[attacker];
                name += colours[1 - colour];
                name += pieces[defender];
                names.push_back(name);
            }
        }
    }
    return names;
}

}  // namespace swchess::anim
