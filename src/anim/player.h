// Plays one resolved capture timeline against an animation clock.
//
// The player counts no frames. Every call hands it the current animation time
// in milliseconds, and it answers with the pose that belongs on the canvas at
// that time plus the sound cues that came due since the last call. A caller
// that advances once per display refresh and a caller that advances every
// millisecond see the same cues in the same order.
#pragma once

#include <cstdint>
#include <vector>

#include "anim/capture.h"

namespace swchess::anim {

// Which set of images the player picks from.
//
// Original120ms uses the authored poses at the recovered frame delay.
// Interpolated60 will pick the generated 60 frames per second images once
// milestone 4 produces them. The player rejects it for now.
enum class Cadence {
    Original120ms,
    Interpolated60,
};

const char* cadenceName(Cadence cadence);

// One cue the player reports. `poseIndex` names the pose that carries it, or
// kEndSoundIndex for the [NAME_OFFSET] cue that plays after the hold.
struct SoundEvent {
    static constexpr std::size_t kEndSoundIndex = static_cast<std::size_t>(-1);

    std::size_t poseIndex = 0;
    const CaptureSound* sound = nullptr;
    std::int64_t timeMs = 0;
    bool cancelled = false;  // true when skip ended the capture before this cue
};

// The pose the caller should draw.
struct DrawState {
    bool visible = false;
    const AnxRecord* record = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::size_t poseIndex = 0;  // the INI key position, 1 or more
};

// What one advance produced.
struct PlayerUpdate {
    DrawState draw;
    std::vector<SoundEvent> sounds;  // cues that came due on this call
    bool finished = false;
};

class CapturePlayer {
public:
    CapturePlayer() = default;

    void setCadence(Cadence cadence);
    Cadence cadence() const { return cadence_; }

    // Begin `timeline` at wall time `nowMs`. The timeline must outlive the
    // player. Passing null stops the player.
    void start(const CaptureTimeline* timeline, std::int64_t nowMs);

    // Move the clock to `nowMs` and report what changed. Time never runs
    // backwards here: a smaller `nowMs` than the last one leaves the clock
    // where it was.
    PlayerUpdate advance(std::int64_t nowMs);

    // End the capture now. Returns the cues that never fired, each marked
    // cancelled. The caller stops whatever is already sounding.
    std::vector<SoundEvent> skip();

    bool isFinished() const { return finished_; }
    bool isRunning() const { return timeline_ != nullptr && !finished_; }

    // Animation time since start, in milliseconds.
    std::int64_t elapsedMs() const { return elapsedMs_; }

    const CaptureTimeline* timeline() const { return timeline_; }

    // Every cue the timeline holds, in time order.
    const std::vector<SoundEvent>& schedule() const { return schedule_; }

    // How many cues have fired so far.
    std::size_t firedCount() const { return next_; }

private:
    DrawState drawAt(std::int64_t ms) const;

    const CaptureTimeline* timeline_ = nullptr;
    Cadence cadence_ = Cadence::Original120ms;
    std::vector<SoundEvent> schedule_;
    std::size_t next_ = 0;
    std::size_t pose_ = 0;  // index into timeline_->poses of the pose on screen
    std::int64_t startedMs_ = 0;
    std::int64_t elapsedMs_ = 0;
    bool finished_ = true;
};

}  // namespace swchess::anim
