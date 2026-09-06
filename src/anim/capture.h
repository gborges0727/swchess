// Resolves one capture animation into a timeline the player can run.
//
// The original reads three files for every capture. NAME.ANX holds the
// bitmaps and their placement, XX.INI holds the pose order and the sound
// cues, and CM.INI holds the frame delay. loadCapture reads all three and
// produces the poses with the times, positions and sounds already worked out.
//
// docs/research/capture-player.md describes the code this follows. Section 2
// lists the INI keys, section 3 describes the position table, section 5
// describes the timing, and section 6 describes the sound lookup.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "assets/anx.h"

namespace swchess::anim {

// The backdrop the original composites a capture over. Section 7 of the
// research note reports a 640 by 480 canvas, and the blit clips to it.
inline constexpr int kCanvasWidth = 640;
inline constexpr int kCanvasHeight = 480;

// The rectangle the player draws into, in canvas pixels.
struct CanvasRect {
    int x = 0;
    int y = 0;
    int width = kCanvasWidth;
    int height = kCanvasHeight;
};

inline constexpr CanvasRect kCaptureCanvas{0, 0, kCanvasWidth, kCanvasHeight};

// What the INI `pause` key does to a cue.
//
// Async is pause 0 or no pause at all. The sound starts and nothing waits.
// Sync is pause 1. The original calls sndPlaySound with SND_SYNC, so the
// animation stops until the sound ends.
// WaitPrevious is pause 2. The sound starts without blocking, and the next
// pose that carries a cue waits for this one to finish first.
enum class SoundMode {
    Async,
    Sync,
    WaitPrevious,
};

const char* soundModeName(SoundMode mode);

// One sound cue attached to a pose or to the end of the capture.
struct CaptureSound {
    std::string cue;       // the name the INI spells, for example "r2alarm\.wav"
    std::string resource;  // the WAVE resource name, empty when nothing matched
    SoundMode mode = SoundMode::Async;
    std::int64_t durationMs = 0;  // 0 when the cue resolved to nothing
    std::int64_t startMs = 0;     // when the original calls sndPlaySound
    bool resolved = false;        // true when a WAVE resource carries this name
};

// One pose the player puts on the canvas.
struct CapturePose {
    std::size_t index = 0;      // position in the INI key list, always 1 or more
    std::string key;            // the INI key, for example "BBWB_040"
    std::int64_t startMs = 0;   // when this pose replaces the one before it
    std::uint32_t recordOffset = 0;  // names the record inside AnxFile::records
    const AnxRecord* record = nullptr;
    int x = 0;  // top left corner on the canvas, table 2 plus the section offset
    int y = 0;
    int width = 0;
    int height = 0;
    bool hasSound = false;
    CaptureSound sound;
};

// Everything one capture needs to play.
struct CaptureTimeline {
    std::string name;     // "BBWB"
    std::string iniPath;  // the file the pose list came from
    AnxFile anx;          // owns every decoded record

    std::vector<CapturePose> poses;  // the poses that reach the screen
    std::size_t entryCount = 0;      // INI keys, one more than poses.size()

    std::int64_t frameDelayMs = 120;
    std::int64_t holdMs = 1000;
    int offsetX = 0;
    int offsetY = 0;

    bool hasEndSound = false;
    CaptureSound endSound;  // [NAME_OFFSET] wav, played after the hold

    // Milliseconds the blocking sounds added. A sync cue adds whatever it
    // runs past the frame delay, and a wait_previous cue adds however long the
    // next sounding pose stood still for it. Zero means the capture ran at the
    // plain cadence.
    std::int64_t blockedMs = 0;

    std::int64_t holdEndMs = 0;  // when the hold finishes and the end cue starts
    std::int64_t endMs = 0;      // when the last pose is erased
};

// Sound durations, read once from SWCAUDIO.DLL.
//
// The original uppercases the cue name and calls FindResource with it, so only
// an exact match after uppercasing plays. Everything else is silent.
class SoundCatalog {
public:
    SoundCatalog() = default;
    // Reads `cdDir`/SWCAUDIO.DLL. Throws std::runtime_error when it cannot.
    explicit SoundCatalog(const std::string& cdDir);

    // Fills `resource` and `durationMs` when the uppercased cue names a
    // resource. Returns false and leaves both alone otherwise.
    bool find(const std::string& cue, std::string* resource, std::int64_t* durationMs) const;

    std::size_t size() const { return durations_.size(); }

private:
    std::map<std::string, std::int64_t> durations_;  // resource name to milliseconds
};

// Reads the catalog for `cdDir` once and hands out the same one afterwards.
const SoundCatalog& sharedSoundCatalog(const std::string& cdDir);

// Reads NAME.ANX, the pose list in the attacker's INI, and CM.INI, then
// resolves every pose time, position and cue. `name` is the four character
// capture code such as "BBWB". Throws std::runtime_error when a file is
// missing or the pose list is longer than the ANX.
CaptureTimeline loadCapture(const std::string& cdDir, const std::string& name);

// The same, reusing a catalog the caller already built.
CaptureTimeline loadCapture(const std::string& cdDir, const std::string& name,
                            const SoundCatalog& sounds);

// The 72 capture codes, attacker colour and piece then defender colour and
// piece, in the order the demo enumerates them.
std::vector<std::string> allCaptureNames();

}  // namespace swchess::anim
