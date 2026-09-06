#include "anim/player.h"

#include <algorithm>
#include <stdexcept>

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
    if (cadence == Cadence::Interpolated60) {
        throw std::runtime_error("the interpolated cadence has no images yet");
    }
    cadence_ = cadence;
}

void CapturePlayer::start(const CaptureTimeline* timeline, std::int64_t nowMs) {
    timeline_ = timeline;
    schedule_.clear();
    next_ = 0;
    pose_ = 0;
    startedMs_ = nowMs;
    elapsedMs_ = 0;
    finished_ = timeline == nullptr || timeline->poses.empty();
    if (timeline == nullptr) {
        return;
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
}

DrawState CapturePlayer::drawAt(std::int64_t ms) const {
    DrawState state;
    if (timeline_ == nullptr || timeline_->poses.empty()) {
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
