#include "assets/wav.h"

#include <cstring>
#include <stdexcept>

#include "assets/cdfs.h"
#include "assets/ne.h"

namespace swchess {
namespace {

std::uint16_t readU16(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 2 > blob.size()) {
        throw std::runtime_error("WAV read past end of file");
    }
    return static_cast<std::uint16_t>(blob[at] | (blob[at + 1] << 8));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 4 > blob.size()) {
        throw std::runtime_error("WAV read past end of file");
    }
    return static_cast<std::uint32_t>(blob[at]) |
           (static_cast<std::uint32_t>(blob[at + 1]) << 8) |
           (static_cast<std::uint32_t>(blob[at + 2]) << 16) |
           (static_cast<std::uint32_t>(blob[at + 3]) << 24);
}

bool tagIs(const std::vector<std::uint8_t>& blob, std::size_t at, const char* tag) {
    return at + 4 <= blob.size() && std::memcmp(&blob[at], tag, 4) == 0;
}

}  // namespace

const char* const kLooseWavFiles[4] = {"BLKVIC.WAV", "STWPRES.WAV", "SWTHEME.WAV", "WHTVIC.WAV"};

double WaveSound::durationSeconds() const {
    std::size_t frameBytes =
        static_cast<std::size_t>(channels) * static_cast<std::size_t>(bitsPerSample) / 8;
    if (frameBytes == 0 || sampleRate == 0) {
        return 0.0;
    }
    return static_cast<double>(samples.size() / frameBytes) / static_cast<double>(sampleRate);
}

std::size_t riffLength(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (!tagIs(blob, at, "RIFF")) {
        throw std::runtime_error("no RIFF header where one was expected");
    }
    return 8 + static_cast<std::size_t>(readU32(blob, at + 4));
}

WaveSound parseWave(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (!tagIs(blob, at, "RIFF") || !tagIs(blob, at + 8, "WAVE")) {
        throw std::runtime_error("not a RIFF WAVE");
    }
    std::size_t end = at + riffLength(blob, at);
    if (end > blob.size()) {
        end = blob.size();
    }

    WaveSound sound;
    // Walk the chunks. Every chunk pads to an even length.
    std::size_t pos = at + 12;
    while (pos + 8 <= end) {
        std::size_t body = pos + 8;
        std::size_t size = readU32(blob, pos + 4);
        if (tagIs(blob, pos, "fmt ") && size >= 16) {
            sound.formatTag = readU16(blob, body + 0);
            sound.channels = readU16(blob, body + 2);
            sound.sampleRate = readU32(blob, body + 4);
            sound.bitsPerSample = readU16(blob, body + 14);
        } else if (tagIs(blob, pos, "data")) {
            std::size_t stop = body + size;
            if (stop > blob.size()) {
                stop = blob.size();
            }
            sound.samples.assign(blob.begin() + static_cast<std::ptrdiff_t>(body),
                                 blob.begin() + static_cast<std::ptrdiff_t>(stop));
        }
        pos = body + size + (size & 1u);
    }
    return sound;
}

WaveSound loadWavFile(const std::string& path) {
    std::vector<std::uint8_t> blob = readBinaryFile(path);
    return parseWave(blob, 0);
}

std::vector<WaveResource> loadAudioDll(const std::string& dir) {
    std::vector<std::uint8_t> blob = readBinaryFile(resolveCdFile(dir, "SWCAUDIO.DLL").string());
    std::vector<NeResource> resources = readNeResources(blob);
    std::vector<WaveResource> out;
    for (const NeResource& resource : resources) {
        if (resource.type.isNumeric || resource.type.name != "WAVE") {
            continue;
        }
        WaveResource wave;
        wave.name = resource.name.text();
        wave.offset = resource.offset;
        wave.paddedLength = resource.length;
        wave.riffLength = riffLength(blob, resource.offset);
        wave.sound = parseWave(blob, resource.offset);
        out.push_back(std::move(wave));
    }
    return out;
}

}  // namespace swchess
