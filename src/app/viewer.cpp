// swchess-viewer plays one capture animation over one of the CD backgrounds.
// It opens an SDL3 window backed by the default macOS renderer, which is
// Metal, and drives the animation from src/anim.
//
//   swchess-viewer --cd original/win3x/cd --capture BBWB [--bg SPACE256]
//   swchess-viewer --cd original/win3x/cd --capture BBWB --dump-frame 39 out.ppm
//   swchess-viewer --cd original/win3x/cd --dump-timeline BBWB
//   swchess-viewer --cd original/win3x/cd --dump-board WHTBTM out.ppm [--fen "..."]
//   swchess-viewer --cd original/win3x/cd --hit 108 130
//   swchess-viewer --cd original/win3x/cd --assets assets --review BBWB
//
// The --dump-frame, --dump-timeline, --dump-board and --hit forms never open a
// window, so they run on a machine with no display.

#include <SDL3/SDL.h>
// Windows starts a GUI program at WinMain. This header renames our main so
// that SDL supplies the one Windows looks for.
#include <SDL3/SDL_main.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "anim/capture.h"
#include "anim/player.h"
#include "app/review.h"
#include "assets/anx.h"
#include "assets/bmp.h"
#include "assets/cdfs.h"
#include "assets/wav.h"
#include "audio/audio.h"
#include "board/board_view.h"
#include "board/geometry.h"
#include "board/placement.h"
#include "chess.h"
#include "render/compositor.h"

namespace {

// The eight backgrounds the number keys select, in key order.
const char* const kBackgrounds[] = {
    "SPACE256", "THRON256", "2DBDBTOP", "2DBDWTOP",
    "FACING_P", "2DSET_P",  "WHTBTM_P", "WHTTOP_P",
};
constexpr int kBackgroundCount = 8;

constexpr int kLogicalWidth = swchess::anim::kCanvasWidth;
constexpr int kLogicalHeight = swchess::anim::kCanvasHeight;

struct Options {
    std::string cdDir;
    std::string capture;
    std::string background = "SPACE256";
    int dumpFrame = -1;
    std::string dumpPath;
    std::string dumpTimeline;
    std::string dumpBoardSet;
    std::string dumpBoardPath;
    std::string fen;
    bool hit = false;
    int hitX = 0;
    int hitY = 0;
    std::string assetsDir = "assets";
    std::string review;
    std::int64_t reviewDumpMs = -1;
    std::string reviewDumpPath;
};

void printUsage() {
    std::fprintf(stderr,
                 "usage: swchess-viewer --cd <dir> --capture <NAME> [--bg <NAME>]\n"
                 "       swchess-viewer --cd <dir> --capture <NAME> --dump-frame <N> <out.ppm>\n"
                 "       swchess-viewer --cd <dir> --dump-timeline <NAME>\n"
                 "       swchess-viewer --cd <dir> --dump-board <SET> <out.ppm> [--fen <FEN>]\n"
                 "       swchess-viewer --cd <dir> --hit <x> <y> [--set <SET>]\n"
                 "       swchess-viewer --cd <dir> --assets <dir> --review <NAME>\n"
                 "       swchess-viewer --cd <dir> --assets <dir> --review <NAME> "
                 "--dump-at <MS> <out.ppm>\n"
                 "       a set is WHTBTM, WHTTOP, FACING or 2D\n");
}

bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--cd") {
            const char* value = next("--cd");
            if (value == nullptr) return false;
            options.cdDir = value;
        } else if (arg == "--capture") {
            const char* value = next("--capture");
            if (value == nullptr) return false;
            options.capture = value;
        } else if (arg == "--bg") {
            const char* value = next("--bg");
            if (value == nullptr) return false;
            options.background = value;
        } else if (arg == "--dump-frame") {
            const char* number = next("--dump-frame");
            if (number == nullptr) return false;
            options.dumpFrame = std::atoi(number);
            const char* path = next("--dump-frame output path");
            if (path == nullptr) return false;
            options.dumpPath = path;
        } else if (arg == "--dump-timeline") {
            const char* value = next("--dump-timeline");
            if (value == nullptr) return false;
            options.dumpTimeline = value;
        } else if (arg == "--dump-board") {
            const char* set = next("--dump-board");
            if (set == nullptr) return false;
            options.dumpBoardSet = set;
            const char* path = next("--dump-board output path");
            if (path == nullptr) return false;
            options.dumpBoardPath = path;
        } else if (arg == "--set") {
            const char* value = next("--set");
            if (value == nullptr) return false;
            options.dumpBoardSet = value;
        } else if (arg == "--fen") {
            const char* value = next("--fen");
            if (value == nullptr) return false;
            options.fen = value;
        } else if (arg == "--hit") {
            const char* x = next("--hit x");
            if (x == nullptr) return false;
            const char* y = next("--hit y");
            if (y == nullptr) return false;
            options.hit = true;
            options.hitX = std::atoi(x);
            options.hitY = std::atoi(y);
        } else if (arg == "--assets") {
            const char* value = next("--assets");
            if (value == nullptr) return false;
            options.assetsDir = value;
        } else if (arg == "--review") {
            const char* value = next("--review");
            if (value == nullptr) return false;
            options.review = value;
        } else if (arg == "--dump-at") {
            const char* number = next("--dump-at");
            if (number == nullptr) return false;
            options.reviewDumpMs = std::atoll(number);
            const char* path = next("--dump-at output path");
            if (path == nullptr) return false;
            options.reviewDumpPath = path;
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return false;
        } else {
            std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
            return false;
        }
    }
    if (options.cdDir.empty()) {
        printUsage();
        return false;
    }
    if (options.capture.empty() && options.dumpTimeline.empty() &&
        options.dumpBoardPath.empty() && !options.hit && options.review.empty()) {
        printUsage();
        return false;
    }
    return true;
}

