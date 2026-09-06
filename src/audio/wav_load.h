// Minimal RIFF WAVE reader for the original game's sound files.
// It handles uncompressed PCM only, 8 or 16 bits per sample, mono or stereo.
// The module owns this reader so it does not wait on the asset decoders.
#pragma once

#include <SDL3/SDL_audio.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace swchess::audio {

// One decoded sound. The bytes sit in the format the WAV file declared, so
// SDL converts them when the mixer feeds them to the device.
struct Clip {
  SDL_AudioSpec spec{SDL_AUDIO_U8, 1, 22050};
  std::vector<std::byte> pcm;

  // Bytes one sample of one channel occupies.
  int bytesPerSample() const { return SDL_AUDIO_BYTESIZE(spec.format); }

  // Bytes one sample across every channel occupies.
  int bytesPerFrame() const { return bytesPerSample() * spec.channels; }

  // Samples counted across every channel. A stereo file counts both sides.
  std::size_t sampleCount() const {
    const int bytes = bytesPerSample();
    return bytes > 0 ? pcm.size() / static_cast<std::size_t>(bytes) : 0;
  }

  // Sample positions in time. Stereo pairs count once.
  std::size_t frameCount() const {
    const int bytes = bytesPerFrame();
    return bytes > 0 ? pcm.size() / static_cast<std::size_t>(bytes) : 0;
  }

  // How long the clip plays, rounded up to the next whole millisecond.
  std::int64_t durationMs() const {
    if (spec.freq <= 0) return 0;
    const std::int64_t frames = static_cast<std::int64_t>(frameCount());
    return (frames * 1000 + spec.freq - 1) / spec.freq;
  }

  bool empty() const { return pcm.empty(); }
};

// Parse a whole RIFF WAVE file already in memory.
// Returns nothing when the header is not RIFF/WAVE, when the format is not
// PCM, or when the sample width or channel count is outside what we read.
std::optional<Clip> loadWav(std::span<const std::byte> bytes);

// Read the file and parse it. Returns nothing when the read fails.
std::optional<Clip> loadWav(const std::filesystem::path& path);

}  // namespace swchess::audio
