// Sound playback for the native port.
//
// One Mixer opens the default playback device once and keeps it open. Every
// sound the game starts becomes a voice, and SDL mixes all live voices into
// that device. Voices belong to one of three groups, effects, speech and
// music, and each group carries its own volume.
//
// CueScheduler drives sound from the animation clock the capture player owns,
// so a cue fires at its recovered timestamp rather than on a rendered frame.
#pragma once

#include <SDL3/SDL_audio.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "audio/wav_load.h"

namespace swchess::audio {

// Which volume group a sound plays under.
enum class Channel : int {
  Effects = 0,
  Speech = 1,
  Music = 2,
};

inline constexpr int kChannelCount = 3;

const char* channelName(Channel channel);

// Names one playing sound. Zero never names a voice.
using ClipHandle = std::uint64_t;
inline constexpr ClipHandle kNoClip = 0;

// Opens the audio device and mixes the sounds the game plays.
//
// Construction never throws and never fails outright. When the real device
// refuses to open, which is what happens over SSH and on a build machine, the
// mixer reopens through SDL's dummy driver. That driver eats samples at real
// time, so playback still finishes when it should. When even the dummy driver
// refuses, the mixer keeps timing voices against the wall clock and every call
// still answers.
//
// Call play, stop and isPlaying from one thread, normally the game loop.
// SDL's audio thread only touches a looping voice's own read position.
class Mixer {
 public:
  Mixer();
  ~Mixer();

  Mixer(const Mixer&) = delete;
  Mixer& operator=(const Mixer&) = delete;

  // Start a sound and return the handle that names it.
  // Returns kNoClip when the clip holds no samples.
  //
  // A looping voice reads the clip's bytes for as long as it plays, so that
  // Clip must outlive the voice. A voice that plays once copies the bytes into
  // SDL and does not read the Clip again.
  ClipHandle play(const Clip& clip, Channel channel, float volume = 1.0f,
                  bool loop = false);

  // Stop one voice. An unknown or already finished handle does nothing.
  void stop(ClipHandle handle);

  // Stop every voice in one group.
  void stopAll(Channel channel);

  // Stop every voice in every group.
  void stopAll();

  // True while the voice still has samples left to play. A looping voice
  // answers true until something stops it.
  bool isPlaying(ClipHandle handle);

  // Set a group's volume. Values clamp to the range 0 to 1. Voices already
  // playing in that group change volume right away.
  void setVolume(Channel channel, float volume);
  float volume(Channel channel) const;

  // How many voices are still playing, across every group or in one group.
  std::size_t activeVoices();
  std::size_t activeVoices(Channel channel);

  // True when SDL handed us a working playback device.
  bool deviceOpen() const { return device_ != 0; }

  // The SDL audio driver in use, for example "coreaudio" or "dummy".
  // Empty when no driver started.
  const std::string& driverName() const { return driver_; }

  // True when the real device refused and we fell back to the dummy driver.
  bool usingDummyDriver() const { return driver_ == "dummy"; }

 private:
  // The read position a looping voice keeps. SDL's audio thread advances it.
  struct LoopSource {
    const std::byte* data = nullptr;
    int size = 0;
    int pos = 0;
  };

  struct Voice {
    ClipHandle handle = kNoClip;
    Channel channel = Channel::Effects;
    SDL_AudioStream* stream = nullptr;
    float volume = 1.0f;
    bool loop = false;
    // Wall clock deadline, used only when no device consumes the samples.
    std::uint64_t endNs = 0;
    LoopSource* source = nullptr;
  };

  static void SDLCALL refillLoop(void* userdata, SDL_AudioStream* stream,
                                 int additional, int total);

  bool openDevice(const char* driver);
  void destroyVoice(Voice& voice);
  bool voiceFinished(const Voice& voice) const;
  void reap();
  float gainFor(const Voice& voice) const;

  SDL_AudioDeviceID device_ = 0;
  bool ownsSubsystem_ = false;
  std::string driver_;
  float volumes_[kChannelCount] = {1.0f, 1.0f, 1.0f};
  std::vector<Voice> voices_;
  ClipHandle nextHandle_ = 1;
};

// One scheduled sound: play this clip on this group when the animation clock
// reaches this time.
struct Cue {
  std::int64_t timeMs = 0;
  const Clip* clip = nullptr;
  Channel channel = Channel::Effects;
  float volume = 1.0f;
  bool loop = false;
};

// Fires cues against the animation clock.
//
// The capture player advances one clock and calls advance with it. Each cue
// plays exactly once, in time order, no matter how coarse the steps are. A
// single advance across a whole second fires every cue inside that second.
class CueScheduler {
 public:
  CueScheduler() = default;
  // The mixer may be null. Cues still fire and still record, they just make
  // no sound.
  CueScheduler(Mixer* mixer, std::vector<Cue> cues);

  // Replace the cue list and rewind. Cues sort by time, and cues sharing a
  // time keep the order given.
  void setCues(std::vector<Cue> cues);
  void setMixer(Mixer* mixer) { mixer_ = mixer; }

  // Play every cue whose time has arrived and not yet played.
  void advance(std::int64_t nowMs);

  // Rewind to the start. Every cue can fire again. This stops nothing that is
  // already sounding.
  void reset();

  // Jump the clock forward without sounding anything. Cues at or before ms
  // count as already spent, so the next advance starts after them.
  void skipTo(std::int64_t ms);

  const std::vector<Cue>& cues() const { return cues_; }

  // Index of the next cue that can fire. Equals cues().size() when the
  // timeline is spent.
  std::size_t nextIndex() const { return next_; }

  // How many cues have fired since the last reset.
  std::size_t firedCount() const { return fired_.size(); }

  // Cue indices in the order they fired, oldest first.
  const std::vector<std::size_t>& firedOrder() const { return fired_; }

  // Handles the fired cues produced, in the same order.
  const std::vector<ClipHandle>& firedHandles() const { return handles_; }

  // The clock value the last advance or skipTo used.
  std::int64_t nowMs() const { return nowMs_; }

 private:
  Mixer* mixer_ = nullptr;
  std::vector<Cue> cues_;
  std::vector<std::size_t> fired_;
  std::vector<ClipHandle> handles_;
  std::size_t next_ = 0;
  std::int64_t nowMs_ = 0;
};

}  // namespace swchess::audio
