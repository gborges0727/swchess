#include "export/extract.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <unistd.h>

#include "assets/anx.h"
#include "assets/ini.h"
#include "assets/ne.h"
#include "export/json_write.h"
#include "export/png_write.h"
#include "export/sha256.h"
#include "export/text.h"

namespace swchess::exporter {
namespace {

namespace fs = std::filesystem;

const char* const kVersion = "1.0.0";

// The twelve piece DLLs, in the order tools/extract/pieces.py lists them.
const char* const kPieces[] = {"AT", "BF", "C3", "CB", "DV", "EM",
                               "LO", "LS", "R2", "SP", "ST", "YO"};

// The direction sections a piece INI can carry, in the order they appear.
const char* const kDirections[] = {"S", "N", "E", "W", "NE", "NW", "SE",
                                   "SW", "R", "US", "UN", "DN", "DS"};

const char* const kBackgrounds[] = {"2DBDBTOP.BMP", "2DBDWTOP.BMP", "SPACE256.BMP",
                                    "THRON256.BMP"};

// The four CM.INI keys and the sheet each one names.
const std::pair<const char*, const char*> kSheets[] = {{"2DSET_", "2DSET_P.BMP"},
                                                       {"WHTBTM_", "WHTBTM_P.BMP"},
                                                       {"WHTTOP_", "WHTTOP_P.BMP"},
                                                       {"FACING_", "FACING_P.BMP"}};

const char* const kLooseWaves[] = {"BLKVIC.WAV", "STWPRES.WAV", "SWTHEME.WAV", "WHTVIC.WAV"};

const std::pair<const char*, const char*> kLanguages[] = {{"RESENG.DLL", "english"},
                                                          {"RESFRN.DLL", "french"},
                                                          {"RESGER.DLL", "german"},
                                                          {"RESSPN.DLL", "spanish"}};

// The named files tools/extract reads. The globbed ANX, INI, BMP and WAV files
// join these in the source hash list.
const char* const kNamedSources[] = {
    "AT.DLL", "BF.DLL", "C3.DLL", "CB.DLL", "DV.DLL", "EM.DLL",
    "LO.DLL", "LS.DLL", "R2.DLL", "SP.DLL", "ST.DLL", "YO.DLL",
    "SWCAUDIO.DLL", "TITLERES.DLL",
    "RESENG.DLL", "RESFRN.DLL", "RESGER.DLL", "RESSPN.DLL"};

// Every ANX offset slot counts from this file offset, and each of the three
// tables in front of it holds 150 slots.
constexpr std::size_t kAnxBase = 0x70C;
constexpr std::size_t kAnxSlots = 150;
constexpr std::size_t kAnxPositionTable = 0x25C;
constexpr std::size_t kAnxLengthTable = 0x4B4;

// FUN_1058_0ce6 loads a 640 by 480 backdrop and FUN_1058_0150 clips to it.
constexpr int kCanvasWidth = 640;
constexpr int kCanvasHeight = 480;

// The defaults GetPrivateProfileInt passes when a [XXXX_OFFSET] key is absent.
constexpr int kDefaultOffsetX = 215;
constexpr int kDefaultOffsetY = 100;
constexpr int kDefaultHoldMs = 1000;

constexpr int kSheetColumns = 6;
constexpr int kSheetRows = 2;

// Strips the whitespace off both ends, the way Python's str.strip() does.
std::string trim(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }
    return text.substr(first, last - first);
}

std::vector<std::uint8_t> readFileBytes(const fs::path& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<std::uint8_t> blob(static_cast<std::size_t>(size < 0 ? 0 : size));
    if (!blob.empty() && std::fread(blob.data(), 1, blob.size(), file) != blob.size()) {
        std::fclose(file);
        throw std::runtime_error("short read on " + path.string());
    }
    std::fclose(file);
    return blob;
}

void writeFileBytes(const fs::path& path, const std::vector<std::uint8_t>& blob) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot write " + path.string());
    }
    std::size_t written = std::fwrite(blob.data(), 1, blob.size(), file);
    std::fclose(file);
    if (written != blob.size()) {
        throw std::runtime_error("short write on " + path.string());
    }
}

std::uint32_t readU32(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 4 > blob.size()) {
        throw std::runtime_error("read past the end of a file");
    }
    return static_cast<std::uint32_t>(blob[at]) |
           (static_cast<std::uint32_t>(blob[at + 1]) << 8) |
           (static_cast<std::uint32_t>(blob[at + 2]) << 16) |
           (static_cast<std::uint32_t>(blob[at + 3]) << 24);
}

std::int16_t readI16(const std::vector<std::uint8_t>& blob, std::size_t at) {
    if (at + 2 > blob.size()) {
        throw std::runtime_error("read past the end of a file");
    }
    return static_cast<std::int16_t>(blob[at] | (blob[at + 1] << 8));
}

// The names in one directory that end in `suffix`, sorted the way Python's
// sorted(glob(...)) sorts them.
std::vector<std::string> namesEndingIn(const fs::path& dir, const std::string& suffix) {
    std::vector<std::string> out;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string hexOffset(std::size_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%zx", value);
    return std::string(buffer);
}

std::string recordFileName(std::uint32_t offset) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "rec%08x.png", offset);
    return std::string(buffer);
}

// Rounds the way Python's round(value, 6) does: to the nearest number with six
// decimal places, and to the even one on a tie.
double roundTo6(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return std::strtod(buffer, nullptr);
}

// Rounds the way Python's round(value) does, which is to the nearest whole
// number and to the even one on a tie.
std::int64_t roundHalfEven(double value) {
    return static_cast<std::int64_t>(std::nearbyint(value));
}

