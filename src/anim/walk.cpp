#include "anim/walk.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "assets/ini.h"

namespace swchess::anim {
namespace {

// The delay CM.INI hands the loader when [defaults] leaves frame_delay out.
// capture.cpp passes the same one.
constexpr int kDefaultFrameDelay = 180;

std::string uppercased(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// True when every character is a digit and there is at least one.
bool isNumberedKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }
    for (char c : key) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

// Splits "0,2" into its two numbers. Returns false when the text does not
// hold two of them, and leaves whatever it did read in place.
bool parseStepValue(const std::string& raw, int* dx, int* dy) {
    std::size_t comma = raw.find(',');
    if (comma == std::string::npos) {
        *dx = iniAsInt(raw, 0);
        *dy = 0;
        return false;
    }
    *dx = iniAsInt(raw.substr(0, comma), 0);
    *dy = iniAsInt(raw.substr(comma + 1), 0);
    return true;
}

int sectionInt(const IniSection* section, const std::string& key, int fallback) {
    if (section == nullptr) {
        return fallback;
    }
    return iniAsInt(section->get(key, std::string()), fallback);
}

// Divides and rounds to the nearest whole pixel, halves away from zero.
int scaled(int value, int from, int to) {
    if (from == 0) {
        return 0;
    }
    double product = static_cast<double>(value) * static_cast<double>(to) /
                     static_cast<double>(from);
    return static_cast<int>(std::llround(product));
}

}  // namespace

const Direction kDirections[8] = {
    Direction::N,  Direction::NE, Direction::E, Direction::SE,
    Direction::S,  Direction::SW, Direction::W, Direction::NW,
};

const char* directionName(Direction direction) {
    switch (direction) {
        case Direction::N:
            return "N";
        case Direction::NE:
            return "NE";
        case Direction::E:
            return "E";
        case Direction::SE:
            return "SE";
        case Direction::S:
            return "S";
        case Direction::SW:
            return "SW";
        case Direction::W:
            return "W";
        case Direction::NW:
            break;
    }
    return "NW";
}

bool parseDirection(const std::string& text, Direction* direction) {
    std::string want = uppercased(text);
    for (Direction candidate : kDirections) {
        if (want == directionName(candidate)) {
            if (direction != nullptr) {
                *direction = candidate;
            }
            return true;
        }
    }
    return false;
}

const char* walkFitName(WalkFit fit) {
    switch (fit) {
        case WalkFit::ScaleToTarget:
            return "scale_to_target";
        case WalkFit::IniSteps:
            break;
    }
    return "ini_steps";
}

const PieceDll& sharedPieceDll(const std::string& cdDir, const std::string& piece) {
    static std::mutex lock;
    static std::map<std::string, PieceDll> cache;
    std::lock_guard<std::mutex> held(lock);
    std::string key = cdDir + "/" + uppercased(piece);
    auto found = cache.find(key);
    if (found == cache.end()) {
        found = cache.emplace(key, loadPieceDll(cdDir, uppercased(piece))).first;
    }
    return found->second;
}

WalkSequence loadWalkSection(const std::string& cdDir, const std::string& piece,
                             const std::string& section) {
    WalkSequence walk;
    walk.piece = uppercased(piece);
    walk.section = uppercased(section);
    walk.iniPath = cdDir + "/" + walk.piece + ".INI";
    walk.dllPath = cdDir + "/" + walk.piece + ".DLL";

    IniFile cm(cdDir + "/CM.INI");
    walk.frameDelayMs = sectionInt(cm.section("defaults"), "frame_delay", kDefaultFrameDelay);

    IniFile ini(walk.iniPath);
    const IniSection* found = ini.section(walk.section);
    if (found == nullptr) {
        return walk;
    }
    walk.present = true;
    walk.declaredCount = iniAsInt(found->get("count", std::string()), 0);

    // The numbered keys in the order the file writes them. The original reads
    // `count` and loops that many times, so it stops here and never looks at
    // what follows. R2.INI [US] numbers its keys 000, 002, 004 and so on, so
    // the loader must follow the file order and not assume a step of one.
    std::vector<std::string> numbered;
    for (const std::string& key : found->keys()) {
        if (isNumberedKey(key)) {
            numbered.push_back(key);
        }
    }
    walk.iniStepCount = numbered.size();
    if (walk.declaredCount <= 0) {
        return walk;
    }
    if (numbered.size() < static_cast<std::size_t>(walk.declaredCount)) {
        throw std::runtime_error(walk.iniPath + " [" + walk.section + "] declares count " +
                                 std::to_string(walk.declaredCount) + " and lists only " +
                                 std::to_string(numbered.size()) + " steps");
    }
    walk.extraKeysIgnored = numbered.size() > static_cast<std::size_t>(walk.declaredCount);

    const PieceDll& dll = sharedPieceDll(cdDir, walk.piece);
    std::unordered_map<std::string, const PieceBitmap*> byName;
    byName.reserve(dll.bitmaps.size() * 2);
    for (const PieceBitmap& bitmap : dll.bitmaps) {
        byName.emplace(bitmap.resourceName, &bitmap);
    }

    int runningX = 0;
    int runningY = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(walk.declaredCount); ++i) {
        WalkStep step;
        step.index = i;
        step.key = numbered[i];
        step.keyNumber = iniAsInt(step.key, 0);
        step.raw = found->get(step.key, std::string());
        step.malformed = !parseStepValue(step.raw, &step.dx, &step.dy);

        // The bitmap number runs one ahead of the INI key. INI key 001 of
        // AT's [S] draws AT_S002 and rotation key 000 draws AT_R001.
        char name[32];
        std::snprintf(name, sizeof(name), "%s_%s%03d", walk.piece.c_str(), walk.section.c_str(),
                      step.keyNumber + 1);
        step.resourceName = name;
        auto bitmap = byName.find(step.resourceName);
        if (bitmap == byName.end()) {
            throw std::runtime_error(walk.dllPath + " has no bitmap " + step.resourceName +
                                     " for [" + walk.section + "] step " + step.key);
        }
        step.bitmap = bitmap->second;
        step.width = static_cast<int>(step.bitmap->record.width);
        step.height = static_cast<int>(step.bitmap->record.height);

        runningX += step.dx;
        runningY += step.dy;
        step.cumulativeDx = runningX;
        step.cumulativeDy = runningY;
        walk.steps.push_back(std::move(step));
    }
    walk.totalDx = runningX;
    walk.totalDy = runningY;
    return walk;
}

