// Checks the resolved capture timelines and the player against every capture
// on the CD. Run it with the CD directory as the only argument.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include <chrono>
#include <cmath>
#include <optional>

#include "anim/capture.h"
#include "anim/interp.h"
#include "anim/player.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
        ++failures;
    }
}

// Names one cue by the pose that carries it and the time it fires.
std::string cueKey(const swchess::anim::SoundEvent& event) {
    char key[64];
    std::snprintf(key, sizeof(key), "%zu@%lld", event.poseIndex,
                  static_cast<long long>(event.timeMs));
    return key;
}

// Runs the player from before the start to past the end in fixed steps and
// counts how many times each cue fires.
std::map<std::string, int> sweep(const swchess::anim::CaptureTimeline& timeline, double stepMs) {
    swchess::anim::CapturePlayer player;
    player.start(&timeline, 0);
    std::map<std::string, int> fired;
    double now = 0.0;
    std::int64_t limit = timeline.endMs + 500;
    while (true) {
        swchess::anim::PlayerUpdate update = player.advance(static_cast<std::int64_t>(now));
        for (const swchess::anim::SoundEvent& event : update.sounds) {
            fired[cueKey(event)] += 1;
        }
        if (static_cast<std::int64_t>(now) > limit) {
            break;
        }
        now += stepMs;
    }
    return fired;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: anim_test <cd directory> [assets directory]\n");
        return 2;
    }
    const std::string cdDir = argv[1];
    const std::string assetsDir = argc > 2 ? argv[2] : "assets";

    try {
        const swchess::anim::SoundCatalog& sounds = swchess::anim::sharedSoundCatalog(cdDir);
        check(sounds.size() > 0, "SWCAUDIO.DLL holds WAVE resources");

        // BBWB is the reference capture. Its INI section lists 80 keys, the
        // first is never drawn, and nothing in it plays a sound.
        swchess::anim::CaptureTimeline bbwb = swchess::anim::loadCapture(cdDir, "BBWB");
        check(bbwb.poses.size() == 79, "BBWB shows 79 poses");
        check(bbwb.frameDelayMs == 120, "CM.INI sets the frame delay to 120 ms");
        // The plain cadence puts BBWB at 79 * 120 + 1000 milliseconds. Two of
        // its cues use pause=2, so the poses after them stand still until the
        // earlier sound ends and the capture runs longer than that.
        check(bbwb.blockedMs == 392, "BBWB waits 392 ms on its two pause=2 cues");
        if (bbwb.blockedMs == 0) {
            check(bbwb.endMs == 10480, "BBWB ends at 10480 ms");
        } else {
            std::printf("BBWB blocks on sound for %lld ms, so end_ms is %lld and not 10480\n",
                        static_cast<long long>(bbwb.blockedMs),
                        static_cast<long long>(bbwb.endMs));
            check(bbwb.endMs > 10480, "BBWB ends after the plain cadence would");
        }

        // WNBR.ANX declares 80 records while its INI section lists 71 keys.
        swchess::anim::CaptureTimeline wnbr = swchess::anim::loadCapture(cdDir, "WNBR");
        check(wnbr.entryCount == 71, "WNBR uses 71 poses");
        check(wnbr.poses.size() == 70, "WNBR shows 70 of those poses");

        std::vector<std::string> names = swchess::anim::allCaptureNames();
        check(names.size() == 72, "the CD holds 72 captures");

        int withSync = 0;
        for (const std::string& name : names) {
            swchess::anim::CaptureTimeline timeline = swchess::anim::loadCapture(cdDir, name, sounds);
            check(!timeline.poses.empty(), name + " has at least one shown pose");

            std::int64_t previous = -1;
            bool ordered = true;
            bool inRange = true;
            bool hasSync = false;
            for (const swchess::anim::CapturePose& pose : timeline.poses) {
                if (pose.startMs < previous) {
                    ordered = false;
                }
                previous = pose.startMs;
                if (pose.index >= timeline.anx.timeline.size() || pose.record == nullptr) {
                    inRange = false;
                }
                if (pose.hasSound && pose.sound.mode == swchess::anim::SoundMode::Sync &&
                    pose.sound.resolved) {
                    hasSync = true;
                }
            }
            check(ordered, name + " has non-decreasing pose times");
            check(inRange, name + " indexes only records the ANX holds");

            std::int64_t plain =
                static_cast<std::int64_t>(timeline.entryCount - 1) * timeline.frameDelayMs +
                timeline.holdMs;
            if (hasSync || timeline.blockedMs > 0) {
                if (hasSync) ++withSync;
                check(timeline.endMs > plain, name + " runs past the plain formula");
                if (timeline.endMs <= plain) {
                    std::fprintf(stderr, "  %s end_ms %lld, plain %lld\n", name.c_str(),
                                 static_cast<long long>(timeline.endMs),
                                 static_cast<long long>(plain));
                }
            }
        }
        check(withSync > 0, "some captures block on a sound");
        std::printf("%d of %zu captures block on a sound\n", withSync, names.size());

        // Pose 0 is decoded without being drawn, but the original still starts
        // its sound at time 0. Only these three captures resolve a cue there;
        // every other capture's pose 0 cue is missing or unresolved, so its
        // preSounds stays empty.
        {
            const std::map<std::string, std::string> expectedPreSound = {
                {"BPWN", "GROWL2.WAV"},
                {"BQWQ", "MNDPRB.WAV"},
                {"WPBQ", "BREATH.WAV"},
            };
            for (const std::string& name : names) {
                swchess::anim::CaptureTimeline timeline =
                    swchess::anim::loadCapture(cdDir, name, sounds);
                auto expected = expectedPreSound.find(name);
                if (expected == expectedPreSound.end()) {
                    check(timeline.preSounds.empty(), name + " has no pre-sound");
                    continue;
                }
                check(timeline.preSounds.size() == 1, name + " has exactly one pre-sound");
                if (timeline.preSounds.size() == 1) {
                    const swchess::anim::CaptureSound& pre = timeline.preSounds.front();
                    check(pre.resolved, name + "'s pre-sound resolves");
                    check(pre.resource == expected->second,
                          name + "'s pre-sound names " + expected->second);
                    check(pre.startMs == 0, name + "'s pre-sound starts at time 0");
                }

                std::map<std::string, int> fired = sweep(timeline, 1000.0 / 60.0);
                check(fired["0@0"] == 1, name + " fires its pre-sound once at 16.67 ms steps");
            }
        }

        // A capture with cues of all three kinds, checked at two step sizes.
        for (const std::string& name : {std::string("BBWQ"), std::string("WBBR"),
                                        std::string("BKWR"), std::string("WQBR")}) {
            swchess::anim::CaptureTimeline timeline = swchess::anim::loadCapture(cdDir, name, sounds);
            std::size_t cues = 0;
            for (const swchess::anim::CapturePose& pose : timeline.poses) {
                if (pose.hasSound) ++cues;
            }
            if (timeline.hasEndSound) ++cues;

            std::map<std::string, int> coarse = sweep(timeline, 1000.0 / 60.0);
            std::map<std::string, int> fine = sweep(timeline, 1.0);
            check(coarse.size() == cues, name + " fires every cue at 16.67 ms steps");
            check(fine.size() == cues, name + " fires every cue at 1 ms steps");
            for (const auto& [key, count] : coarse) {
                check(count == 1, name + " fires " + key + " once at 16.67 ms steps");
            }
            for (const auto& [key, count] : fine) {
                check(count == 1, name + " fires " + key + " once at 1 ms steps");
            }
            check(coarse == fine, name + " fires the same cues at both step sizes");
        }

        // The 60 frames per second sequence for BBWB, when the interpolation
        // run has written it. Other captures are still being generated, so a
        // missing manifest is not a failure.
        {
            const auto before = std::chrono::steady_clock::now();
            std::optional<swchess::anim::InterpSequence> interp =
                swchess::anim::loadInterp(assetsDir, "BBWB");
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
            if (!interp.has_value()) {
                std::printf("no interp60 manifest for BBWB under %s, skipping those checks\n",
                            assetsDir.c_str());
            } else {
                std::printf("loaded %zu BBWB interpolated frames in %.3f seconds\n",
                            interp->frames.size(), seconds);
                check(seconds < 2.0, "BBWB's interpolated frames load in under 2 seconds");
                check(interp->frames.size() == 653, "BBWB has 653 interpolated frames");
                check(interp->fps == 60, "the manifest says 60 frames per second");

                double total = 0.0;
                for (const swchess::anim::InterpFrame& frame : interp->frames) {
                    total += frame.durationMs;
                }
                check(std::fabs(total - static_cast<double>(interp->endMs)) < 1e-6,
                      "the frame durations add up to end_ms");
                check(interp->endMs == bbwb.endMs, "both cadences end at the same time");

                swchess::anim::CapturePlayer plain;
                plain.start(&bbwb, &interp.value(), 0);
                swchess::anim::CapturePlayer fast;
                fast.start(&bbwb, &interp.value(), 0);
                fast.setCadence(swchess::anim::Cadence::Interpolated60);
                check(fast.cadence() == swchess::anim::Cadence::Interpolated60,
                      "the player accepts the interpolated cadence once it has pictures");

                // start already refuses a mismatch, so this repeats the
                // comparison in the test's own terms.
                std::size_t cued = 0;
                bool sameCues = true;
                for (const swchess::anim::SoundEvent& event : plain.schedule()) {
                    if (event.poseIndex == swchess::anim::SoundEvent::kEndSoundIndex) {
                        sameCues = sameCues && interp->hasFinalSound &&
                                   interp->finalSound.name == event.sound->resource;
                        continue;
                    }
                    if (cued >= interp->sounds.size()) {
                        sameCues = false;
                        break;
                    }
                    const swchess::anim::InterpSound& mine = interp->sounds[cued];
                    ++cued;
                    sameCues = sameCues && mine.name == event.sound->resource &&
                               mine.startMs == event.timeMs && mine.poseIndex == event.poseIndex;
                }
                check(sameCues && cued == interp->sounds.size(),
                      "both cadences name the same cues at the same times");
                check(cued == 10, "BBWB carries ten pose cues");

                // Sweep both players together and watch what they return.
                const double step = 1000.0 / 60.0;
                const std::int64_t limit = bbwb.endMs + 500;
                std::map<std::string, int> plainFired;
                std::map<std::string, int> fastFired;
                bool framesOnTime = true;
                bool sawFrame = false;
                double now = 0.0;
                while (true) {
                    const std::int64_t ms = static_cast<std::int64_t>(now);
                    for (const swchess::anim::SoundEvent& event : plain.advance(ms).sounds) {
                        plainFired[cueKey(event)] += 1;
                    }
                    swchess::anim::PlayerUpdate update = fast.advance(ms);
                    for (const swchess::anim::SoundEvent& event : update.sounds) {
                        fastFired[cueKey(event)] += 1;
                    }
                    if (update.draw.frame != nullptr) {
                        sawFrame = true;
                        const double drift = static_cast<double>(ms) - update.draw.frame->tMs;
                        if (drift < 0.0 || drift > step) {
                            framesOnTime = false;
                        }
                    } else {
                        check(!update.draw.visible, "a player with no frame draws nothing");
                    }
                    if (ms > limit) {
                        break;
                    }
                    now += step;
                }
                check(sawFrame, "the interpolated player returns frames");
                check(framesOnTime,
                      "every interpolated frame sits within one frame time of the sweep");
                check(plainFired == fastFired, "both cadences fire the same cues");
                for (const auto& [key, count] : fastFired) {
                    check(count == 1, "the interpolated cadence fires " + key + " once");
                }

                // Toggling mid capture keeps the clock and replays nothing.
                swchess::anim::CapturePlayer toggling;
                toggling.start(&bbwb, &interp.value(), 0);
                std::map<std::string, int> toggled;
                now = 0.0;
                bool switched = false;
                while (true) {
                    const std::int64_t ms = static_cast<std::int64_t>(now);
                    for (const swchess::anim::SoundEvent& event : toggling.advance(ms).sounds) {
                        toggled[cueKey(event)] += 1;
                    }
                    if (!switched && ms >= bbwb.endMs / 2) {
                        const std::int64_t held = toggling.elapsedMs();
                        const std::size_t fired = toggling.firedCount();
                        toggling.setCadence(swchess::anim::Cadence::Interpolated60);
                        check(toggling.elapsedMs() == held, "the toggle keeps the animation time");
                        check(toggling.firedCount() == fired, "the toggle rewinds no cue");
                        switched = true;
                    }
                    if (ms > limit) {
                        break;
                    }
                    now += step;
                }
                check(switched, "the sweep reached the toggle");
                check(toggled == plainFired, "toggling mid capture fires the same cues");
                for (const auto& [key, count] : toggled) {
                    check(count == 1, "a toggled capture fires " + key + " once");
                }

                // Skipping behaves the same under either cadence.
                swchess::anim::CapturePlayer skipper;
                skipper.start(&bbwb, &interp.value(), 0);
                skipper.setCadence(swchess::anim::Cadence::Interpolated60);
                skipper.advance(2000);
                const std::size_t firedBeforeSkip = skipper.firedCount();
                std::vector<swchess::anim::SoundEvent> dropped = skipper.skip();
                check(skipper.isFinished(), "skip finishes the interpolated capture");
                check(firedBeforeSkip + dropped.size() == skipper.schedule().size(),
                      "skip cancels every interpolated cue that had not fired");
                check(skipper.elapsedMs() == bbwb.endMs, "skip moves the clock to the end");
                swchess::anim::PlayerUpdate after = skipper.advance(bbwb.endMs + 1000);
                check(!after.draw.visible && after.draw.frame == nullptr,
                      "a skipped interpolated capture draws nothing");
            }
        }

        // BPWN carries a pre-sound. When the interpolation run has already
        // written its 60 frames per second sequence, start() compares the
        // preSounds the same way it compares pose cues; a mismatch throws.
        {
            swchess::anim::CaptureTimeline bpwn = swchess::anim::loadCapture(cdDir, "BPWN", sounds);
            std::optional<swchess::anim::InterpSequence> bpwnInterp =
                swchess::anim::loadInterp(assetsDir, "BPWN");
            if (!bpwnInterp.has_value()) {
                std::printf("no interp60 manifest for BPWN under %s, skipping that check\n",
                            assetsDir.c_str());
            } else {
                check(bpwnInterp->preSounds.size() == 1, "BPWN's manifest carries one pre-sound");
                swchess::anim::CapturePlayer bpwnPlayer;
                bpwnPlayer.start(&bpwn, &bpwnInterp.value(), 0);
                check(bpwnPlayer.interp() == &bpwnInterp.value(),
                      "BPWN's timeline and interp60 sequence agree, so start accepts both");
            }
        }

        // Skipping ends the capture and reports what never played.
        swchess::anim::CaptureTimeline bbwq = swchess::anim::loadCapture(cdDir, "BBWQ", sounds);
        swchess::anim::CapturePlayer player;
        player.start(&bbwq, 0);
        player.advance(0);
        std::size_t before = player.firedCount();
        std::vector<swchess::anim::SoundEvent> cancelled = player.skip();
        check(player.isFinished(), "skip finishes the capture");
        check(before + cancelled.size() == player.schedule().size(),
              "skip cancels every cue that had not fired");
        for (const swchess::anim::SoundEvent& event : cancelled) {
            check(event.cancelled, "a skipped cue is marked cancelled");
        }
        check(!player.advance(bbwq.endMs + 5000).draw.visible, "a finished capture draws nothing");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL threw: %s\n", error.what());
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::printf("anim_test passed\n");
    return 0;
}