std::string backgroundPath(const std::string& cdDir, const std::string& name) {
    return swchess::resolveCdFile(cdDir, name + ".BMP").string();
}

const swchess::anim::CapturePose* findPose(const swchess::anim::CaptureTimeline& timeline,
                                           int index) {
    for (const swchess::anim::CapturePose& pose : timeline.poses) {
        if (static_cast<int>(pose.index) == index) {
            return &pose;
        }
    }
    return nullptr;
}

// Composites pose `index` at its resolved position over the background.
int dumpFrame(const Options& options) {
    swchess::anim::CaptureTimeline timeline =
        swchess::anim::loadCapture(options.cdDir, options.capture);
    const swchess::anim::CapturePose* pose = findPose(timeline, options.dumpFrame);
    if (pose == nullptr) {
        std::fprintf(stderr, "%s has no shown pose %d, it shows %zu of %zu entries\n",
                     options.capture.c_str(), options.dumpFrame, timeline.poses.size(),
                     timeline.entryCount);
        return 1;
    }
    swchess::Image canvas = swchess::loadBmp(backgroundPath(options.cdDir, options.background));
    std::vector<std::uint8_t> rgba = swchess::anxToRGBA(*pose->record);
    swchess::blitRGBA(canvas, rgba.data(), pose->width, pose->height, pose->x, pose->y);
    swchess::writePPM(canvas, options.dumpPath);
    std::printf("wrote %s from %s pose %zu, record 0x%x, %dx%d at %d,%d, t %lld ms, over %s\n",
                options.dumpPath.c_str(), options.capture.c_str(), pose->index, pose->recordOffset,
                pose->width, pose->height, pose->x, pose->y,
                static_cast<long long>(pose->startMs), options.background.c_str());
    return 0;
}

