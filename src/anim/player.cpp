#include "anim/player.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace swchess::anim {

const char* cadenceName(Cadence cadence) {
    switch (cadence) {
        case Cadence::Interpolated60:
            return "interpolated60";
        case Cadence::Original120ms:
            break;
    }
    return "original120ms";
}

void CapturePlayer::setCadence(Cadence cadence) {
    if (cadence == Cadence::Interpolated60 && interp_ == nullptr) {
        throw std::runtime_error("the interpolated cadence has no pictures for this capture");
    }
    // The clock, the pose cursor and the fired count all stay put, so the
    // caller sees the same animation time and no cue plays twice.
    cadence_ = cadence;
}

namespace {

// Refuses an interpolated sequence whose cues differ from the timeline's. The
// tool copies the cues out of the same timeline, so a mismatch means the frames
// on disk belong to a different capture or a stale extraction.
void requireSameCues(const CaptureTimeline& timeline, const InterpSequence& interp,
                     const std::vector<SoundEvent>& schedule) {
    const std::string where = timeline.name + " interp60";
    if (interp.endMs != timeline.endMs) {
        throw std::runtime_error(where + " ends at a different time than the timeline");
    }
    std::size_t at = 0;
    std::size_t preAt = 0;
    for (const SoundEvent& event : schedule) {
        if (event.poseIndex == SoundEvent::kEndSoundIndex) {
            if (!interp.hasFinalSound || interp.finalSound.name != event.sound->resource) {
                throw std::runtime_error(where + " disagrees about the final sound");
            }
            continue;
        }
        if (event.poseIndex == 0) {
            // A pre-sound: pose 0's own cue, fired at time 0 without a pose.
            if (preAt >= interp.preSounds.size()) {
                throw std::runtime_error(where + " is missing a pre-sound cue");
            }
            const InterpSound& mine = interp.preSounds[preAt];
            ++preAt;
            if (mine.startMs != event.timeMs || mine.name != event.sound->resource ||
                mine.durationMs != event.sound->durationMs) {
                throw std::runtime_error(where + " disagrees about the pre-sound cue");
            }
            continue;
        }
        if (at >= interp.sounds.size()) {
            throw std::runtime_error(where + " is missing a sound cue");
        }
        const InterpSound& mine = interp.sounds[at];
        ++at;
        if (mine.poseIndex != event.poseIndex || mine.startMs != event.timeMs ||
            mine.name != event.sound->resource || mine.durationMs != event.sound->durationMs) {
            throw std::runtime_error(where + " disagrees about the cue on pose " +
                                     std::to_string(event.poseIndex) + ": manifest pose " +
                                     std::to_string(mine.poseIndex) + " at " +
                                     std::to_string(mine.startMs) + " ms " + mine.name + " " +
                                     std::to_string(mine.durationMs) + " ms, timeline at " +
                                     std::to_string(event.timeMs) + " ms " +
                                     event.sound->resource + " " +
                                     std::to_string(event.sound->durationMs) + " ms");
        }
    }
    if (at != interp.sounds.size()) {
        throw std::runtime_error(where + " carries a sound cue the timeline does not");
    }
    if (preAt != interp.preSounds.size()) {
        throw std::runtime_error(where + " carries a pre-sound cue the timeline does not");
    }
    if (interp.hasFinalSound && !timeline.hasEndSound) {
        throw std::runtime_error(where + " carries a final sound the timeline does not");
    }
}

}  // namespace

void CapturePlayer::start(const CaptureTimeline* timeline, std::int64_t nowMs) {
    start(timeline, nullptr, nowMs);
}

