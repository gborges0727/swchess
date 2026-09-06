// Checks the WAV reader, the cue scheduler and the mixer.
// Everything here runs without a real sound card. The mixer falls back to
// SDL's dummy driver, so this passes over SSH and on a build machine.
#include "audio/audio.h"
#include "audio/wav_load.h"

#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace swchess::audio;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (ok) {
    std::printf("  ok    %s\n", what.c_str());
  } else {
    std::printf("  FAIL  %s\n", what.c_str());
    ++failures;
  }
}

std::vector<std::byte> readFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<char> raw{std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>()};
  std::vector<std::byte> bytes(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
  }
  return bytes;
}

std::uint32_t readU32(const std::byte* p) {
  return static_cast<std::uint32_t>(std::to_integer<unsigned char>(p[0])) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned char>(p[1])) << 8) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned char>(p[2])) << 16) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned char>(p[3])) << 24);
}

// Walk the RIFF chunks by hand and report what the data chunk claims. The
// reader under test must agree with this number.
std::uint32_t dataChunkSize(const std::vector<std::byte>& bytes) {
  std::size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const std::byte* h = bytes.data() + pos;
    const bool isData = std::to_integer<char>(h[0]) == 'd' &&
                        std::to_integer<char>(h[1]) == 'a' &&
                        std::to_integer<char>(h[2]) == 't' &&
                        std::to_integer<char>(h[3]) == 'a';
    const std::uint32_t size = readU32(h + 4);
    if (isData) return size;
    pos += 8 + size + (size & 1u);
  }
  return 0;
}

void testWavFiles(const fs::path& root) {
  std::printf("wav reader\n");
  const fs::path names[3] = {
      root / "assets" / "audio" / "ARC1.WAV",
      root / "assets" / "audio" / "ATAT.WAV",
      root / "assets" / "audio" / "R2ALARM.WAV",
  };
  for (const fs::path& path : names) {
    const std::string label = path.filename().string();
    if (!fs::exists(path)) {
      check(false, label + " is missing, run python3 -m tools.extract");
      continue;
    }
    const std::vector<std::byte> bytes = readFile(path);
    const std::uint32_t claimed = dataChunkSize(bytes);
    auto clip = loadWav(path);
    if (!clip.has_value()) {
      check(false, label + " failed to parse");
      continue;
    }
    check(clip->pcm.size() == claimed,
          label + " holds " + std::to_string(clip->pcm.size()) +
              " bytes and the data chunk claims " + std::to_string(claimed));
    // Every extracted effect is unsigned 8-bit mono at 11025 Hz. The CD's
    // standalone SWTHEME.WAV is the same format at 22050 Hz.
    check(clip->spec.format == SDL_AUDIO_U8 && clip->spec.channels == 1 &&
              clip->spec.freq == 11025,
          label + " is 8-bit mono at " + std::to_string(clip->spec.freq) + " Hz");
    check(clip->sampleCount() == claimed,
          label + " counts " + std::to_string(clip->sampleCount()) +
              " samples, one byte each");
    check(clip->durationMs() > 0,
          label + " lasts " + std::to_string(clip->durationMs()) + " ms");
  }

  // The theme lives beside the original CD files rather than in assets.
  const fs::path theme = root / "original" / "win3x" / "cd" / "SWTHEME.WAV";
  if (fs::exists(theme)) {
    const std::uint32_t claimed = dataChunkSize(readFile(theme));
    auto clip = loadWav(theme);
    check(clip.has_value() && clip->pcm.size() == claimed,
          "SWTHEME.WAV holds the " + std::to_string(claimed) +
              " bytes its data chunk claims");
    check(clip.has_value() && clip->spec.format == SDL_AUDIO_U8 &&
              clip->spec.channels == 1 && clip->spec.freq == 22050,
          "SWTHEME.WAV is 8-bit mono at 22050 Hz");
  } else {
    std::printf("  note  SWTHEME.WAV is absent, skipping it\n");
  }

  // Garbage must be rejected rather than parsed into noise.
  const std::vector<std::byte> junk(64, std::byte{0x41});
  check(!loadWav(std::span<const std::byte>(junk)).has_value(),
        "a file that is not RIFF is rejected");
  check(!loadWav(root / "assets" / "audio" / "NOSUCHFILE.WAV").has_value(),
        "a missing path is rejected");
}

