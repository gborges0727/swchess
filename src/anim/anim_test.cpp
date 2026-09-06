// Checks the resolved capture timelines and the player against every capture
// on the CD. Run it with the CD directory as the only argument.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include "anim/capture.h"
#include "anim/player.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
        ++failures;
    }
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
            char key[64];
            std::snprintf(key, sizeof(key), "%zu@%lld", event.poseIndex,
                          static_cast<long long>(event.timeMs));
            fired[key] += 1;
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
        std::fprintf(stderr, "usage: anim_test <cd directory>\n");
        return 2;
    }
    const std::string cdDir = argv[1];

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
