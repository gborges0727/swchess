#include "audio/wav_load.h"

#include <cstring>
#include <fstream>

namespace swchess::audio {
namespace {

constexpr std::uint16_t kFormatPcm = 0x0001;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

std::uint16_t readU16(const std::byte* p) {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(p[0]) |
                                    (std::to_integer<std::uint8_t>(p[1]) << 8));
}

std::uint32_t readU32(const std::byte* p) {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[0])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[1])) << 8) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[2])) << 16) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[3])) << 24);
}

bool tagIs(const std::byte* p, const char (&tag)[5]) {
  for (int i = 0; i < 4; ++i) {
    if (std::to_integer<char>(p[i]) != tag[i]) return false;
  }
  return true;
}

}  // namespace

std::optional<Clip> loadWav(std::span<const std::byte> bytes) {
  // The outer chunk holds the tag, a length, the WAVE tag, then subchunks.
  if (bytes.size() < 12) return std::nullopt;
  if (!tagIs(bytes.data(), "RIFF")) return std::nullopt;
  if (!tagIs(bytes.data() + 8, "WAVE")) return std::nullopt;

  // Trust the RIFF length when it is smaller than the file. Some extractors
  // leave padding after the last chunk.
  const std::uint32_t riffSize = readU32(bytes.data() + 4);
  std::size_t limit = bytes.size();
  if (static_cast<std::size_t>(riffSize) + 8 < limit) {
    limit = static_cast<std::size_t>(riffSize) + 8;
  }

  bool haveFormat = false;
  std::uint16_t channels = 0;
  std::uint16_t bits = 0;
  std::uint32_t rate = 0;
  std::span<const std::byte> data{};

  std::size_t pos = 12;
  while (pos + 8 <= limit) {
    const std::byte* header = bytes.data() + pos;
    const std::uint32_t chunkSize = readU32(header + 4);
    const std::size_t body = pos + 8;
    // A chunk that claims more bytes than remain is truncated. Take the rest.
    const std::size_t avail = limit - body;
    const std::size_t take =
        chunkSize <= avail ? static_cast<std::size_t>(chunkSize) : avail;

    if (tagIs(header, "fmt ")) {
      if (take < 16) return std::nullopt;
      const std::byte* f = bytes.data() + body;
      const std::uint16_t formatTag = readU16(f);
      if (formatTag != kFormatPcm && formatTag != kFormatExtensible) {
        return std::nullopt;
      }
      channels = readU16(f + 2);
      rate = readU32(f + 4);
      bits = readU16(f + 14);
      if (formatTag == kFormatExtensible) {
        // The real sample format sits in the extension's first GUID field.
        if (take < 40) return std::nullopt;
        if (readU16(f + 24) != kFormatPcm) return std::nullopt;
      }
      haveFormat = true;
    } else if (tagIs(header, "data")) {
      data = bytes.subspan(body, take);
    }

    // Chunks pad to an even length.
    pos = body + take + (take & 1u);
  }

  if (!haveFormat) return std::nullopt;
  if (channels != 1 && channels != 2) return std::nullopt;
  if (bits != 8 && bits != 16) return std::nullopt;
  if (rate == 0) return std::nullopt;
  if (data.empty()) return std::nullopt;

  Clip clip;
  // WAV stores 8-bit samples unsigned and 16-bit samples signed little endian.
  clip.spec.format = bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
  clip.spec.channels = channels;
  clip.spec.freq = static_cast<int>(rate);

  // Drop a trailing partial frame so the buffer holds whole frames.
  const std::size_t frameBytes =
      static_cast<std::size_t>(bits / 8) * static_cast<std::size_t>(channels);
  const std::size_t usable = data.size() - (data.size() % frameBytes);
  clip.pcm.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(usable));
  return clip;
}

std::optional<Clip> loadWav(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  // Read the whole file, then view the chars as bytes.
  std::vector<char> raw{std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>()};
  if (!file.eof() && file.fail()) return std::nullopt;
  std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(raw.data()),
                                   raw.size());
  return loadWav(bytes);
}

}  // namespace swchess::audio
