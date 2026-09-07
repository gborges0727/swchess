#include "audio/audio.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdlib>

namespace swchess::audio {
namespace {

int channelIndex(Channel channel) {
  const int i = static_cast<int>(channel);
  return (i >= 0 && i < kChannelCount) ? i : 0;
}

float clamp01(float v) {
  if (!(v > 0.0f)) return 0.0f;  // Also catches NaN.
  return v < 1.0f ? v : 1.0f;
}

}  // namespace

const char* channelName(Channel channel) {
  switch (channel) {
    case Channel::Effects: return "effects";
    case Channel::Speech: return "speech";
    case Channel::Music: return "music";
  }
  return "effects";
}

// --- Mixer ---------------------------------------------------------------

Mixer::Mixer() {
  // Try whatever driver the platform picks first. Over SSH and on a build
  // machine that fails, so fall back to the dummy driver, which runs the same
  // code path and discards the samples.
  if (!openDevice(nullptr)) {
    if (!openDevice("dummy")) {
      // Nothing plays. Voices still track their own length so the rest of the
      // game sees the timing it expects.
      driver_.clear();
    }
  }
}

bool Mixer::openDevice(const char* driver) {
  if (SDL_WasInit(SDL_INIT_AUDIO) != 0) {
    // A previous attempt left the subsystem up. Take it down so a new driver
    // hint can take effect.
    if (ownsSubsystem_) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      ownsSubsystem_ = false;
    }
  }
  if (driver != nullptr) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    return false;
  }
  ownsSubsystem_ = true;

  const SDL_AudioDeviceID id =
      SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  if (id == 0) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ownsSubsystem_ = false;
    return false;
  }
  device_ = id;
  const char* name = SDL_GetCurrentAudioDriver();
  driver_ = name != nullptr ? name : "";
  return true;
}

Mixer::~Mixer() {
  stopAll();
  if (device_ != 0) {
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }
  if (ownsSubsystem_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ownsSubsystem_ = false;
  }
}

void SDLCALL Mixer::refillLoop(void* userdata, SDL_AudioStream* stream,
                               int additional, int /*total*/) {
  // SDL's audio thread calls this. It only touches this voice's own read
  // position, so it needs no lock against the game thread.
  auto* src = static_cast<LoopSource*>(userdata);
  if (src == nullptr || src->data == nullptr || src->size <= 0) return;
  int wanted = additional;
  while (wanted > 0) {
    const int remaining = src->size - src->pos;
    const int chunk = remaining < wanted ? remaining : wanted;
    if (chunk <= 0) {
      src->pos = 0;
      continue;
    }
    SDL_PutAudioStreamData(stream, src->data + src->pos, chunk);
    src->pos += chunk;
    if (src->pos >= src->size) src->pos = 0;
    wanted -= chunk;
  }
}

float Mixer::gainFor(const Voice& voice) const {
  return volumes_[channelIndex(voice.channel)] * voice.volume;
}

ClipHandle Mixer::play(const Clip& clip, Channel channel, float volume,
                       bool loop) {
  reap();
  if (clip.empty()) return kNoClip;
  if (clip.pcm.size() > static_cast<std::size_t>(INT32_MAX)) return kNoClip;

  SDL_AudioStream* stream = SDL_CreateAudioStream(&clip.spec, &clip.spec);
  if (stream == nullptr) return kNoClip;

  Voice voice;
  voice.handle = nextHandle_++;
  voice.channel = channel;
  voice.stream = stream;
  voice.volume = clamp01(volume);
  voice.loop = loop;

  SDL_SetAudioStreamGain(stream, gainFor(voice));

  const int size = static_cast<int>(clip.pcm.size());
  if (loop) {
    // Hand SDL a first pass, then let the callback wrap around forever.
    voice.source = new LoopSource{clip.pcm.data(), size, 0};
    SDL_PutAudioStreamData(stream, clip.pcm.data(), size);
    SDL_SetAudioStreamGetCallback(stream, &Mixer::refillLoop, voice.source);
  } else {
    // SDL copies these bytes, so the clip may go away right after this.
    if (!SDL_PutAudioStreamData(stream, clip.pcm.data(), size)) {
      SDL_DestroyAudioStream(stream);
      return kNoClip;
    }
    SDL_FlushAudioStream(stream);
  }

  if (device_ != 0) {
    if (!SDL_BindAudioStream(device_, stream)) {
      destroyVoice(voice);
      return kNoClip;
    }
    SDL_ResumeAudioStreamDevice(stream);
  } else {
    // No device consumes the samples, so time the voice off the wall clock.
    voice.endNs = SDL_GetTicksNS() +
                  static_cast<std::uint64_t>(clip.durationMs()) * 1000000ull;
  }

  voices_.push_back(voice);
  return voice.handle;
}