WalkSequence loadWalk(const std::string& cdDir, const std::string& piece, Direction direction) {
    return loadWalkSection(cdDir, piece, directionName(direction));
}

WalkSequence loadRotation(const std::string& cdDir, const std::string& piece) {
    return loadWalkSection(cdDir, piece, kRotationSection);
}

void WalkPlayer::start(const WalkSequence* sequence, int fromX, int fromY, int toX, int toY,
                       std::int64_t nowMs) {
    sequence_ = sequence;
    positions_.clear();
    startedMs_ = nowMs;
    elapsedMs_ = 0;
    durationMs_ = 0;
    residualX_ = 0;
    residualY_ = 0;
    finished_ = sequence == nullptr || sequence->steps.empty();
    if (sequence == nullptr || sequence->steps.empty()) {
        return;
    }

    const std::size_t count = sequence->steps.size();
    durationMs_ = static_cast<std::int64_t>(count) * sequence->frameDelayMs;
    const int wantX = toX - fromX;
    const int wantY = toY - fromY;

    positions_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const WalkStep& step = sequence->steps[i];
        int offsetX = step.cumulativeDx;
        int offsetY = step.cumulativeDy;
        if (fit_ == WalkFit::ScaleToTarget) {
            if (sequence->totalDx != 0) {
                offsetX = scaled(step.cumulativeDx, sequence->totalDx, wantX);
            } else {
                // The INI never moves this walk sideways, so spread the whole
                // sideways distance evenly across the steps.
                offsetX = static_cast<int>(
                    std::llround(static_cast<double>(wantX) * static_cast<double>(i + 1) /
                                 static_cast<double>(count)));
            }
            if (sequence->totalDy != 0) {
                offsetY = scaled(step.cumulativeDy, sequence->totalDy, wantY);
            } else {
                offsetY = static_cast<int>(
                    std::llround(static_cast<double>(wantY) * static_cast<double>(i + 1) /
                                 static_cast<double>(count)));
            }
            if (i + 1 == count) {
                // Rounding can leave the last step a pixel short, so put it
                // on the target and let the step before it absorb the error.
                offsetX = wantX;
                offsetY = wantY;
            }
        }
        WalkDraw draw;
        draw.visible = true;
        draw.bitmap = step.bitmap;
        draw.x = fromX + offsetX;
        draw.y = fromY + offsetY;
        draw.width = step.width;
        draw.height = step.height;
        draw.stepIndex = i;
        positions_.push_back(draw);
    }
    residualX_ = toX - positions_.back().x;
    residualY_ = toY - positions_.back().y;
}

WalkDraw WalkPlayer::drawAt(std::int64_t ms) const {
    WalkDraw draw;
    if (positions_.empty()) {
        return draw;
    }
    if (ms >= durationMs_) {
        return draw;
    }
    std::size_t at = static_cast<std::size_t>(ms / sequence_->frameDelayMs);
    if (at >= positions_.size()) {
        at = positions_.size() - 1;
    }
    return positions_[at];
}

WalkUpdate WalkPlayer::advance(std::int64_t nowMs) {
    WalkUpdate update;
    if (sequence_ == nullptr) {
        update.finished = true;
        return update;
    }
    if (!finished_) {
        std::int64_t elapsed = nowMs - startedMs_;
        if (elapsed > elapsedMs_) {
            elapsedMs_ = elapsed;
        }
        if (elapsedMs_ >= durationMs_) {
            finished_ = true;
        }
    }
    update.draw = drawAt(elapsedMs_);
    update.finished = finished_;
    return update;
}

void WalkPlayer::skip() {
    if (sequence_ == nullptr) {
        finished_ = true;
        return;
    }
    if (elapsedMs_ < durationMs_) {
        elapsedMs_ = durationMs_;
    }
    finished_ = true;
}

}  // namespace swchess::anim