// Prints one line per shown pose plus a summary of the whole capture.
int dumpTimeline(const Options& options) {
    swchess::anim::CaptureTimeline timeline =
        swchess::anim::loadCapture(options.cdDir, options.dumpTimeline);
    // `sound` is the WAVE resource the cue resolved to, and `-` when the INI
    // named nothing the sound library holds. `sound_t_ms` is when the original
    // calls sndPlaySound, which a sync cue does before its pose appears.
    std::printf("index t_ms x y w h sound mode sound_t_ms\n");
    for (const swchess::anim::CapturePose& pose : timeline.poses) {
        const bool sounding = pose.hasSound && pose.sound.resolved;
        const char* cue = sounding ? pose.sound.resource.c_str() : "-";
        const char* mode = sounding ? swchess::anim::soundModeName(pose.sound.mode) : "-";
        char startMs[32] = "-";
        if (sounding) {
            std::snprintf(startMs, sizeof(startMs), "%lld",
                          static_cast<long long>(pose.sound.startMs));
        }
        std::printf("%zu %lld %d %d %d %d %s %s %s\n", pose.index,
                    static_cast<long long>(pose.startMs), pose.x, pose.y, pose.width, pose.height,
                    cue, mode, startMs);
    }
    std::printf("# %s entries %zu poses %zu delay %lld hold %lld offset %d,%d hold_end %lld end %lld\n",
                timeline.name.c_str(), timeline.entryCount, timeline.poses.size(),
                static_cast<long long>(timeline.frameDelayMs),
                static_cast<long long>(timeline.holdMs), timeline.offsetX, timeline.offsetY,
                static_cast<long long>(timeline.holdEndMs), static_cast<long long>(timeline.endMs));
    if (timeline.hasEndSound) {
        std::printf("# end sound %s %s t %lld\n", timeline.endSound.cue.c_str(),
                    timeline.endSound.resolved ? "resolved" : "silent",
                    static_cast<long long>(timeline.endSound.startMs));
    }
    return 0;
}

// Reads the position the board forms use: the start position, or the FEN the
// caller passed.
bool positionFor(const Options& options, swchess::chess::Position& position) {
    if (options.fen.empty()) {
        position = swchess::chess::Position::start();
        return true;
    }
    std::optional<swchess::chess::Position> parsed =
        swchess::chess::Position::fromFen(options.fen);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "cannot read the FEN %s\n", options.fen.c_str());
        return false;
    }
    position = *parsed;
    return true;
}

// Draws one position with one set over its own background.
int dumpBoard(const Options& options) {
    std::optional<swchess::board::SetId> set =
        swchess::board::parseSetName(options.dumpBoardSet);
    if (!set.has_value()) {
        std::fprintf(stderr, "unknown set %s, use WHTBTM, WHTTOP, FACING or 2D\n",
                     options.dumpBoardSet.c_str());
        return 1;
    }
    swchess::chess::Position position;
    if (!positionFor(options, position)) {
        return 1;
    }
    swchess::board::BoardScene scene = swchess::board::loadBoardScene(options.cdDir, *set);
    std::vector<swchess::board::PieceSprite> sprites =
        swchess::board::drawOrder(position, scene);
    swchess::Image canvas;
    swchess::board::renderPosition(position, scene, canvas);
    swchess::writePPM(canvas, options.dumpBoardPath);
    std::printf("wrote %s, set %s over %s, %zu pieces, cells %dx%d, board center %d,%d\n",
                options.dumpBoardPath.c_str(), swchess::board::setKey(*set),
                scene.backgroundSource.c_str(), sprites.size(), scene.sheet.cellWidth,
                scene.sheet.cellHeight, scene.geometry.center().x, scene.geometry.center().y);
    return 0;
}

// Prints the square under one pixel.
int printHit(const Options& options) {
    swchess::board::SetId set = swchess::board::SetId::WhiteBottom;
    if (!options.dumpBoardSet.empty()) {
        std::optional<swchess::board::SetId> parsed =
            swchess::board::parseSetName(options.dumpBoardSet);
        if (!parsed.has_value()) {
            std::fprintf(stderr, "unknown set %s\n", options.dumpBoardSet.c_str());
            return 1;
        }
        set = *parsed;
    }
    swchess::board::BoardSettings settings =
        swchess::board::loadBoardSettingsFromCd(options.cdDir);
    swchess::board::BoardGeometry geometry = swchess::board::geometryFor(set, settings);
    std::optional<swchess::chess::Square> square =
        swchess::board::hitTest(geometry, options.hitX, options.hitY);
    if (!square.has_value()) {
        std::printf("%d,%d is off the board\n", options.hitX, options.hitY);
        return 0;
    }
    std::printf("%d,%d is %s\n", options.hitX, options.hitY,
                swchess::chess::squareName(*square).c_str());
    return 0;
}