void Mixer::destroyVoice(Voice& voice) {
  if (voice.stream != nullptr) {
    // Destroying the stream unbinds it and waits out any running callback,
    // so the loop source is safe to free afterwards.
    SDL_DestroyAudioStream(voice.stream);
    voice.stream = nullptr;
  }
  delete voice.source;
  voice.source = nullptr;
}

bool Mixer::voiceFinished(const Voice& voice) const {
  if (voice.loop) return false;
  if (voice.stream == nullptr) return true;
  if (device_ == 0) return SDL_GetTicksNS() >= voice.endNs;
  return SDL_GetAudioStreamAvailable(voice.stream) <= 0;
}

void Mixer::reap() {
  for (std::size_t i = voices_.size(); i-- > 0;) {
    if (voiceFinished(voices_[i])) {
      destroyVoice(voices_[i]);
      voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
}

void Mixer::stop(ClipHandle handle) {
  if (handle == kNoClip) return;
  for (std::size_t i = 0; i < voices_.size(); ++i) {
    if (voices_[i].handle != handle) continue;
    destroyVoice(voices_[i]);
    voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i));
    return;
  }
}

void Mixer::stopAll(Channel channel) {
  for (std::size_t i = voices_.size(); i-- > 0;) {
    if (voices_[i].channel != channel) continue;
    destroyVoice(voices_[i]);
    voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

void Mixer::stopAll() {
  for (auto& voice : voices_) destroyVoice(voice);
  voices_.clear();
}

bool Mixer::isPlaying(ClipHandle handle) {
  if (handle == kNoClip) return false;
  reap();
  for (const auto& voice : voices_) {
    if (voice.handle == handle) return true;
  }
  return false;
}

void Mixer::setVolume(Channel channel, float volume) {
  volumes_[channelIndex(channel)] = clamp01(volume);
  for (auto& voice : voices_) {
    if (voice.channel != channel || voice.stream == nullptr) continue;
    SDL_SetAudioStreamGain(voice.stream, gainFor(voice));
  }
}

float Mixer::volume(Channel channel) const {
  return volumes_[channelIndex(channel)];
}

std::size_t Mixer::activeVoices() {
  reap();
  return voices_.size();
}

std::size_t Mixer::activeVoices(Channel channel) {
  reap();
  std::size_t count = 0;
  for (const auto& voice : voices_) {
    if (voice.channel == channel) ++count;
  }
  return count;
}

// --- CueScheduler --------------------------------------------------------

CueScheduler::CueScheduler(Mixer* mixer, std::vector<Cue> cues)
    : mixer_(mixer) {
  setCues(std::move(cues));
}

void CueScheduler::setCues(std::vector<Cue> cues) {
  cues_ = std::move(cues);
  std::stable_sort(cues_.begin(), cues_.end(),
                   [](const Cue& a, const Cue& b) { return a.timeMs < b.timeMs; });
  reset();
}

void CueScheduler::reset() {
  next_ = 0;
  nowMs_ = 0;
  fired_.clear();
  handles_.clear();
  live_ = kNoClip;
}

void CueScheduler::advance(std::int64_t nowMs) {
  nowMs_ = nowMs;
  while (next_ < cues_.size() && cues_[next_].timeMs <= nowMs) {
    const Cue& cue = cues_[next_];
    ClipHandle handle = kNoClip;
    if (mixer_ != nullptr && cue.clip != nullptr) {
      // sndPlaySound stops the sound that is playing before it starts the new
      // one, so the previous cue goes quiet here rather than mixing on.
      if (replacePrevious_ && live_ != kNoClip) {
        mixer_->stop(live_);
        live_ = kNoClip;
      }
      handle = mixer_->play(*cue.clip, cue.channel, cue.volume, cue.loop);
      live_ = handle;
    }
    fired_.push_back(next_);
    handles_.push_back(handle);
    ++next_;
  }
}

void CueScheduler::skipTo(std::int64_t ms) {
  nowMs_ = ms;
  // Spend every cue at or before the target without sounding it.
  while (next_ < cues_.size() && cues_[next_].timeMs <= ms) {
    ++next_;
  }
}

}  // namespace swchess::audio
