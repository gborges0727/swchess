#include "anim/walk.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "assets/cdfs.h"
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
    walk.iniPath = resolveCdFile(cdDir, walk.piece + ".INI").string();
    walk.dllPath = resolveCdFile(cdDir, walk.piece + ".DLL").string();

    IniFile cm(resolveCdFile(cdDir, "CM.INI").string());
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


std::vector<PathPoint> linePoints(int x0, int y0, int x1, int y1) {
    std::vector<PathPoint> points;
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    const int stepY = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    const int spanX = dx < 0 ? -dx : dx;
    const int spanY = dy < 0 ? -dy : dy;
    const int major = spanX > spanY ? spanX : spanY;
    points.reserve(static_cast<std::size_t>(major) + 1);
    if (major == 0) {
        points.push_back(PathPoint{x0, y0});
        return points;
    }
    // One point per step along the longer axis, which is what LineDDA hands
    // its callback. The shorter axis carries the remainder and rounds to the
    // nearest pixel.
    for (int i = 0; i <= major; ++i) {
        PathPoint point;
        if (spanX >= spanY) {
            point.x = x0 + stepX * i;
            point.y = y0 + stepY * ((spanY * i + major / 2) / major);
        } else {
            point.y = y0 + stepY * i;
            point.x = x0 + stepX * ((spanX * i + major / 2) / major);
        }
        points.push_back(point);
    }
    return points;
}

std::vector<PathPoint> walkPath(int x0, int y0, int x1, int y1) {
    const std::vector<PathPoint> all = linePoints(x0, y0, x1, y1);
    std::vector<PathPoint> kept;
    kept.reserve(all.size() / 2 + 2);
    for (std::size_t i = 0; i < all.size(); i += 2) {
        kept.push_back(all[i]);
    }
    if (!all.empty() && kept.back() != all.back()) {
        kept.push_back(all.back());
    }
    return kept;
}

Direction walkDirection(int fileDelta, int rankDelta, bool turned) {
    // Board rows grow downward on the screen, so a move toward rank 1 walks
    // south. FUN_1068_0013 negates both deltas when the board is turned.
    int dx = fileDelta;
    int dy = -rankDelta;
    if (turned) {
        dx = -dx;
        dy = -dy;
    }
    if (dx == 0) {
        return dy > 0 ? Direction::S : Direction::N;
    }
    if (dx < 0) {
        if (dy > 0) return Direction::SW;
        if (dy < 0) return Direction::NW;
        return Direction::W;
    }
    if (dy > 0) return Direction::SE;
    if (dy < 0) return Direction::NE;
    return Direction::E;
}

void WalkPlayer::start(const WalkSequence* sequence, std::vector<PathPoint> path,
                       std::int64_t nowMs) {
    sequence_ = sequence;
    path_ = std::move(path);
    positions_.clear();
    startedMs_ = nowMs;
    elapsedMs_ = 0;
    durationMs_ = 0;
    sliding_ = sequence == nullptr || sequence->steps.empty();
    finished_ = path_.size() < 2;
    if (path_.empty()) {
        return;
    }
    build();
    if (finished_) {
        elapsedMs_ = durationMs_;
    }
}

void WalkPlayer::start(const WalkSequence* sequence, int fromX, int fromY, int toX, int toY,
                       std::int64_t nowMs) {
    start(sequence, walkPath(fromX, fromY, toX, toY), nowMs);
}

void WalkPlayer::build() {
    const std::size_t last = path_.size() - 1;
    std::size_t at = 0;
    std::size_t frame = 0;
    while (true) {
        WalkDraw draw;
        draw.visible = true;
        draw.x = path_[at].x;
        draw.y = path_[at].y;
        draw.stepIndex = frame;
        draw.pointIndex = at;
        draw.progress = last == 0 ? 1.0 : static_cast<double>(at) / static_cast<double>(last);
        if (!sliding_) {
            // The frames cycle, so a path longer than the sequence starts the
            // sequence again rather than standing still on its last picture.
            const WalkStep& step = sequence_->steps[frame % sequence_->steps.size()];
            draw.bitmap = step.bitmap;
            draw.width = step.width;
            draw.height = step.height;
        }
        positions_.push_back(draw);
        if (at >= last) {
            break;
        }
        const std::size_t advance = static_cast<std::size_t>(kWalkPointsPerFrame);
        at = at + advance >= last ? last : at + advance;
        ++frame;
    }
    // The last frame stands on the screen for its own 100 ms before the piece
    // belongs to the board again.
    durationMs_ = static_cast<std::int64_t>(positions_.size()) * kWalkFrameMs;
}

WalkUpdate WalkPlayer::advance(std::int64_t nowMs) {
    WalkUpdate update;
    if (positions_.empty()) {
        update.finished = true;
        return update;
    }
    if (!finished_) {
        const std::int64_t elapsed = nowMs - startedMs_;
        if (elapsed > elapsedMs_) {
            elapsedMs_ = elapsed;
        }
        if (elapsedMs_ >= durationMs_) {
            finished_ = true;
        }
    }
    std::size_t at = static_cast<std::size_t>(elapsedMs_ / kWalkFrameMs);
    if (at >= positions_.size()) {
        at = positions_.size() - 1;
    }
    update.draw = positions_[at];
    // The enhanced cadence carries the piece between this point and the next
    // one by the clock. The two points are eight pixels apart and 100 ms
    // apart, so a caller drawing 60 times a second moves it about 1.3 pixels
    // a frame instead of standing still for six frames and then jumping.
    if (cadence_ == Cadence::Interpolated60 && at + 1 < positions_.size()) {
        const double part = static_cast<double>(elapsedMs_ - static_cast<std::int64_t>(at) *
                                                                 kWalkFrameMs) /
                            static_cast<double>(kWalkFrameMs);
        const WalkDraw& here = positions_[at];
        const WalkDraw& next = positions_[at + 1];
        update.draw.x = here.x + static_cast<int>(std::lround((next.x - here.x) * part));
        update.draw.y = here.y + static_cast<int>(std::lround((next.y - here.y) * part));
        update.draw.progress = here.progress + (next.progress - here.progress) * part;
    }
    update.finished = finished_;
    return update;
}

void WalkPlayer::skip() {
    if (elapsedMs_ < durationMs_) {
        elapsedMs_ = durationMs_;
    }
    finished_ = true;
}

std::vector<TurnPose> turnInPlace(const WalkSequence& rotation, int baseX, int baseY) {
    std::vector<TurnPose> poses;
    poses.reserve(rotation.steps.size());
    for (const WalkStep& step : rotation.steps) {
        TurnPose pose;
        pose.bitmap = step.bitmap;
        pose.x = baseX + step.dx;
        pose.y = baseY + step.dy;
        pose.width = step.width;
        pose.height = step.height;
        poses.push_back(pose);
    }
    return poses;
}

}  // namespace swchess::anim
