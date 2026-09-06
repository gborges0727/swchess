// Reader for the game's sound effects.
//
// SWCAUDIO.DLL holds 110 resources of type WAVE. The first RIFF starts at file
// offset 0x1400. The resource table rounds every length up to the 512-byte
// alignment, so the true size comes from the RIFF header instead. Two records
// are both named GRUNT1.WAV and hold identical bytes, which leaves 109 distinct
// names. Four more sounds sit on the CD as loose files: BLKVIC.WAV,
// STWPRES.WAV, SWTHEME.WAV and WHTVIC.WAV.
//
// Every sound on the disc is mono 8-bit PCM at 22050 Hz. This reader reports
// what the fmt chunk actually says rather than assuming that.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swchess {

// The format the fmt chunk declares plus the samples the data chunk holds.
struct WaveSound {
    std::uint16_t formatTag = 0;   // 1 for PCM
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<std::uint8_t> samples;  // the data chunk, unsigned bytes for 8-bit PCM

    // Playing time in seconds, computed from the sample count and the rate.
    double durationSeconds() const;
};

// One WAVE resource inside SWCAUDIO.DLL.
struct WaveResource {
    std::string name;            // "R2ALARM.WAV", from the resource table
    std::size_t offset = 0;      // file offset of the RIFF header
    std::size_t paddedLength = 0;  // what the resource table claims, rounded up
    std::size_t riffLength = 0;  // what the RIFF header says, the real size
    WaveSound sound;
};

// The four sounds that sit on the CD as plain files.
extern const char* const kLooseWavFiles[4];

// Parses one RIFF WAVE already in memory, starting at `at`. Throws
// std::runtime_error when the bytes are not a RIFF WAVE.
WaveSound parseWave(const std::vector<std::uint8_t>& blob, std::size_t at);

// Returns the true byte length of the RIFF at `at`, header included.
std::size_t riffLength(const std::vector<std::uint8_t>& blob, std::size_t at);

// Reads the RIFF WAV file at `path`. Throws when it cannot.
WaveSound loadWavFile(const std::string& path);

// Reads `dir`/SWCAUDIO.DLL and loads all 110 WAVE resources, in table order.
std::vector<WaveResource> loadAudioDll(const std::string& dir);

}  // namespace swchess