// Every WAVE resource in SWCAUDIO.DLL, ready for the mixer.
std::map<std::string, swchess::audio::Clip> loadClips(const std::string& cdDir) {
    std::map<std::string, swchess::audio::Clip> clips;
    for (const swchess::WaveResource& resource : swchess::loadAudioDll(cdDir)) {
        swchess::audio::Clip clip;
        clip.spec.channels = resource.sound.channels > 0 ? resource.sound.channels : 1;
        clip.spec.freq = resource.sound.sampleRate > 0
                             ? static_cast<int>(resource.sound.sampleRate)
                             : 22050;
        clip.spec.format = resource.sound.bitsPerSample == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
        clip.pcm.resize(resource.sound.samples.size());
        std::memcpy(clip.pcm.data(), resource.sound.samples.data(), resource.sound.samples.size());
        clips.emplace(resource.name, std::move(clip));
    }
    return clips;
}

SDL_Texture* makeTexture(SDL_Renderer* renderer, const std::uint8_t* rgba, int width, int height) {
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture == nullptr) {
        return nullptr;
    }
    SDL_UpdateTexture(texture, nullptr, rgba, width * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return texture;
}

int runViewer(const Options& options) {
    swchess::anim::CaptureTimeline timeline =
        swchess::anim::loadCapture(options.cdDir, options.capture);
    if (timeline.poses.empty()) {
        std::fprintf(stderr, "%s shows no poses\n", options.capture.c_str());
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("swchess-viewer", kLogicalWidth, kLogicalHeight,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, kLogicalWidth, kLogicalHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // Upload every distinct record once, then point each pose at its texture.
    std::map<std::uint32_t, SDL_Texture*> textures;
    for (const auto& [offset, record] : timeline.anx.records) {
        std::vector<std::uint8_t> rgba = swchess::anxToRGBA(record);
        SDL_Texture* texture = makeTexture(renderer, rgba.data(), record.width, record.height);
        if (texture == nullptr) {
            std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            return 1;
        }
        textures[offset] = texture;
    }

    // The mixer falls back to SDL's dummy driver when no device opens, so this
    // path works over SSH as well as on a machine with speakers.
    swchess::audio::Mixer mixer;
    std::map<std::string, swchess::audio::Clip> clips = loadClips(options.cdDir);
    std::vector<swchess::audio::Cue> cues;
    for (const swchess::anim::CapturePose& pose : timeline.poses) {
        if (!pose.hasSound || !pose.sound.resolved) {
            continue;
        }
        auto found = clips.find(pose.sound.resource);
        if (found == clips.end()) {
            continue;
        }
        cues.push_back(swchess::audio::Cue{pose.sound.startMs, &found->second,
                                           swchess::audio::Channel::Effects, 1.0f, false});
    }
    if (timeline.hasEndSound && timeline.endSound.resolved) {
        auto found = clips.find(timeline.endSound.resource);
        if (found != clips.end()) {
            cues.push_back(swchess::audio::Cue{timeline.endSound.startMs, &found->second,
                                               swchess::audio::Channel::Effects, 1.0f, false});
        }
    }
    swchess::audio::CueScheduler scheduler(&mixer, std::move(cues));

    std::string backgroundName = options.background;
    swchess::Image background = swchess::loadBmp(backgroundPath(options.cdDir, backgroundName));
    SDL_Texture* backgroundTexture =
        makeTexture(renderer, background.rgba.data(), background.width, background.height);

    swchess::anim::CapturePlayer player;

    using Clock = std::chrono::steady_clock;
    Clock::time_point origin = Clock::now();
    auto wallMs = [&]() -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - origin).count();
    };

    // The animation clock. It stands still while paused and jumps when the
    // viewer steps a pose, and the player and the cue scheduler both read it.
    std::int64_t animationMs = 0;
    std::int64_t anchorWallMs = wallMs();
    bool playing = true;
    player.start(&timeline, 0);

    auto seekTo = [&](std::int64_t ms) {
        if (ms < 0) ms = 0;
        animationMs = ms;
        anchorWallMs = wallMs();
        player.start(&timeline, 0);
        scheduler.reset();
        scheduler.skipTo(ms);
        mixer.stopAll();
    };

    bool running = true;
    swchess::anim::DrawState drawn;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode key = event.key.key;
                if (key == SDLK_ESCAPE) {
                    running = false;
                } else if (key == SDLK_SPACE) {
                    playing = !playing;
                    anchorWallMs = wallMs();
                } else if (key == SDLK_RIGHT || key == SDLK_LEFT) {
                    // Step to the next or previous pose start time.
                    playing = false;
                    std::size_t at = 0;
                    for (std::size_t i = 0; i < timeline.poses.size(); ++i) {
                        if (timeline.poses[i].startMs <= animationMs) {
                            at = i;
                        }
                    }
                    if (key == SDLK_RIGHT && at + 1 < timeline.poses.size()) {
                        ++at;
                    } else if (key == SDLK_LEFT && at > 0) {
                        --at;
                    }
                    seekTo(timeline.poses[at].startMs);
                } else if (key == SDLK_R) {
                    seekTo(0);
                    playing = true;
                } else if (key == SDLK_S) {
                    // Skip the capture. The player reports the cues that never
                    // played and the scheduler drops them too.
                    std::vector<swchess::anim::SoundEvent> cancelled = player.skip();
                    std::printf("skipped %s, %zu cues cancelled\n", timeline.name.c_str(),
                                cancelled.size());
                    animationMs = timeline.endMs;
                    anchorWallMs = wallMs();
                    scheduler.skipTo(timeline.endMs);
                    mixer.stopAll();
                    playing = false;
                } else if (key >= SDLK_1 && key <= SDLK_9) {
                    int pick = static_cast<int>(key - SDLK_1);
                    if (pick < kBackgroundCount) {
                        backgroundName = kBackgrounds[pick];
                        try {
                            background =
                                swchess::loadBmp(backgroundPath(options.cdDir, backgroundName));
                            SDL_DestroyTexture(backgroundTexture);
                            backgroundTexture = makeTexture(renderer, background.rgba.data(),
                                                            background.width, background.height);
                        } catch (const std::exception& error) {
                            std::fprintf(stderr, "%s\n", error.what());
                        }
                    }
                }
            }
        }

        std::int64_t now = wallMs();
        if (playing) {
            animationMs += now - anchorWallMs;
        }
        anchorWallMs = now;

        swchess::anim::PlayerUpdate update = player.advance(animationMs);
        scheduler.advance(animationMs);
        if (update.draw.visible || update.finished) {
            drawn = update.draw;
        }
        if (update.finished) {
            playing = false;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (backgroundTexture != nullptr) {
            SDL_FRect whole{0.0f, 0.0f, static_cast<float>(background.width),
                            static_cast<float>(background.height)};
            SDL_RenderTexture(renderer, backgroundTexture, nullptr, &whole);
        }
        if (drawn.visible && drawn.record != nullptr) {
            SDL_FRect target{static_cast<float>(drawn.x), static_cast<float>(drawn.y),
                             static_cast<float>(drawn.width), static_cast<float>(drawn.height)};
            SDL_RenderTexture(renderer, textures[drawn.record->offset], nullptr, &target);
        }
        SDL_RenderPresent(renderer);

        char title[256];
        std::snprintf(title, sizeof(title),
                      "swchess-viewer  %s over %s  pose %zu/%zu  %lld/%lld ms  %s  audio %s",
                      options.capture.c_str(), backgroundName.c_str(), drawn.poseIndex,
                      timeline.poses.size(), static_cast<long long>(animationMs),
                      static_cast<long long>(timeline.endMs), playing ? "playing" : "paused",
                      mixer.driverName().c_str());
        SDL_SetWindowTitle(window, title);
    }

    for (auto& [offset, texture] : textures) {
        (void)offset;
        SDL_DestroyTexture(texture);
    }
    if (backgroundTexture != nullptr) {
        SDL_DestroyTexture(backgroundTexture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        return 2;
    }
    try {
        if (!options.review.empty()) {
            swchess::app::ReviewOptions review;
            review.cdDir = options.cdDir;
            review.assetsDir = options.assetsDir;
            review.capture = options.review;
            review.dumpAtMs = options.reviewDumpMs;
            review.dumpPath = options.reviewDumpPath;
            if (review.dumpAtMs >= 0) {
                return swchess::app::dumpReview(review);
            }
            return swchess::app::runReview(review);
        }
        if (!options.dumpTimeline.empty()) {
            return dumpTimeline(options);
        }
        if (options.dumpFrame >= 0) {
            return dumpFrame(options);
        }
        if (!options.dumpBoardPath.empty()) {
            return dumpBoard(options);
        }
        if (options.hit) {
            return printHit(options);
        }
        return runViewer(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