void CapturePlayer::start(const CaptureTimeline* timeline, const InterpSequence* interp,
                          std::int64_t nowMs) {
    timeline_ = timeline;
    interp_ = interp;
    schedule_.clear();
    next_ = 0;
    pose_ = 0;
    startedMs_ = nowMs;
    elapsedMs_ = 0;
    finished_ = timeline == nullptr || timeline->poses.empty();
    if (timeline == nullptr) {
        interp_ = nullptr;
        cadence_ = Cadence::Original120ms;
        return;
    }

    for (const CaptureSound& pre : timeline->preSounds) {
        SoundEvent event;
        event.poseIndex = 0;  // never a real pose index; those start at 1
        event.sound = &pre;
        event.timeMs = pre.startMs;
        schedule_.push_back(event);
    }
    for (const CapturePose& pose : timeline->poses) {
        if (pose.hasSound) {
            SoundEvent event;
            event.poseIndex = pose.index;
            event.sound = &pose.sound;
            event.timeMs = pose.sound.startMs;
            schedule_.push_back(event);
        }
    }
    if (timeline->hasEndSound) {
        SoundEvent event;
        event.poseIndex = SoundEvent::kEndSoundIndex;
        event.sound = &timeline->endSound;
        event.timeMs = timeline->endSound.startMs;
        schedule_.push_back(event);
    }
    std::stable_sort(schedule_.begin(), schedule_.end(),
                     [](const SoundEvent& a, const SoundEvent& b) { return a.timeMs < b.timeMs; });

    if (interp_ != nullptr) {
        try {
            requireSameCues(*timeline, *interp_, schedule_);
        } catch (const std::exception& problem) {
            // A manifest left behind by an older extraction is not worth
            // ending the game for. The capture plays the authored poses at
            // the original cadence and the frames on disk go unused.
            std::fprintf(stderr, "swchess: %s, playing the original cadence instead\n",
                         problem.what());
            interp_ = nullptr;
        }
    }
    if (interp_ == nullptr && cadence_ == Cadence::Interpolated60) {
        cadence_ = Cadence::Original120ms;
    }
}

DrawState CapturePlayer::drawAt(std::int64_t ms) const {
    DrawState state;
    if (timeline_ == nullptr || timeline_->poses.empty()) {
        return state;
    }
    if (cadence_ == Cadence::Interpolated60 && interp_ != nullptr) {
        const InterpFrame* frame = interp_->frameAt(static_cast<double>(ms));
        if (frame == nullptr) {
            return state;
        }
        state.frame = frame;
        state.visible = frame->kind != InterpKind::Blank;
        state.x = interp_->frameRect.x;
        state.y = interp_->frameRect.y;
        state.width = interp_->frameRect.width;
        state.height = interp_->frameRect.height;
        if (!frame->source.empty()) {
            state.poseIndex = static_cast<std::size_t>(frame->source.front());
        }
        return state;
    }
    // The pose the last one replaced stays on the canvas until its successor
    // is drawn, and the last pose stays through the hold and the end cue.
    if (ms >= timeline_->endMs) {
        return state;
    }
    const std::vector<CapturePose>& poses = timeline_->poses;
    if (ms < poses.front().startMs) {
        return state;
    }
    std::size_t at = 0;
    while (at + 1 < poses.size() && poses[at + 1].startMs <= ms) {
        ++at;
    }
    const CapturePose& pose = poses[at];
    state.visible = true;
    state.record = pose.record;
    state.x = pose.x;
    state.y = pose.y;
    state.width = pose.width;
    state.height = pose.height;
    state.poseIndex = pose.index;
    return state;
}

PlayerUpdate CapturePlayer::advance(std::int64_t nowMs) {
    PlayerUpdate update;
    if (timeline_ == nullptr) {
        update.finished = true;
        return update;
    }
    if (!finished_) {
        std::int64_t elapsed = nowMs - startedMs_;
        if (elapsed > elapsedMs_) {
            elapsedMs_ = elapsed;
        }
        while (next_ < schedule_.size() && schedule_[next_].timeMs <= elapsedMs_) {
            update.sounds.push_back(schedule_[next_]);
            ++next_;
        }
        if (elapsedMs_ >= timeline_->endMs) {
            finished_ = true;
        }
    }
    update.draw = drawAt(elapsedMs_);
    update.finished = finished_;
    return update;
}

std::vector<SoundEvent> CapturePlayer::skip() {
    std::vector<SoundEvent> cancelled;
    if (timeline_ == nullptr) {
        finished_ = true;
        return cancelled;
    }
    for (std::size_t i = next_; i < schedule_.size(); ++i) {
        SoundEvent event = schedule_[i];
        event.cancelled = true;
        cancelled.push_back(event);
    }
    next_ = schedule_.size();
    elapsedMs_ = std::max(elapsedMs_, timeline_->endMs);
    finished_ = true;
    return cancelled;
}

}  // namespace swchess::anim