Json optionalInt(const std::optional<int>& value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

// Returns the section's keys and values as an ordered object, the way
// IniFile.Section.raw() returns a dict. A repeated key keeps its first
// position and takes its last value.
Json rawSection(const IniSection* section) {
    Json out = Json::object();
    if (section == nullptr) {
        return out;
    }
    for (const std::string& key : section->keys()) {
        out[cp437ToUtf8(key)] = cp437ToUtf8(section->get(key));
    }
    return out;
}

std::optional<int> sectionInt(const IniSection* section, const char* key) {
    if (section == nullptr || !section->has(key)) {
        return std::nullopt;
    }
    return parseInt(section->get(key));
}

int sectionIntOr(const IniSection* section, const char* key, int fallback) {
    std::optional<int> value = sectionInt(section, key);
    return value.value_or(fallback);
}

Json canvasJson() {
    Json canvas = Json::object();
    canvas["x"] = 0;
    canvas["y"] = 0;
    canvas["w"] = kCanvasWidth;
    canvas["h"] = kCanvasHeight;
    return canvas;
}

// ---------------------------------------------------------------------------
// Plain Windows bitmaps
// ---------------------------------------------------------------------------

// One uncompressed 8-bit DIB, either a whole .BMP file or a bitmap resource
// inside a DLL. This mirrors tools/extract/dib.py.
struct Dib {
    int width = 0;
    int height = 0;
    bool topDown = false;
    int bitCount = 0;
    std::vector<std::uint8_t> palette;
    std::size_t stride = 0;
    std::vector<std::uint8_t> pixels;
};

Dib parseDib(const std::vector<std::uint8_t>& blob, std::size_t start,
             std::size_t pixelOffset, bool havePixelOffset) {
    Dib dib;
    std::uint32_t headerSize = readU32(blob, start);
    dib.width = static_cast<std::int32_t>(readU32(blob, start + 4));
    std::int32_t height = static_cast<std::int32_t>(readU32(blob, start + 8));
    std::uint32_t planesAndBits = readU32(blob, start + 12);
    dib.bitCount = static_cast<int>(planesAndBits >> 16);
    std::uint32_t compression = readU32(blob, start + 16);
    std::uint32_t colorsUsed = readU32(blob, start + 32);
    if (compression != 0) {
        throw std::runtime_error("expected an uncompressed DIB");
    }
    if (dib.bitCount != 8) {
        throw std::runtime_error("expected 8 bits per pixel");
    }
    dib.topDown = height < 0;
    dib.height = height < 0 ? -height : height;
    std::size_t entries = colorsUsed != 0 ? colorsUsed : 256;
    std::size_t paletteAt = start + headerSize;
    dib.palette.assign(blob.begin() + static_cast<std::ptrdiff_t>(paletteAt),
                       blob.begin() + static_cast<std::ptrdiff_t>(paletteAt + entries * 4));
    std::size_t pixelsAt = havePixelOffset ? pixelOffset : paletteAt + entries * 4;
    dib.stride = (static_cast<std::size_t>(dib.width) + 3) & ~std::size_t(3);
    std::size_t want = dib.stride * static_cast<std::size_t>(dib.height);
    std::size_t stop = pixelsAt + want;
    if (stop > blob.size()) {
        stop = blob.size();
    }
    dib.pixels.assign(blob.begin() + static_cast<std::ptrdiff_t>(pixelsAt),
                      blob.begin() + static_cast<std::ptrdiff_t>(stop));
    return dib;
}

Dib readBmpFile(const fs::path& path, std::size_t* fileSize) {
    std::vector<std::uint8_t> blob = readFileBytes(path);
    if (blob.size() < 2 || blob[0] != 'B' || blob[1] != 'M') {
        throw std::runtime_error(path.string() + " does not start with BM");
    }
    if (fileSize != nullptr) {
        *fileSize = blob.size();
    }
    return parseDib(blob, 14, readU32(blob, 10), true);
}

std::uint8_t dibIndexAt(const Dib& dib, int x, int y) {
    int row = dib.topDown ? y : dib.height - 1 - y;
    return dib.pixels[static_cast<std::size_t>(row) * dib.stride + static_cast<std::size_t>(x)];
}

// Copies one rectangle out of a DIB as top-down RGB or RGBA. Pass -1 for
// `transparentIndex` to keep every pixel opaque.
std::vector<std::uint8_t> dibToRows(const Dib& dib, int rx, int ry, int rw, int rh,
                                    int transparentIndex) {
    const int channels = transparentIndex < 0 ? 3 : 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(rw) * rh * channels);
    std::size_t at = 0;
    for (int y = ry; y < ry + rh; ++y) {
        int row = dib.topDown ? y : dib.height - 1 - y;
        std::size_t base = static_cast<std::size_t>(row) * dib.stride;
        for (int x = rx; x < rx + rw; ++x) {
            std::uint8_t index = dib.pixels[base + static_cast<std::size_t>(x)];
            const std::uint8_t* entry = &dib.palette[static_cast<std::size_t>(index) * 4];
            out[at++] = entry[2];
            out[at++] = entry[1];
            out[at++] = entry[0];
            if (channels == 4) {
                out[at++] = index == transparentIndex ? 0 : 255;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The escape-byte RLE records the ANX files and the piece DLLs share
// ---------------------------------------------------------------------------

// The decoded record plus the header fields the manifests record.
struct RleRecord {
    AnxRecord decoded;
    int bitCount = 0;
    std::uint32_t compression = 0;
    std::size_t paletteEntries = 0;
};

RleRecord decodeRecord(const std::vector<std::uint8_t>& blob, std::size_t start,
                       std::size_t end) {
    RleRecord out;
    out.decoded = decodeRleRecord(blob, start, end);
    std::uint32_t planesAndBits = readU32(blob, start + 12);
    out.bitCount = static_cast<int>(planesAndBits >> 16);
    out.compression = readU32(blob, start + 16);
    out.paletteEntries = out.decoded.palette.size() / 4;
    return out;
}

// Turns one record into top-down RGBA with palette index 0 fully clear.
std::vector<std::uint8_t> recordToRgba(const AnxRecord& record) {
    return anxToRGBA(record);
}

// ---------------------------------------------------------------------------
// Sound cues
// ---------------------------------------------------------------------------

// How long each sound plays, in milliseconds, under its upper case name. A
// name with no length known keeps an empty entry.
struct SoundIndex {
    std::map<std::string, std::optional<std::int64_t>> resources;
    std::map<std::string, std::optional<std::int64_t>> files;
};

// ---------------------------------------------------------------------------
// Stage results
// ---------------------------------------------------------------------------

struct AudioStage {
    Json summary;
    SoundIndex index;
    std::int64_t recordCount = 0;
    std::int64_t distinctNames = 0;
};

struct CaptureStage {
    Json captures = Json::array();
    Json records = Json::array();
    Json silent = Json::array();
    std::int64_t distinctRecords = 0;
    std::int64_t timelineEntries = 0;
    std::int64_t poses = 0;
    std::int64_t captureCount = 0;
    std::int64_t blockingCaptures = 0;
    std::vector<std::string> summaryLines;
};

struct PieceStage {
    Json pieces = Json::array();
    Json bitmaps = Json::array();
    Json mismatches = Json::array();
    std::int64_t bitmapCount = 0;
};

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------

// Reads the fmt and data chunks the way tools/extract/audio.py reads them.
// The declared data chunk size is what the duration comes from, so a truncated
// file reports the same length Python reports.
Json describeWave(const std::vector<std::uint8_t>& data) {
    Json info = Json::object();
    info["channels"] = nullptr;
    info["sample_rate"] = nullptr;
    info["bits_per_sample"] = nullptr;
    info["format_tag"] = nullptr;
    info["data_bytes"] = nullptr;
    info["duration_seconds"] = nullptr;
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        return info;
    }
    std::size_t pos = 12;
    while (pos + 8 <= data.size()) {
        std::size_t size = readU32(data, pos + 4);
        std::size_t body = pos + 8;
        if (std::memcmp(data.data() + pos, "fmt ", 4) == 0 && size >= 16 &&
            body + 16 <= data.size()) {
            info["format_tag"] = static_cast<int>(readI16(data, body) & 0xFFFF);
            info["channels"] = static_cast<int>(readI16(data, body + 2) & 0xFFFF);
            info["sample_rate"] = readU32(data, body + 4);
            info["bits_per_sample"] = static_cast<int>(readI16(data, body + 14) & 0xFFFF);
        } else if (std::memcmp(data.data() + pos, "data", 4) == 0) {
            info["data_bytes"] = static_cast<std::uint64_t>(size);
        }
        pos = body + size + (size & 1u);
    }
    if (!info["data_bytes"].is_null() && info["data_bytes"].get<std::uint64_t>() != 0 &&
        !info["sample_rate"].is_null() && info["sample_rate"].get<std::uint64_t>() != 0 &&
        !info["channels"].is_null() && info["channels"].get<int>() != 0 &&
        !info["bits_per_sample"].is_null() && info["bits_per_sample"].get<int>() != 0) {
        std::int64_t frame =
            static_cast<std::int64_t>(info["channels"].get<int>()) *
            info["bits_per_sample"].get<int>() / 8;
        if (frame != 0) {
            double seconds = static_cast<double>(info["data_bytes"].get<std::uint64_t>()) /
                             static_cast<double>(frame) /
                             static_cast<double>(info["sample_rate"].get<std::uint64_t>());
            info["duration_seconds"] = roundTo6(seconds);
        }
    }
    return info;
}

std::optional<std::int64_t> durationMs(const Json& entry) {
    const Json& seconds = entry.at("duration_seconds");
    if (seconds.is_null()) {
        return std::nullopt;
    }
    return roundHalfEven(seconds.get<double>() * 1000.0);
}

void mergeInto(Json& target, const Json& extra) {
    for (auto item = extra.begin(); item != extra.end(); ++item) {
        target[item.key()] = item.value();
    }
}

AudioStage extractAudio(const fs::path& cdDir, const fs::path& outDir) {
    fs::create_directories(outDir);
    std::vector<std::uint8_t> blob = readFileBytes(cdDir / "SWCAUDIO.DLL");
    std::vector<NeResource> resources = readNeResources(blob);

    AudioStage stage;
    Json records = Json::array();
    // The first record written under each name, so a repeat can point at it.
    std::map<std::string, Json> written;
    std::set<std::string> duplicates;

    for (const NeResource& resource : resources) {
        if (resource.type.isNumeric || resource.type.name != "WAVE") {
            continue;
        }
        if (std::memcmp(&blob[resource.offset], "RIFF", 4) != 0) {
            throw std::runtime_error("no RIFF at " + hexOffset(resource.offset));
        }
        std::size_t length = 8 + readU32(blob, resource.offset + 4);
        std::vector<std::uint8_t> data(
            blob.begin() + static_cast<std::ptrdiff_t>(resource.offset),
            blob.begin() + static_cast<std::ptrdiff_t>(resource.offset + length));
        std::string digest = sha256Hex(data);
        std::string name = resource.name.text();

        Json entry = Json::object();
        entry["source"] = "SWCAUDIO.DLL";
        entry["resource_name"] = name;
        entry["resource_offset"] = static_cast<std::uint64_t>(resource.offset);
        entry["resource_length"] = static_cast<std::uint64_t>(resource.length);
        entry["riff_length"] = static_cast<std::uint64_t>(length);
        entry["sha256"] = digest;
        mergeInto(entry, describeWave(data));

        auto already = written.find(name);
        if (already != written.end()) {
            entry["duplicate_of"] = already->second.at("output");
            entry["identical_bytes"] = already->second.at("sha256").get<std::string>() == digest;
            entry["output"] = nullptr;
            duplicates.insert(name);
            if (!entry["identical_bytes"].get<bool>()) {
                std::string stem = name.substr(0, name.rfind('.'));
                std::string alternate = stem + "__dup2.WAV";
                writeFileBytes(outDir / alternate, data);
                entry["output"] = "audio/" + alternate;
            }
        } else {
            writeFileBytes(outDir / name, data);
            entry["output"] = "audio/" + name;
            written.emplace(name, entry);
        }
        records.push_back(entry);
    }

    Json standalone = Json::array();
    for (const char* name : kLooseWaves) {
        std::vector<std::uint8_t> data = readFileBytes(cdDir / name);
        writeFileBytes(outDir / name, data);
        Json entry = Json::object();
        entry["source"] = name;
        entry["resource_name"] = name;
        entry["resource_offset"] = 0;
        entry["resource_length"] = static_cast<std::uint64_t>(data.size());
        bool isRiff = data.size() >= 8 && std::memcmp(data.data(), "RIFF", 4) == 0;
        entry["riff_length"] = isRiff ? static_cast<std::uint64_t>(8 + readU32(data, 4))
                                      : static_cast<std::uint64_t>(data.size());
        entry["sha256"] = sha256Hex(data);
        entry["output"] = std::string("audio/") + name;
        mergeInto(entry, describeWave(data));
        standalone.push_back(entry);
    }

    for (const auto& pair : written) {
        stage.index.resources[upperAscii(pair.first)] = durationMs(pair.second);
    }
    for (const Json& entry : standalone) {
        stage.index.files[upperAscii(entry.at("resource_name").get<std::string>())] =
            durationMs(entry);
    }

    Json duplicateNames = Json::array();
    for (const std::string& name : duplicates) {
        duplicateNames.push_back(name);
    }

    stage.summary = Json::object();
    stage.summary["resource_records"] = records;
    stage.summary["standalone_files"] = standalone;
    stage.summary["distinct_resource_names"] = static_cast<std::uint64_t>(written.size());
    stage.summary["resource_record_count"] = static_cast<std::uint64_t>(records.size());
    stage.summary["duplicate_resource_names"] = duplicateNames;
    stage.recordCount = static_cast<std::int64_t>(records.size());
    stage.distinctNames = static_cast<std::int64_t>(written.size());
    return stage;
}

// ---------------------------------------------------------------------------
// captures
// ---------------------------------------------------------------------------

// Splits an art= path into its folder, its file name and its frame number,
// the way tools/extract/captures.py parse_art does.
Json parseArt(const std::string& value) {
    if (value.empty()) {
        return Json(nullptr);
    }
    std::string path = value;
    for (char& c : path) {
        if (c == '/') {
            c = '\\';
        }
    }
    std::size_t lastSlash = path.rfind('\\');
    std::string name = lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
    Json out = Json::object();
    out["path"] = cp437ToUtf8(value);
    if (lastSlash == std::string::npos) {
        out["folder"] = nullptr;
    } else {
        std::size_t before = path.rfind('\\', lastSlash == 0 ? 0 : lastSlash - 1);
        std::size_t start = (lastSlash == 0) ? 0
                            : (before == std::string::npos ? 0 : before + 1);
        out["folder"] = cp437ToUtf8(path.substr(start, lastSlash - start));
    }
    out["file"] = cp437ToUtf8(name);
    out["frame"] = nullptr;
    out["stem"] = nullptr;

    // The name has to end in three digits, a dot and one or more word
    // characters, with at least one character of stem in front.
    std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot + 1 >= name.size() || dot < 4) {
        return out;
    }
    for (std::size_t i = dot + 1; i < name.size(); ++i) {
        char c = name[i];
        bool word = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') || c == '_';
        if (!word) {
            return out;
        }
    }
    for (std::size_t i = dot - 3; i < dot; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return out;
        }
    }
    out["frame"] = std::atoi(name.substr(dot - 3, 3).c_str());
    out["stem"] = cp437ToUtf8(name.substr(0, dot - 3));
    return out;
}

// Resolves one per-frame wav= against the WAVE resource names. FUN_1058_0c4d
// upper cases the value and looks it up with no repair, so a value that names
// no resource plays nothing.
Json resolveCue(const std::string& raw, const SoundIndex& index, const std::string& capture,
                const std::string& iniFile, const std::string& section) {
    std::string plain = upperAscii(trim(raw));
    Json entry = Json::object();
    entry["raw"] = cp437ToUtf8(raw);
    entry["normalized"] = cp437ToUtf8(plain);
    entry["resolved"] = nullptr;
    entry["duration_ms"] = nullptr;
    entry["status"] = "silent_in_original";
    entry["capture"] = capture;
    entry["ini_file"] = iniFile;
    entry["section"] = cp437ToUtf8(section);

    auto found = index.resources.find(plain);
    if (found != index.resources.end()) {
        entry["resolved"] = cp437ToUtf8(plain);
        entry["duration_ms"] =
            found->second.has_value() ? Json(*found->second) : Json(nullptr);
        entry["status"] = "resolved";
        return entry;
    }
    entry["silent_reason"] = "SWCAUDIO.DLL holds no WAVE resource named " + plain +
                             ", so the original plays nothing on this frame";
    return entry;
}

// Resolves the [XXXX_OFFSET] wav= that plays after the hold. FUN_1008_1519
// tries the name as a file before it tries the resource, so this lookup sees
// the four loose WAV files too.
Json resolveFinalWav(const std::string& raw, const SoundIndex& index) {
    if (raw.empty()) {
        return Json(nullptr);
    }
    std::string name = upperAscii(trim(raw));
    Json entry = Json::object();
    for (int pass = 0; pass < 2; ++pass) {
        const auto& table = pass == 0 ? index.files : index.resources;
        auto found = table.find(name);
        if (found != table.end()) {
            entry["name"] = cp437ToUtf8(name);
            entry["raw"] = cp437ToUtf8(raw);
            entry["source"] = pass == 0 ? "files" : "resources";
            entry["duration_ms"] = found->second.value_or(0);
            return entry;
        }
    }
    entry["name"] = cp437ToUtf8(name);
    entry["raw"] = cp437ToUtf8(raw);
    entry["source"] = nullptr;
    entry["duration_ms"] = 0;
    entry["status"] = "silent_in_original";
    return entry;
}

// One timeline entry, with the fields the timing walk needs kept beside the
// object that reaches timeline.json.
struct TimelineEntry {
    int index = 0;
    Json json;
    Json sound;  // null, or the resolved cue
    std::optional<int> pause;
    std::string image;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// What walking the timeline produced.
struct Timing {
    Json poses = Json::array();
    Json preSounds = Json::array();
    std::int64_t endMs = 0;
    std::int64_t blockingCount = 0;
    std::int64_t blockingMs = 0;
    std::int64_t finalStart = 0;
};

const char* soundMode(int pause) {
    switch (pause) {
        case 1: return "sync";
        case 2: return "wait_previous";
        default: return "async";
    }
}

// Walks the timeline the way FUN_1058_0a0a does and times every pose.
//
// Pose 0 is decoded but never drawn, and its iteration still spends a whole
// frame delay, so every later pose lands one frame delay later than it would
// if that iteration were skipped. A sync sound blocks for its whole length. A
// pause=2 sound does not block on its own frame, but the next frame that
// carries a sound waits for it to finish.
Timing buildTiming(const std::vector<TimelineEntry>& entries, int frameDelay, int holdMs,
                   const Json& finalWav) {
    Timing timing;
    std::int64_t clock = 0;
    std::int64_t lastStart = 0;
    std::int64_t lastStep = 0;
    bool pending = false;
    std::int64_t pendingEnd = 0;

    for (const TimelineEntry& entry : entries) {
        std::int64_t start = clock;
        Json sound = nullptr;
        if (!entry.sound.is_null() &&
            entry.sound.at("status").get<std::string>() == "resolved") {
            const Json& length = entry.sound.at("duration_ms");
            std::int64_t duration = length.is_null() ? 0 : length.get<std::int64_t>();
            int pause = entry.pause.value_or(0);
            if (pending) {
                if (pendingEnd > clock) {
                    timing.blockingCount += 1;
                    timing.blockingMs += pendingEnd - clock;
                    clock = pendingEnd;
                }
                pending = false;
            }
            std::int64_t soundStart = clock;
            if (pause == 1) {
                clock += duration;
                timing.blockingCount += 1;
                timing.blockingMs += duration;
            } else if (pause == 2) {
                pending = true;
                pendingEnd = clock + duration;
            }
            sound = Json::object();
            sound["t_ms"] = soundStart;
            sound["name"] = entry.sound.at("resolved");
            sound["mode"] = soundMode(pause);
            sound["duration_ms"] = duration;
        }

        if (entry.index != 0) {
            Json pose = Json::object();
            pose["index"] = entry.index;
            pose["t_ms"] = clock;
            pose["image"] = entry.image;
            pose["x"] = entry.x;
            pose["y"] = entry.y;
            pose["w"] = entry.w;
            pose["h"] = entry.h;
            pose["sound"] = sound;
            timing.poses.push_back(pose);
        } else if (!sound.is_null()) {
            timing.preSounds.push_back(sound);
        }

        lastStart = start;
        lastStep = std::max<std::int64_t>(clock - start, frameDelay);
        clock = start + lastStep;
    }

    if (!entries.empty()) {
        std::int64_t loopEnd = lastStart + lastStep;
        timing.endMs = std::max<std::int64_t>(loopEnd, lastStart + holdMs);
    } else {
        timing.endMs = holdMs;
    }
    timing.finalStart = timing.endMs;
    if (!finalWav.is_null()) {
        timing.endMs += finalWav.at("duration_ms").get<std::int64_t>();
    }
    return timing;
}

CaptureStage extractCaptures(const fs::path& cdDir, const fs::path& outDir,
                             const SoundIndex& soundIndex, int frameDelay) {
    fs::create_directories(outDir);
    CaptureStage stage;
    std::map<std::string, std::shared_ptr<IniFile>> iniCache;

    for (const std::string& anxName : namesEndingIn(cdDir, ".ANX")) {
        const std::string capture = upperAscii(anxName.substr(0, anxName.size() - 4));
        const std::string iniName = capture.substr(0, 2) + ".INI";
        auto cached = iniCache.find(iniName);
        if (cached == iniCache.end()) {
            cached = iniCache
                         .emplace(iniName, std::make_shared<IniFile>((cdDir / iniName).string()))
                         .first;
        }
        const IniFile& ini = *cached->second;

        std::vector<std::uint8_t> blob = readFileBytes(cdDir / anxName);
        const std::uint32_t frameCount = readU32(blob, 0);
        std::vector<std::uint32_t> offsets;
        offsets.reserve(frameCount);
        for (std::uint32_t i = 0; i < frameCount; ++i) {
            offsets.push_back(readU32(blob, 4 + 4 * static_cast<std::size_t>(i)));
        }
        std::vector<std::uint32_t> distinct(offsets.begin(), offsets.end());
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());

        std::map<std::uint32_t, RleRecord> records;
        for (std::size_t k = 0; k < distinct.size(); ++k) {
            std::size_t start = kAnxBase + distinct[k];
            std::size_t end = k + 1 < distinct.size() ? kAnxBase + distinct[k + 1] : blob.size();
            records.emplace(distinct[k], decodeRecord(blob, start, end));
        }

        std::vector<std::pair<int, int>> positions;
        std::vector<std::uint32_t> lengths;
        for (std::size_t i = 0; i < kAnxSlots; ++i) {
            positions.emplace_back(readI16(blob, kAnxPositionTable + 4 * i),
                                   readI16(blob, kAnxPositionTable + 4 * i + 2));
            lengths.push_back(readU32(blob, kAnxLengthTable + 4 * i));
        }

        const fs::path folder = outDir / capture;
        fs::create_directories(folder);

        std::map<std::uint32_t, std::string> images;
        for (const auto& pair : records) {
            const RleRecord& record = pair.second;
            if (!record.decoded.complete) {
                throw std::runtime_error(capture + " record " + hexOffset(pair.first) +
                                         " decoded short");
            }
            const std::string name = recordFileName(pair.first);
            std::vector<std::uint8_t> file =
                encodePng(record.decoded.width, record.decoded.height, kPngRgba,
                          recordToRgba(record.decoded), 6);
            writeFileBytes(folder / name, file);
            images[pair.first] = name;

            Json entry = Json::object();
            entry["capture"] = capture;
            entry["source"] = anxName;
            entry["record_offset"] = pair.first;
            entry["file_offset"] = static_cast<std::uint64_t>(kAnxBase + pair.first);
            entry["width"] = record.decoded.width;
            entry["height"] = record.decoded.height;
            entry["bit_count"] = record.bitCount;
            entry["escape"] = record.decoded.escape;
            entry["compression"] = record.compression;
            entry["palette_entries"] = static_cast<std::uint64_t>(record.paletteEntries);
            entry["palette_sha256"] = sha256Hex(record.decoded.palette);
            entry["transparent_index"] = 0;
            entry["output"] = "captures/" + capture + "/" + name;
            entry["output_sha256"] = sha256Hex(file);
            stage.records.push_back(entry);
            stage.distinctRecords += 1;
        }

        const IniSection* section = ini.section(capture);
        std::vector<std::string> keys;
        if (section != nullptr) {
            keys = section->keys();
        }
        if (keys.size() > frameCount) {
            throw std::runtime_error(capture + ": more INI keys than ANX records");
        }

        const IniSection* offsetSection = ini.section(capture + "_OFFSET");
        Json offsetRaw = rawSection(offsetSection);
        const int offsetX = sectionIntOr(offsetSection, "x", kDefaultOffsetX);
        const int offsetY = sectionIntOr(offsetSection, "y", kDefaultOffsetY);
        const int holdMs = sectionIntOr(offsetSection, "hold", kDefaultHoldMs);
        Json finalWav = resolveFinalWav(
            offsetSection != nullptr ? offsetSection->get("wav") : std::string(), soundIndex);

        Json placement = Json::object();
        placement["raw"] = offsetRaw;
        placement["x"] = offsetX;
        placement["y"] = offsetY;
        placement["x_from_default"] = offsetSection == nullptr || !offsetSection->has("x");
        placement["y_from_default"] = offsetSection == nullptr || !offsetSection->has("y");
        placement["hold_ms"] = holdMs;
        placement["wav"] = finalWav;

        std::vector<TimelineEntry> entries;
        for (std::size_t index = 0; index < keys.size(); ++index) {
            const std::string& key = keys[index];
            const IniSection* frame = ini.section(key);
            Json raw = rawSection(frame);
            Json sound = nullptr;
            if (frame != nullptr && !frame->get("wav").empty()) {
                sound = resolveCue(frame->get("wav"), soundIndex, capture, iniName, key);
                if (sound.at("status").get<std::string>() == "silent_in_original") {
                    stage.silent.push_back(sound);
                }
            }
            const std::uint32_t off = offsets[index];
            const RleRecord& record = records.at(off);
            const int tableX = positions[index].first;
            const int tableY = positions[index].second;

            TimelineEntry entry;
            entry.index = static_cast<int>(index);
            entry.sound = sound;
            entry.pause = frame != nullptr && frame->has("pause")
                              ? parseInt(frame->get("pause"))
                              : std::nullopt;
            entry.image = images[off];
            entry.x = tableX + offsetX;
            entry.y = tableY + offsetY;
            entry.w = record.decoded.width;
            entry.h = record.decoded.height;

            Json item = Json::object();
            item["index"] = entry.index;
            item["ini_key"] = cp437ToUtf8(key);
            item["frame_number"] = entry.index + 1;
            item["record_offset"] = off;
            item["image"] = entry.image;
            item["width"] = entry.w;
            item["height"] = entry.h;
            item["compressed_length"] = lengths[index];
            item["table_x"] = tableX;
            item["table_y"] = tableY;
            item["x"] = entry.x;
            item["y"] = entry.y;
            item["ini_x"] = optionalInt(frame != nullptr && frame->has("x")
                                            ? parseInt(frame->get("x"))
                                            : std::nullopt);
            item["ini_y"] = optionalInt(frame != nullptr && frame->has("y")
                                            ? parseInt(frame->get("y"))
                                            : std::nullopt);
            item["pause"] = optionalInt(entry.pause);
            item["art"] = parseArt(frame != nullptr ? frame->get("art") : std::string());
            item["sound"] = sound;
            item["ini"] = raw;
            entry.json = item;
            entries.push_back(std::move(entry));
        }
        stage.timelineEntries += static_cast<std::int64_t>(entries.size());

        Timing timing = buildTiming(entries, frameDelay, holdMs, finalWav);
        stage.poses += static_cast<std::int64_t>(timing.poses.size());
        const std::int64_t nominal =
            static_cast<std::int64_t>(entries.empty() ? 0 : entries.size() - 1) * frameDelay +
            holdMs + (finalWav.is_null() ? 0 : finalWav.at("duration_ms").get<std::int64_t>());
        const std::int64_t added = timing.endMs - nominal;
        if (timing.blockingCount != 0) {
            stage.blockingCaptures += 1;
        }

        std::int64_t last = -1;
        for (const Json& pose : timing.poses) {
            std::int64_t t = pose.at("t_ms").get<std::int64_t>();
            if (t < last) {
                throw std::runtime_error(capture + ": pose times run backwards");
            }
            last = t;
            if (!fs::exists(folder / pose.at("image").get<std::string>())) {
                throw std::runtime_error(capture + ": missing image " +
                                         pose.at("image").get<std::string>());
            }
        }

        Json resolved = Json::object();
        resolved["capture"] = capture;
        resolved["frame_delay_ms"] = frameDelay;
        resolved["hold_ms"] = holdMs;
        resolved["canvas"] = canvasJson();
        resolved["pre_sounds"] = timing.preSounds;
        resolved["poses"] = timing.poses;
        resolved["end_ms"] = timing.endMs;
        if (finalWav.is_null()) {
            resolved["final_wav"] = nullptr;
        } else {
            Json entry = Json::object();
            entry["t_ms"] = timing.finalStart;
            entry["name"] = finalWav.at("name");
            entry["duration_ms"] = finalWav.at("duration_ms");
            resolved["final_wav"] = entry;
        }
        resolved["cuts"] = Json::array();
        writeJsonFile((folder / "resolved.json").string(), resolved, true, false);

        Json entryList = Json::array();
        for (const TimelineEntry& entry : entries) {
            entryList.push_back(entry.json);
        }
        std::set<std::uint32_t> used(offsets.begin(),
                                     offsets.begin() + static_cast<std::ptrdiff_t>(entries.size()));

        Json timeline = Json::object();
        timeline["capture"] = capture;
        timeline["anx_file"] = anxName;
        timeline["ini_file"] = iniName;
        timeline["ini_section"] = capture;
        timeline["frame_count_in_anx"] = frameCount;
        timeline["ini_key_count"] = static_cast<std::uint64_t>(keys.size());
        timeline["timeline_length"] = static_cast<std::uint64_t>(entries.size());
        timeline["unused_anx_records"] =
            static_cast<std::int64_t>(frameCount) - static_cast<std::int64_t>(entries.size());
        timeline["distinct_records"] = static_cast<std::uint64_t>(records.size());
        timeline["reused_references"] =
            static_cast<std::int64_t>(entries.size()) - static_cast<std::int64_t>(used.size());
        timeline["frame_delay_ms"] = frameDelay;
        timeline["capture_offset"] = placement;
        timeline["canvas"] = canvasJson();
        timeline["poses_shown"] = static_cast<std::uint64_t>(timing.poses.size());
        timeline["end_ms"] = timing.endMs;
        timeline["blocking_sound_count"] = timing.blockingCount;
        timeline["blocking_stall_ms"] = timing.blockingMs;
        timeline["nominal_end_ms"] = nominal;
        timeline["blocking_added_ms"] = added;
        timeline["entries"] = entryList;
        writeJsonFile((folder / "timeline.json").string(), timeline, true, false);

        Json summary = Json::object();
        summary["capture"] = capture;
        summary["anx_file"] = anxName;
        summary["ini_file"] = iniName;
        summary["distinct_records"] = static_cast<std::uint64_t>(records.size());
        summary["frame_count_in_anx"] = frameCount;
        summary["timeline_entries"] = static_cast<std::uint64_t>(entries.size());
        summary["unused_anx_records"] =
            static_cast<std::int64_t>(frameCount) - static_cast<std::int64_t>(entries.size());
        summary["poses_shown"] = static_cast<std::uint64_t>(timing.poses.size());
        summary["hold_ms"] = holdMs;
        summary["end_ms"] = timing.endMs;
        summary["blocking_sound_count"] = timing.blockingCount;
        summary["blocking_stall_ms"] = timing.blockingMs;
        summary["nominal_end_ms"] = nominal;
        summary["blocking_added_ms"] = added;
        summary["offset_x"] = offsetX;
        summary["offset_y"] = offsetY;
        summary["final_wav"] = finalWav;
        Json compressed = Json::array();
        for (std::uint32_t i = 0; i < frameCount && i < lengths.size(); ++i) {
            compressed.push_back(lengths[i]);
        }
        summary["compressed_lengths"] = compressed;
        summary["output"] = "captures/" + capture + "/resolved.json";
        stage.captures.push_back(summary);
        stage.captureCount += 1;

        char line[256];
        std::snprintf(line, sizeof(line), "  %s poses %3zu end_ms %6lld blocking sounds %lld",
                      capture.c_str(), timing.poses.size(),
                      static_cast<long long>(timing.endMs),
                      static_cast<long long>(timing.blockingCount));
        std::string text(line);
        if (timing.blockingCount != 0) {
            char more[256];
            std::snprintf(more, sizeof(more),
                          " (stalled %lld ms, end_ms %lld ms later than the %lld ms a run with"
                          " no blocking sound takes)",
                          static_cast<long long>(timing.blockingMs),
                          static_cast<long long>(added), static_cast<long long>(nominal));
            text += more;
        }
        stage.summaryLines.push_back(text);
    }
    return stage;
}

// ---------------------------------------------------------------------------
// pieces
// ---------------------------------------------------------------------------

// Splits a resource name such as AT_S002 into its piece, direction and frame.
bool classifyPieceName(const std::string& name, std::string* direction, int* frame) {
    std::size_t underscore = name.find('_');
    if (underscore != 2) {
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    std::size_t at = 3;
    std::size_t letters = 0;
    while (at < name.size() && name[at] >= 'A' && name[at] <= 'Z' && letters < 2) {
        ++at;
        ++letters;
    }
    if (letters == 0 || at >= name.size()) {
        return false;
    }
    for (std::size_t i = at; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }
    *direction = name.substr(3, letters);
    *frame = std::atoi(name.substr(at).c_str());
    return true;
}

PieceStage extractPieces(const fs::path& cdDir, const fs::path& outDir) {
    fs::create_directories(outDir);
    PieceStage stage;

    for (const char* piece : kPieces) {
        const std::string name(piece);
        std::vector<std::uint8_t> blob = readFileBytes(cdDir / (name + ".DLL"));
        std::vector<NeResource> resources = readNeResources(blob);
        const fs::path folder = outDir / name;
        fs::create_directories(folder);

        std::map<std::string, std::vector<Json>> byDirection;
        std::size_t bitmapCount = 0;
        for (const NeResource& resource : resources) {
            if (!resource.type.isNumeric || resource.type.id != kRtBitmap) {
                continue;
            }
            ++bitmapCount;
            RleRecord record =
                decodeRecord(blob, resource.offset, resource.offset + resource.length);
            if (!record.decoded.complete) {
                throw std::runtime_error(name + ".DLL resource " + resource.name.text() +
                                         " decoded short");
            }
            const std::string resourceName = resource.name.text();
            const std::string file = resourceName + ".png";
            std::vector<std::uint8_t> png =
                encodePng(record.decoded.width, record.decoded.height, kPngRgba,
                          recordToRgba(record.decoded), 6);
            writeFileBytes(folder / file, png);

            std::string direction;
            int frame = 0;
            const bool parsed = classifyPieceName(resourceName, &direction, &frame);

            Json entry = Json::object();
            entry["piece"] = name;
            entry["source"] = name + ".DLL";
            entry["resource_name"] = resourceName;
            entry["resource_offset"] = static_cast<std::uint64_t>(resource.offset);
            entry["resource_length"] = static_cast<std::uint64_t>(resource.length);
            entry["direction"] = parsed ? Json(direction) : Json(nullptr);
            entry["frame"] = parsed ? Json(frame) : Json(nullptr);
            entry["width"] = record.decoded.width;
            entry["height"] = record.decoded.height;
            entry["escape"] = record.decoded.escape;
            entry["compression"] = record.compression;
            entry["palette_entries"] = static_cast<std::uint64_t>(record.paletteEntries);
            entry["palette_sha256"] = sha256Hex(record.decoded.palette);
            entry["transparent_index"] = 0;
            entry["output"] = "pieces/" + name + "/" + file;
            entry["output_sha256"] = sha256Hex(png);
            stage.bitmaps.push_back(entry);
            stage.bitmapCount += 1;
            if (parsed) {
                byDirection[direction].push_back(entry);
            }
        }

        IniFile ini((cdDir / (name + ".INI")).string());
        Json sequences = Json::array();
        Json directions = Json::array();
        for (const char* direction : kDirections) {
            const IniSection* section = ini.section(direction);
            auto found = byDirection.find(direction);
            if (section == nullptr && found == byDirection.end()) {
                continue;
            }
            Json raw = rawSection(section);
            std::optional<int> declared = sectionInt(section, "count");
            Json steps = Json::array();
            for (auto item = raw.begin(); item != raw.end(); ++item) {
                std::string key = item.key();
                if (upperAscii(key) == "COUNT") {
                    continue;
                }
                const std::string value = item.value().get<std::string>();
                std::vector<std::string> parts;
                std::size_t start = 0;
                while (true) {
                    std::size_t comma = value.find(',', start);
                    parts.push_back(trim(value.substr(
                        start, comma == std::string::npos ? std::string::npos : comma - start)));
                    if (comma == std::string::npos) {
                        break;
                    }
                    start = comma + 1;
                }
                Json step = Json::object();
                step["key"] = key;
                step["frame"] = optionalInt(parseInt(key));
                step["dx"] = optionalInt(parts.empty() ? std::nullopt : parseInt(parts[0]));
                step["dy"] = optionalInt(parts.size() > 1 ? parseInt(parts[1]) : std::nullopt);
                step["raw"] = value;
                steps.push_back(step);
            }

            std::vector<Json> frames;
            if (found != byDirection.end()) {
                frames = found->second;
            }
            std::stable_sort(frames.begin(), frames.end(), [](const Json& a, const Json& b) {
                return a.at("frame").get<int>() < b.at("frame").get<int>();
            });

            Json bitmaps = Json::array();
            for (const Json& frame : frames) {
                Json item = Json::object();
                item["resource_name"] = frame.at("resource_name");
                item["frame"] = frame.at("frame");
                const std::string output = frame.at("output").get<std::string>();
                item["image"] = output.substr(output.rfind('/') + 1);
                item["width"] = frame.at("width");
                item["height"] = frame.at("height");
                bitmaps.push_back(item);
            }

            Json sequence = Json::object();
            sequence["direction"] = direction;
            sequence["kind"] = std::string(direction) == "R" ? "rotation" : "walk";
            sequence["declared_count"] = optionalInt(declared);
            sequence["ini_step_count"] = static_cast<std::uint64_t>(steps.size());
            sequence["bitmap_count"] = static_cast<std::uint64_t>(frames.size());
            sequence["steps"] = steps;
            sequence["bitmaps"] = bitmaps;
            if (declared.has_value() &&
                (*declared != static_cast<int>(frames.size()) ||
                 *declared != static_cast<int>(steps.size()))) {
                std::vector<std::string> reasons;
                if (*declared != static_cast<int>(frames.size())) {
                    reasons.push_back(
                        "the INI count differs from the number of bitmaps in the DLL");
                }
                if (*declared != static_cast<int>(steps.size())) {
                    reasons.push_back(
                        "the INI count differs from the number of step lines in the section");
                }
                std::string note = reasons[0];
                for (std::size_t i = 1; i < reasons.size(); ++i) {
                    note += " and " + reasons[i];
                }
                Json mismatch = Json::object();
                mismatch["piece"] = name;
                mismatch["direction"] = direction;
                mismatch["declared_count"] = *declared;
                mismatch["ini_step_count"] = static_cast<std::uint64_t>(steps.size());
                mismatch["bitmap_count"] = static_cast<std::uint64_t>(frames.size());
                mismatch["note"] = note;
                sequence["count_mismatch"] = mismatch;
                stage.mismatches.push_back(mismatch);
            }
            sequences.push_back(sequence);
            directions.push_back(direction);
        }

        Json manifest = Json::object();
        manifest["piece"] = name;
        manifest["dll"] = name + ".DLL";
        manifest["ini"] = name + ".INI";
        manifest["bitmap_count"] = static_cast<std::uint64_t>(bitmapCount);
        manifest["sequences"] = sequences;
        writeJsonFile((folder / "manifest.json").string(), manifest, true, false);

        Json summary = Json::object();
        summary["piece"] = name;
        summary["bitmap_count"] = static_cast<std::uint64_t>(bitmapCount);
        summary["directions"] = directions;
        summary["output"] = "pieces/" + name + "/manifest.json";
        stage.pieces.push_back(summary);
    }
    return stage;
}

// ---------------------------------------------------------------------------
// piece sheets and backgrounds
// ---------------------------------------------------------------------------

// CM.INI [demo] numbers the attackers K=0 Q=1 R=2 B=3 N=4 P=5. The sheets have
// not been matched to that order yet, so the manifest marks this unverified.
const char* const kAssumedColumns[] = {"K", "Q", "R", "B", "N", "P"};
const char* const kAssumedRows[] = {"white", "black"};

Json extractSets(const fs::path& cdDir, const fs::path& outDir) {
    fs::create_directories(outDir);
    IniFile cm((cdDir / "CM.INI").string());
    const IniSection* chesssets = cm.section("chesssets");
    Json entries = Json::array();

    for (const auto& sheet : kSheets) {
        const std::string key = sheet.first;
        const std::string filename = sheet.second;
        const bool haveValue = chesssets != nullptr && chesssets->has(key);
        const std::string rawValue = haveValue ? chesssets->get(key) : std::string();
        std::vector<std::string> tokens = haveValue ? iniTokens(rawValue) : std::vector<std::string>();
        std::optional<int> cellW;
        std::optional<int> cellH;
        if (tokens.size() > 5) {
            cellW = std::atoi(tokens[4].c_str());
            cellH = std::atoi(tokens[5].c_str());
        }

        std::size_t fileSize = 0;
        const fs::path source = cdDir / filename;
        Dib bitmap = readBmpFile(source, &fileSize);
        const fs::path folder = outDir / key.substr(0, key.size() - 1);
        fs::create_directories(folder);

        writePng((folder / "sheet.png").string(), bitmap.width, bitmap.height, kPngRgb,
                 dibToRows(bitmap, 0, 0, bitmap.width, bitmap.height, -1), 6);

        Json cells = Json::array();
        Json separators = Json::object();
        separators["columns"] = Json::array();
        separators["rows"] = Json::array();
        separators["all_flat"] = nullptr;
        const int gridW = cellW.has_value() ? kSheetColumns * (*cellW + 1) : 0;
        const int gridH = cellH.has_value() ? kSheetRows * (*cellH + 1) : 0;
        if (cellW.has_value() && cellH.has_value() && *cellW != 0 && *cellH != 0) {
            bool flat = true;
            for (int c = 0; c < kSheetColumns; ++c) {
                const int x = c * (*cellW + 1);
                if (x >= bitmap.width) {
                    continue;
                }
                const int limit = std::min(gridH, bitmap.height);
                const std::uint8_t first = dibIndexAt(bitmap, x, 0);
                bool same = true;
                for (int y = 0; y < limit; ++y) {
                    if (dibIndexAt(bitmap, x, y) != first) {
                        same = false;
                        break;
                    }
                }
                Json item = Json::object();
                item["x"] = x;
                item["flat"] = same;
                item["palette_index"] = first;
                separators["columns"].push_back(item);
                flat = flat && same;
            }
            for (int r = 0; r < kSheetRows; ++r) {
                const int y = r * (*cellH + 1);
                if (y >= bitmap.height) {
                    continue;
                }
                const int limit = std::min(gridW, bitmap.width);
                const std::uint8_t first = dibIndexAt(bitmap, 0, y);
                bool same = true;
                for (int x = 0; x < limit; ++x) {
                    if (dibIndexAt(bitmap, x, y) != first) {
                        same = false;
                        break;
                    }
                }
                Json item = Json::object();
                item["y"] = y;
                item["flat"] = same;
                item["palette_index"] = first;
                separators["rows"].push_back(item);
                flat = flat && same;
            }
            separators["all_flat"] = flat;

            for (int r = 0; r < kSheetRows; ++r) {
                for (int c = 0; c < kSheetColumns; ++c) {
                    const int x = 1 + c * (*cellW + 1);
                    const int y = 1 + r * (*cellH + 1);
                    if (x + *cellW > bitmap.width || y + *cellH > bitmap.height) {
                        continue;
                    }
                    char cellName[32];
                    std::snprintf(cellName, sizeof(cellName), "r%dc%d.png", r, c);
                    writePng((folder / cellName).string(), *cellW, *cellH, kPngRgba,
                             dibToRows(bitmap, x, y, *cellW, *cellH, 0), 6);
                    Json cell = Json::object();
                    cell["row"] = r;
                    cell["column"] = c;
                    cell["x"] = x;
                    cell["y"] = y;
                    cell["width"] = *cellW;
                    cell["height"] = *cellH;
                    cell["image"] = cellName;
                    cell["assumed_color"] = r < 2 ? Json(kAssumedRows[r]) : Json(nullptr);
                    cell["assumed_piece"] = c < 6 ? Json(kAssumedColumns[c]) : Json(nullptr);
                    cells.push_back(cell);
                }
            }
        }

        Json tokenList = Json::array();
        for (const std::string& token : tokens) {
            tokenList.push_back(cp437ToUtf8(token));
        }

        Json manifest = Json::object();
        manifest["set"] = key;
        manifest["source"] = filename;
        manifest["source_sha256"] = sha256File(source.string());
        manifest["cm_ini_value"] = haveValue ? Json(cp437ToUtf8(rawValue)) : Json(nullptr);
        manifest["cm_ini_tokens"] = tokenList;
        manifest["label"] = tokens.empty() ? Json(nullptr) : Json(cp437ToUtf8(tokens[0]));
        manifest["dimension"] = tokens.size() > 1 ? Json(cp437ToUtf8(tokens[1])) : Json(nullptr);
        manifest["sheet_width"] = bitmap.width;
        manifest["sheet_height"] = bitmap.height;
        manifest["cell_width"] = optionalInt(cellW);
        manifest["cell_height"] = optionalInt(cellH);
        manifest["columns"] = kSheetColumns;
        manifest["rows"] = kSheetRows;
        manifest["slicing_rule"] = "cell k starts at 1 + k * (cell size + 1) on both axes";
        manifest["separators"] = separators;
        Json unused = Json::object();
        unused["right"] = bitmap.width - gridW;
        unused["bottom"] = bitmap.height - gridH;
        manifest["unused_canvas"] = unused;
        manifest["piece_order_verified"] = false;
        manifest["piece_order_note"] =
            "column and color assignments come from the CM.INI [demo] numbering and still"
            " need code evidence";
        manifest["transparent_index"] = 0;
        manifest["sheet_image"] = "sets/" + key.substr(0, key.size() - 1) + "/sheet.png";
        manifest["cells"] = cells;
        writeJsonFile((folder / "manifest.json").string(), manifest, true, false);
        entries.push_back(manifest);
    }
    return entries;
}

Json extractBackgrounds(const fs::path& cdDir, const fs::path& outDir) {
    fs::create_directories(outDir);
    Json entries = Json::array();
    for (const char* filename : kBackgrounds) {
        const fs::path source = cdDir / filename;
        Dib bitmap = readBmpFile(source, nullptr);
        std::string name(filename);
        name = name.substr(0, name.rfind('.')) + ".png";
        const fs::path out = outDir / name;
        writePng(out.string(), bitmap.width, bitmap.height, kPngRgb,
                 dibToRows(bitmap, 0, 0, bitmap.width, bitmap.height, -1), 6);
        Json entry = Json::object();
        entry["source"] = filename;
        entry["source_sha256"] = sha256File(source.string());
        entry["width"] = bitmap.width;
        entry["height"] = bitmap.height;
        entry["bit_count"] = bitmap.bitCount;
        entry["opaque"] = true;
        entry["output"] = "backgrounds/" + name;
        entry["output_sha256"] = sha256File(out.string());
        entries.push_back(entry);
    }
    return entries;
}

// ---------------------------------------------------------------------------
// language string tables
// ---------------------------------------------------------------------------

Json neIdJson(const NeId& id) {
    return id.isNumeric ? Json(id.id) : Json(id.name);
}

Json extractLocales(const fs::path& cdDir, const fs::path& outDir) {
    fs::create_directories(outDir);
    Json files = Json::array();
    for (const auto& language : kLanguages) {
        const std::string filename = language.first;
        const std::string name = language.second;
        std::vector<std::uint8_t> blob = readFileBytes(cdDir / filename);
        std::vector<NeResource> resources = readNeResources(blob);

        Json tables = Json::array();
        std::int64_t stringCount = 0;
        for (const NeResource& resource : resources) {
            if (!resource.type.isNumeric || resource.type.id != kRtString) {
                continue;
            }
            const std::uint8_t* data = blob.data() + resource.offset;
            const std::size_t size = resource.length;
            Json strings = Json::array();
            std::size_t pos = 0;
            const std::int64_t base = (static_cast<std::int64_t>(resource.name.id) - 1) * 16;
            for (int index = 0; index < 16; ++index) {
                Json entry = Json::object();
                entry["index"] = index;
                if (pos >= size) {
                    entry["length"] = nullptr;
                    entry["present"] = false;
                } else {
                    std::size_t length = data[pos];
                    std::size_t stop = std::min(pos + 1 + length, size);
                    const std::uint8_t* body = data + pos + 1;
                    const std::size_t bodySize = stop > pos + 1 ? stop - pos - 1 : 0;
                    pos += 1 + length;
                    entry["length"] = static_cast<std::uint64_t>(length);
                    entry["present"] = true;
                    entry["bytes_hex"] = toHex(body, bodySize);
                    entry["cp437"] = cp437ToUtf8(body, bodySize);
                    entry["latin1"] = latin1ToUtf8(body, bodySize);
                    stringCount += 1;
                }
                entry["string_id"] = base + index;
                strings.push_back(entry);
            }

            std::size_t tailEnd = size;
            while (tailEnd > pos && data[tailEnd - 1] == 0) {
                --tailEnd;
            }
            Json table = Json::object();
            table["resource_id"] = resource.name.id;
            table["resource_offset"] = static_cast<std::uint64_t>(resource.offset);
            table["resource_offset_hex"] = hexOffset(resource.offset);
            table["resource_length"] = static_cast<std::uint64_t>(resource.length);
            table["bytes_consumed"] = static_cast<std::uint64_t>(pos);
            table["trailing_bytes_hex"] =
                pos < size ? toHex(data + pos, tailEnd > pos ? tailEnd - pos : 0) : std::string();
            table["strings"] = strings;
            tables.push_back(table);
        }

        Json others = Json::array();
        for (const NeResource& resource : resources) {
            if (resource.type.isNumeric && resource.type.id == kRtString) {
                continue;
            }
            Json entry = Json::object();
            entry["type"] = neIdJson(resource.type);
            entry["name"] = neIdJson(resource.name);
            entry["offset"] = static_cast<std::uint64_t>(resource.offset);
            entry["length"] = static_cast<std::uint64_t>(resource.length);
            entry["flags"] = resource.flags;
            others.push_back(entry);
        }

        Json document = Json::object();
        document["language"] = name;
        document["source"] = filename;
        document["source_sha256"] = sha256Hex(blob);
        document["encoding_note"] =
            "the bytes are not plain Windows text, the game substitutes glyphs while drawing";
        document["string_table_count"] = static_cast<std::uint64_t>(tables.size());
        document["string_count"] = stringCount;
        document["other_resources"] = others;
        document["string_tables"] = tables;
        writeJsonFile((outDir / (name + ".json")).string(), document, false, false);

        Json summary = Json::object();
        summary["language"] = name;
        summary["source"] = filename;
        summary["string_table_count"] = static_cast<std::uint64_t>(tables.size());
        summary["string_count"] = stringCount;
        summary["output"] = "locales/" + name + ".json";
        files.push_back(summary);
    }
    return files;
}

// ---------------------------------------------------------------------------
// title artwork
// ---------------------------------------------------------------------------

Json extractUi(const fs::path& cdDir, const fs::path& outDir) {
    fs::create_directories(outDir);
    std::vector<std::uint8_t> blob = readFileBytes(cdDir / "TITLERES.DLL");
    std::vector<NeResource> resources = readNeResources(blob);
    Json entries = Json::array();
    for (const NeResource& resource : resources) {
        if (!resource.type.isNumeric || resource.type.id != kRtBitmap) {
            continue;
        }
        Dib bitmap = parseDib(blob, resource.offset, 0, false);
        const std::string name = resource.name.text() + ".png";
        const fs::path out = outDir / name;
        writePng(out.string(), bitmap.width, bitmap.height, kPngRgb,
                 dibToRows(bitmap, 0, 0, bitmap.width, bitmap.height, -1), 6);
        Json entry = Json::object();
        entry["source"] = "TITLERES.DLL";
        entry["resource_name"] = resource.name.text();
        entry["resource_offset"] = static_cast<std::uint64_t>(resource.offset);
        entry["resource_offset_hex"] = hexOffset(resource.offset);
        entry["resource_length"] = static_cast<std::uint64_t>(resource.length);
        entry["width"] = bitmap.width;
        entry["height"] = bitmap.height;
        entry["bit_count"] = bitmap.bitCount;
        entry["output"] = "ui/" + name;
        entry["output_sha256"] = sha256File(out.string());
        entries.push_back(entry);
    }
    Json manifest = Json::object();
    manifest["source"] = "TITLERES.DLL";
    manifest["bitmaps"] = entries;
    writeJsonFile((outDir / "manifest.json").string(), manifest, true, false);
    return entries;
}

// ---------------------------------------------------------------------------
// the source hash list and the three known silent cues
// ---------------------------------------------------------------------------

Json hashSources(const fs::path& cdDir) {
    std::set<std::string> names;
    for (const char* named : kNamedSources) {
        names.insert(named);
    }
    for (const char* suffix : {".ANX", ".INI", ".BMP", ".WAV"}) {
        for (const std::string& name : namesEndingIn(cdDir, suffix)) {
            names.insert(name);
        }
    }
    Json out = Json::object();
    for (const std::string& name : names) {
        const fs::path path = cdDir / name;
        if (!fs::exists(path)) {
            continue;
        }
        std::vector<std::uint8_t> blob = readFileBytes(path);
        Json entry = Json::object();
        entry["sha256"] = sha256Hex(blob);
        entry["bytes"] = static_cast<std::uint64_t>(blob.size());
        out[name] = entry;
    }
    return out;
}

// The three cue problems the plan names. All three play nothing in the
// original, so the catalog records them whether or not the run also finds
// them on its own.
struct KnownCue {
    const char* iniFile;
    const char* section;
    const char* raw;
    const char* note;
};

const KnownCue kKnownCues[] = {
    {"WN.INI", "WNBR_019", "atftstep.awv",
     "ATFTSTEP.AWV is not a WAVE resource, and the player never tries ATFTSTEP.WAV"},
    {"WP.INI", "WPBB_002", "r2alarm\\.wav",
     "R2ALARM\\.WAV keeps the stray backslash, and the player never tries R2ALARM.WAV"},
    {"BB.INI", "BBWQ_007", "LEIA2.WAV", "SWCAUDIO.DLL holds no LEIA2.WAV"},
};

std::string lowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

}  // namespace

const std::map<std::string, std::int64_t>& expectedCounts() {
    static const std::map<std::string, std::int64_t> expected = {
        {"capture_records", 4799}, {"timeline_entries", 5414}, {"poses_shown", 5342},
        {"captures", 72},          {"piece_bitmaps", 1344},    {"wave_records", 110},
        {"distinct_wave_names", 109}, {"bmp_files", 8},        {"locale_files", 4}};
    return expected;
}

std::vector<std::string> requiredCdFiles() {
    std::vector<std::string> out;
    for (const char* name : kNamedSources) {
        out.push_back(name);
    }
    out.push_back("CM.INI");
    for (const char* piece : kPieces) {
        out.push_back(std::string(piece) + ".INI");
    }
    for (const auto& sheet : kSheets) {
        out.push_back(sheet.second);
    }
    for (const char* background : kBackgrounds) {
        out.push_back(background);
    }
    for (const char* wave : kLooseWaves) {
        out.push_back(wave);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> missingCdFiles(const std::string& cdDir) {
    std::vector<std::string> missing;
    for (const std::string& name : requiredCdFiles()) {
        if (!fs::exists(fs::path(cdDir) / name)) {
            missing.push_back(name);
        }
    }
    return missing;
}

ExtractResult runExtract(const ExtractOptions& options) {
    const auto started = std::chrono::system_clock::now();
    const auto startedSteady = std::chrono::steady_clock::now();
    auto say = options.progress ? options.progress
                                : std::function<void(const std::string&)>([](const std::string& line) {
                                      std::cout << line << std::endl;
                                  });

    const fs::path cdDir = fs::absolute(options.cdDir);
    const fs::path outDir = fs::absolute(options.outDir);

    std::vector<std::string> missing = missingCdFiles(cdDir.string());
    if (!missing.empty()) {
        std::string message = "the CD folder is missing " + std::to_string(missing.size()) +
                              " files, starting with " + missing.front();
        throw std::runtime_error(message);
    }

    // Everything goes into a sibling of the requested directory and moves into
    // place at the end, so an interrupted run leaves the old cache alone.
    const std::string stamp = std::to_string(static_cast<long>(::getpid()));
    const fs::path workDir = outDir.parent_path() / (outDir.filename().string() + ".new-" + stamp);
    fs::remove_all(workDir);
    fs::create_directories(workDir);

    IniFile cm((cdDir / "CM.INI").string());
    const IniSection* defaults = cm.section("defaults");
    const int frameDelay = sectionIntOr(defaults, "frame_delay", 120);

    say("hashing source files");
    Json sources = hashSources(cdDir);

    say("extracting audio");
    AudioStage audio = extractAudio(cdDir, workDir / "audio");

    say("extracting captures");
    CaptureStage captures =
        extractCaptures(cdDir, workDir / "captures", audio.index, frameDelay);

    say("extracting piece sprites");
    PieceStage pieces = extractPieces(cdDir, workDir / "pieces");

    say("extracting piece sheets and backgrounds");
    Json sets = extractSets(cdDir, workDir / "sets");
    Json backgrounds = extractBackgrounds(cdDir, workDir / "backgrounds");

    say("extracting language string tables");
    Json locales = extractLocales(cdDir, workDir / "locales");

    say("extracting title artwork");
    Json ui = extractUi(cdDir, workDir / "ui");

    ExtractResult result;
    Json counts = Json::object();
    counts["capture_records"] = captures.distinctRecords;
    counts["timeline_entries"] = captures.timelineEntries;
    counts["poses_shown"] = captures.poses;
    counts["captures"] = captures.captureCount;
    counts["piece_bitmaps"] = pieces.bitmapCount;
    counts["wave_records"] = audio.recordCount;
    counts["distinct_wave_names"] = audio.distinctNames;
    counts["bmp_files"] = static_cast<std::int64_t>(sets.size() + backgrounds.size());
    counts["locale_files"] = static_cast<std::int64_t>(locales.size());

    Json expected = Json::object();
    Json matches = Json::object();
    for (const char* key : {"capture_records", "timeline_entries", "poses_shown", "captures",
                            "piece_bitmaps", "wave_records", "distinct_wave_names", "bmp_files",
                            "locale_files"}) {
        const std::int64_t want = expectedCounts().at(key);
        const std::int64_t got = counts.at(key).get<std::int64_t>();
        expected[key] = want;
        matches[key] = got == want;
        result.counts[key] = got;
        if (got != want) {
            result.mismatched.push_back(key);
        }
    }

    // Start from the three cases the plan names, then fold in whatever this run
    // found. A case that appears in both keeps one entry and gains a flag.
    Json unresolved = Json::array();
    std::map<std::string, std::size_t> index;
    for (const KnownCue& known : kKnownCues) {
        Json entry = Json::object();
        entry["kind"] = "silent_in_original";
        entry["ini_file"] = known.iniFile;
        entry["section"] = known.section;
        entry["raw"] = known.raw;
        entry["resolved"] = nullptr;
        entry["note"] = known.note;
        entry["confirmed_by_extractor"] = false;
        index[std::string(known.iniFile) + "\x1f" + known.section + "\x1f" +
              lowerAscii(trim(known.raw))] = unresolved.size();
        unresolved.push_back(entry);
    }
    for (const Json& found : captures.silent) {
        const std::string key = found.at("ini_file").get<std::string>() + "\x1f" +
                                found.at("section").get<std::string>() + "\x1f" +
                                lowerAscii(trim(found.at("raw").get<std::string>()));
        auto already = index.find(key);
        if (already != index.end()) {
            unresolved[already->second]["confirmed_by_extractor"] = true;
            continue;
        }
        Json entry = Json::object();
        entry["kind"] = "silent_in_original";
        entry["ini_file"] = found.at("ini_file");
        entry["section"] = found.at("section");
        entry["raw"] = found.at("raw");
        entry["resolved"] = found.at("resolved");
        entry["note"] = found.contains("silent_reason") ? found.at("silent_reason") : Json("");
        entry["confirmed_by_extractor"] = true;
        index[key] = unresolved.size();
        unresolved.push_back(entry);
    }

    Json catalog = Json::object();
    catalog["extractor_version"] = kVersion;
    catalog["generated_unix_time"] = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(started.time_since_epoch()).count());
    catalog["cd_directory"] = cdDir.string();
    catalog["output_directory"] = outDir.string();
    catalog["frame_delay_ms"] = frameDelay;
    catalog["transparent_palette_index"] = 0;
    catalog["counts"] = counts;
    catalog["expected_counts"] = expected;
    catalog["counts_match_expected"] = matches;
    catalog["unresolved"] = unresolved;
    catalog["sources"] = sources;
    catalog["canvas"] = canvasJson();
    catalog["captures_with_blocking_sounds"] = captures.blockingCaptures;
    catalog["captures"] = captures.captures;
    catalog["capture_records"] = captures.records;
    catalog["pieces"] = pieces.pieces;
    catalog["piece_bitmaps"] = pieces.bitmaps;
    catalog["piece_count_mismatches"] = pieces.mismatches;
    catalog["sets"] = sets;
    catalog["backgrounds"] = backgrounds;
    catalog["audio"] = audio.summary;
    catalog["locales"] = locales;
    catalog["ui"] = ui;
    writeJsonFile((workDir / "catalog.json").string(), catalog, true, false);

    for (const std::string& line : captures.summaryLines) {
        say(line);
    }

    if (!result.mismatched.empty()) {
        say("the counts do not match, so the new cache stays in " + workDir.string());
        result.ok = false;
        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                       startedSteady)
                             .count();
        return result;
    }

    // Move the finished tree into place. The old one steps aside first,
    // because a rename onto an existing directory fails.
    const fs::path retired = outDir.parent_path() / (outDir.filename().string() + ".old-" + stamp);
    if (fs::exists(outDir)) {
        fs::rename(outDir, retired);
    }
    fs::rename(workDir, outDir);
    fs::remove_all(retired);

    result.ok = true;
    result.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedSteady).count();
    say("wrote " + outDir.string());
    return result;
}

}  // namespace swchess::exporter
