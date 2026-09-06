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
#include "anim/interp.h"

namespace swchess::anim {

// Which set of images the player picks from.
//
// Original120ms uses the authored poses at the recovered frame delay.
// Interpolated60 picks the generated 60 frames per second pictures that
// tools/interp writes. It needs an InterpSequence, so the player rejects it
// until start receives one.
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

// The picture the caller should draw.
//
// The original cadence fills `record` with a decoded ANX pose. The
// interpolated cadence fills `frame` with an RGBA picture instead. Exactly one
// of the two is set whenever the player has something on the canvas. A blank
// interpolated frame sets `frame` and leaves `visible` false, which is what the
// original cadence does before the first pose reaches the screen.
struct DrawState {
    bool visible = false;
    const AnxRecord* record = nullptr;
    const InterpFrame* frame = nullptr;
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

    // Switches which pictures the player draws. The animation clock and the
    // cues that already fired stay where they are, so a toggle mid capture
    // neither rewinds nor replays. Throws when Interpolated60 is asked for and
    // start received no InterpSequence.
    void setCadence(Cadence cadence);
    Cadence cadence() const { return cadence_; }

    // Begin `timeline` at wall time `nowMs`. The timeline must outlive the
    // player. Passing null stops the player. This form has no interpolated
    // pictures, so the cadence falls back to Original120ms.
    void start(const CaptureTimeline* timeline, std::int64_t nowMs);

    // The same, with the 60 frames per second pictures for the same capture.
    // Both must outlive the player. `interp` may be null, which behaves like
    // the form above. The two must agree: same end time, and the same sound
    // cues at the same times in the same order. When they do not, the player
    // writes one line to stderr, drops the pictures, and falls back to
    // Original120ms rather than throwing, so a stale manifest on disk cannot
    // end the game.
    void start(const CaptureTimeline* timeline, const InterpSequence* interp,
               std::int64_t nowMs);

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
    const InterpSequence* interp() const { return interp_; }

    // Every cue the timeline holds, in time order.
    const std::vector<SoundEvent>& schedule() const { return schedule_; }

    // How many cues have fired so far.
    std::size_t firedCount() const { return next_; }

private:
    DrawState drawAt(std::int64_t ms) const;

    const CaptureTimeline* timeline_ = nullptr;
    const InterpSequence* interp_ = nullptr;
    Cadence cadence_ = Cadence::Original120ms;
    std::vector<SoundEvent> schedule_;
    std::size_t next_ = 0;
    std::size_t pose_ = 0;  // index into timeline_->poses of the pose on screen
    std::int64_t startedMs_ = 0;
    std::int64_t elapsedMs_ = 0;
    bool finished_ = true;
};

}  // namespace swchess::anim