// Run the scheduler over a two second timeline and confirm each cue fires
// once, in time order, whatever the step size is.
void testScheduler(const Clip& clip) {
  std::printf("cue scheduler\n");
  std::vector<Cue> cues = {
      {0, &clip, Channel::Effects, 1.0f, false},
      {1500, &clip, Channel::Speech, 1.0f, false},
      {250, &clip, Channel::Effects, 1.0f, false},
      {1000, &clip, Channel::Music, 1.0f, false},
      {2000, &clip, Channel::Effects, 1.0f, false},
      {1000, &clip, Channel::Speech, 1.0f, false},
  };

  CueScheduler scheduler(nullptr, cues);
  check(scheduler.cues().size() == 6, "the scheduler holds all six cues");
  check(scheduler.cues()[0].timeMs == 0 && scheduler.cues()[1].timeMs == 250 &&
            scheduler.cues()[2].timeMs == 1000 &&
            scheduler.cues()[3].timeMs == 1000 &&
            scheduler.cues()[4].timeMs == 1500 &&
            scheduler.cues()[5].timeMs == 2000,
        "cues sort by time and keep their order within one time");
  // Cues at 1000 ms keep the order they were given, music before speech.
  check(scheduler.cues()[2].channel == Channel::Music &&
            scheduler.cues()[3].channel == Channel::Speech,
        "two cues at 1000 ms fire in the order given");

  // Step the clock in 16 ms slices, the way a 60 Hz loop would.
  for (std::int64_t t = 0; t <= 2000; t += 16) scheduler.advance(t);
  scheduler.advance(2000);
  check(scheduler.firedCount() == 6, "all six cues fired");
  bool inOrder = true;
  for (std::size_t i = 0; i < scheduler.firedOrder().size(); ++i) {
    if (scheduler.firedOrder()[i] != i) inOrder = false;
  }
  check(inOrder, "the cues fired in time order with no repeats");

  // A single long step must still fire everything exactly once.
  scheduler.reset();
  check(scheduler.firedCount() == 0 && scheduler.nextIndex() == 0,
        "reset rewinds the timeline");
  scheduler.advance(5000);
  check(scheduler.firedCount() == 6, "one long step fires every cue once");
  scheduler.advance(9000);
  check(scheduler.firedCount() == 6, "advancing past the end fires nothing new");

  // Skipping the first second must swallow the cues inside it.
  scheduler.reset();
  scheduler.skipTo(1000);
  check(scheduler.firedCount() == 0, "skipTo sounds nothing on the way");
  check(scheduler.nextIndex() == 4,
        "skipTo(1000) spends the four cues at or before 1000 ms");
  scheduler.advance(2000);
  check(scheduler.firedCount() == 2, "only the cues after the skip fire");
  check(scheduler.firedOrder()[0] == 4 && scheduler.firedOrder()[1] == 5,
        "the cues after the skip are the 1500 ms and 2000 ms ones");
}

void testMixer(const Clip& shortClip, const Clip& otherClip) {
  std::printf("mixer\n");
  Mixer mixer;
  std::printf("  note  driver=%s device=%s\n",
              mixer.driverName().empty() ? "(none)" : mixer.driverName().c_str(),
              mixer.deviceOpen() ? "open" : "none");
  check(true, "the mixer constructed without a real device requirement");

  mixer.setVolume(Channel::Music, 0.4f);
  check(mixer.volume(Channel::Music) == 0.4f, "music volume reads back as 0.4");
  mixer.setVolume(Channel::Speech, 5.0f);
  check(mixer.volume(Channel::Speech) == 1.0f, "an out of range volume clamps to 1");
  mixer.setVolume(Channel::Speech, 1.0f);

  const ClipHandle handle = mixer.play(shortClip, Channel::Effects, 1.0f, false);
  check(handle != kNoClip, "play returned a handle");
  check(mixer.isPlaying(handle), "the clip reports playing right after it starts");

  // Two more voices at once, on other groups.
  const ClipHandle speech = mixer.play(otherClip, Channel::Speech);
  const ClipHandle music = mixer.play(otherClip, Channel::Music, 1.0f, true);
  check(mixer.activeVoices() == 3, "three voices play at the same time");
  check(mixer.activeVoices(Channel::Speech) == 1, "one voice plays on speech");

  mixer.stop(speech);
  check(!mixer.isPlaying(speech), "stop ends that one voice");
  check(mixer.isPlaying(music), "the looping music voice keeps playing");

  // Wait for the one-shot to run out. The dummy driver eats samples at real
  // time, so this finishes in about as long as the clip lasts.
  const std::uint64_t limitMs =
      static_cast<std::uint64_t>(shortClip.durationMs()) + 4000;
  const std::uint64_t start = SDL_GetTicks();
  while (mixer.isPlaying(handle) && SDL_GetTicks() - start < limitMs) {
    SDL_Delay(20);
  }
  const std::uint64_t elapsed = SDL_GetTicks() - start;
  check(!mixer.isPlaying(handle),
        "isPlaying turned false after " + std::to_string(elapsed) +
            " ms for a " + std::to_string(shortClip.durationMs()) + " ms clip");
  check(mixer.isPlaying(music), "the loop still plays after the one-shot ended");

  mixer.stopAll(Channel::Music);
  check(!mixer.isPlaying(music), "stopAll ends the looping voice");
  check(mixer.activeVoices() == 0, "no voices remain");

  // A handle that never named a voice, and one that already ended.
  check(!mixer.isPlaying(kNoClip), "the null handle never plays");
  check(!mixer.isPlaying(handle + 9999), "an unknown handle never plays");
  mixer.stop(handle);  // Must not crash.
  check(true, "stopping a finished voice is harmless");

  Clip empty;
  check(mixer.play(empty, Channel::Effects) == kNoClip,
        "an empty clip yields no handle");
}

}  // namespace

int main(int argc, char** argv) {
  fs::path root = argc > 1 ? fs::path(argv[1]) : fs::path(SWCHESS_REPO_ROOT);
  std::printf("repo root %s\n", root.string().c_str());

  testWavFiles(root);

  auto shortClip = loadWav(root / "assets" / "audio" / "R2ALARM.WAV");
  auto otherClip = loadWav(root / "assets" / "audio" / "ARC1.WAV");
  if (!shortClip.has_value() || !otherClip.has_value()) {
    std::printf("  FAIL  cannot load the clips the mixer tests need\n");
    return 1;
  }

  testScheduler(*otherClip);
  testMixer(*shortClip, *otherClip);

  std::printf("%s\n", failures == 0 ? "all checks passed"
                                    : (std::to_string(failures) + " checks failed").c_str());
  return failures == 0 ? 0 : 1;
}
