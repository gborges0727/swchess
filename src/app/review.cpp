#include "app/review.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim/capture.h"
#include "anim/interp.h"
#include "anim/player.h"
#include "assets/anx.h"
#include "assets/bmp.h"
#include "assets/wav.h"
#include "audio/audio.h"
#include "render/compositor.h"
#include "text/font.h"

namespace swchess::app {
namespace {

// One half of the window holds the whole 640 by 480 game canvas, and the two
// halves sit next to each other. The status strip runs along the bottom.
constexpr int kHalfWidth = anim::kCanvasWidth;
constexpr int kHalfHeight = anim::kCanvasHeight;
constexpr int kStripHeight = 40;
constexpr int kWindowWidth = kHalfWidth * 2;
constexpr int kWindowHeight = kHalfHeight + kStripHeight;

// One 60 frames per second sample, in milliseconds.
constexpr double kSampleMs = 1000.0 / 60.0;

// The eight backgrounds the number keys select, in key order.
const char* const kBackgrounds[] = {
    "SPACE256", "THRON256", "2DBDBTOP", "2DBDWTOP",
    "FACING_P", "2DSET_P",  "WHTBTM_P", "WHTTOP_P",
};
constexpr int kBackgroundCount = 8;

// What sits behind the sprite. Section 3 of the plan asks a reviewer to check
// the edges over black, white, a checkerboard and the real artwork.
enum class Backdrop {
    Black,
    White,
    Checker,
    Real,
};

const char* backdropName(Backdrop backdrop) {
    switch (backdrop) {
        case Backdrop::Black:
            return "black";
        case Backdrop::White:
            return "white";
        case Backdrop::Checker:
            return "checker";
        case Backdrop::Real:
            break;
    }
    return "background";
}

Backdrop nextBackdrop(Backdrop backdrop) {
    switch (backdrop) {
        case Backdrop::Black:
            return Backdrop::White;
        case Backdrop::White:
            return Backdrop::Checker;
        case Backdrop::Checker:
            return Backdrop::Real;
        case Backdrop::Real:
            break;
    }
    return Backdrop::Black;
}

// The playback speeds the bracket keys walk through.
const double kSpeeds[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
constexpr int kSpeedCount = 6;
constexpr int kNormalSpeed = 3;

std::string backgroundPath(const std::string& cdDir, const std::string& name) {
    return cdDir + "/" + name + ".BMP";
}

// Everything the review reads off the disk once.
struct Scene {
    anim::CaptureTimeline timeline;
    anim::InterpSequence interp;
    Image background;
    std::string backgroundName = "SPACE256";
    text::BitmapFont font;
};

// Reads the capture, its 60 frames per second sequence, one background and the
// GUITEXT font. Throws when the interpolated sequence is missing, because
// without it there is nothing to review.
Scene loadScene(const ReviewOptions& options) {
    Scene scene;
    scene.timeline = anim::loadCapture(options.cdDir, options.capture);
    if (scene.timeline.poses.empty()) {
        throw std::runtime_error(options.capture + " shows no poses");
    }
    std::optional<anim::InterpSequence> interp =
        anim::loadInterp(options.assetsDir, options.capture);
    if (!interp.has_value()) {
        throw std::runtime_error(options.assetsDir + "/captures/" + options.capture +
                                 "/interp60 holds no manifest.json, so there is nothing to"
                                 " compare against");
    }
    scene.interp = std::move(*interp);
    scene.background = loadBmp(backgroundPath(options.cdDir, scene.backgroundName));
    scene.font = text::loadGuiFont(options.cdDir);
    return scene;
}

// Where a frame sits in the sequence, counting from zero. The player hands
// back a pointer into `frames`, so subtracting the first one names it.
std::size_t frameIndex(const anim::InterpSequence& interp, const anim::InterpFrame* frame) {
    if (frame == nullptr || interp.frames.empty()) {
        return 0;
    }
    return static_cast<std::size_t>(frame - interp.frames.data());
}

// The pose that stands at `ms`, counting from one. Zero means the capture has
// not put anything on the canvas yet.
std::size_t poseIndexAt(const anim::CaptureTimeline& timeline, double ms) {
    std::size_t index = 0;
    for (const anim::CapturePose& pose : timeline.poses) {
        if (static_cast<double>(pose.startMs) <= ms) {
            index = pose.index;
        }
    }
    return index;
}

// The two status lines. The first names the capture and the clock, the second
// describes each side.
struct Status {
    std::string top;
    std::string bottom;
};

std::string msText(double ms) {
    char text[32];
    std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(std::llround(ms)));
    return text;
}

Status statusFor(const Scene& scene, double animMs, double speed, bool playing,
                 bool leftIsInterp, const anim::DrawState& left, const anim::DrawState& right,
                 Backdrop backdrop) {
    const anim::DrawState& interpSide = leftIsInterp ? left : right;
    const anim::InterpFrame* frame = interpSide.frame;
    char top[256];
    std::snprintf(top, sizeof(top), "%s  t %s of %s ms  speed %.2fx  %s  bg %s %s",
                  scene.timeline.name.c_str(), msText(animMs).c_str(),
                  msText(static_cast<double>(scene.timeline.endMs)).c_str(), speed,
                  playing ? "playing" : "paused", scene.backgroundName.c_str(),
                  backdropName(backdrop));

    const std::size_t pose = poseIndexAt(scene.timeline, animMs);
    const std::size_t index = frameIndex(scene.interp, frame);
    const char* kind = frame != nullptr ? anim::interpKindName(frame->kind) : "none";
    char bottom[256];
    std::snprintf(bottom, sizeof(bottom),
                  "left %s  right %s  pose %zu of %zu  frame %zu of %zu %s",
                  leftIsInterp ? "interpolated60" : "original120ms",
                  leftIsInterp ? "original120ms" : "interpolated60", pose,
                  scene.timeline.poses.size(), index, scene.interp.frames.size(), kind);
    return Status{top, bottom};
}

// Fills one half of the canvas with the chosen backdrop.
void drawBackdrop(Image& canvas, int xOffset, Backdrop backdrop, const Image& background) {
    for (int y = 0; y < kHalfHeight; ++y) {
        for (int x = 0; x < kHalfWidth; ++x) {
            std::uint8_t red = 0;
            std::uint8_t green = 0;
            std::uint8_t blue = 0;
            if (backdrop == Backdrop::White) {
                red = green = blue = 255;
            } else if (backdrop == Backdrop::Checker) {
                const bool light = ((x / 16) + (y / 16)) % 2 == 0;
                red = green = blue = light ? 200 : 90;
            } else if (backdrop == Backdrop::Real && x < background.width &&
                       y < background.height) {
                const std::uint8_t* source =
                    &background.rgba[(static_cast<std::size_t>(y) * background.width + x) * 4];
                red = source[0];
                green = source[1];
                blue = source[2];
            }
            std::uint8_t* dest =
                &canvas.rgba[(static_cast<std::size_t>(y) * canvas.width + xOffset + x) * 4];
            dest[0] = red;
            dest[1] = green;
            dest[2] = blue;
            dest[3] = 255;
        }
    }
}

// Draws one rendered line in one colour. The font leaves its background
// transparent, so only the ink lands on the canvas.
void blitText(Image& canvas, const text::TextImage& text, int x, int y, std::uint8_t red,
              std::uint8_t green, std::uint8_t blue) {
    for (int row = 0; row < text.height; ++row) {
        const int destY = y + row;
        if (destY < 0 || destY >= canvas.height) {
            continue;
        }
        for (int col = 0; col < text.width; ++col) {
            const int destX = x + col;
            if (destX < 0 || destX >= canvas.width) {
                continue;
            }
            if (text.rgba[(static_cast<std::size_t>(row) * text.width + col) * 4 + 3] == 0) {
                continue;
            }
            std::uint8_t* dest =
                &canvas.rgba[(static_cast<std::size_t>(destY) * canvas.width + destX) * 4];
            dest[0] = red;
            dest[1] = green;
            dest[2] = blue;
            dest[3] = 255;
        }
    }
}

// Paints the status strip along the bottom of the canvas.
void drawStatus(Image& canvas, const text::BitmapFont& font, const Status& status) {
    for (int y = kHalfHeight; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            std::uint8_t* dest =
                &canvas.rgba[(static_cast<std::size_t>(y) * canvas.width + x) * 4];
            dest[0] = 16;
            dest[1] = 16;
            dest[2] = 20;
            dest[3] = 255;
        }
    }
    const text::TextImage top = text::render(font, status.top);
    const text::TextImage bottom = text::render(font, status.bottom);
    blitText(canvas, top, 6, kHalfHeight + 2, 255, 232, 96);
    blitText(canvas, bottom, 6, kHalfHeight + 2 + font.cellHeight + 3, 220, 220, 230);
}

// Puts one side's picture on the software canvas.
void drawSprite(Image& canvas, int xOffset, const anim::DrawState& draw) {
    if (!draw.visible) {
        return;
    }
    if (draw.record != nullptr) {
        std::vector<std::uint8_t> rgba = anxToRGBA(*draw.record);
        blitRGBA(canvas, rgba.data(), draw.width, draw.height, xOffset + draw.x, draw.y);
        return;
    }
    if (draw.frame != nullptr) {
        blitRGBA(canvas, draw.frame->image.pixels.data(), draw.frame->image.width,
                 draw.frame->image.height, xOffset + draw.x, draw.y);
    }
}

// Builds the whole side-by-side canvas on the CPU. The window and the PPM dump
// place the sprites the same way, so a dump shows what a reviewer would see.
Image composeCanvas(const Scene& scene, const anim::DrawState& left,
                    const anim::DrawState& right, Backdrop backdrop, const Status& status) {
    Image canvas;
    canvas.width = kWindowWidth;
    canvas.height = kWindowHeight;
    canvas.rgba.assign(static_cast<std::size_t>(canvas.width) * canvas.height * 4, 255);
    drawBackdrop(canvas, 0, backdrop, scene.background);
    drawBackdrop(canvas, kHalfWidth, backdrop, scene.background);
    drawSprite(canvas, 0, left);
    drawSprite(canvas, kHalfWidth, right);
    drawStatus(canvas, scene.font, status);
    return canvas;
}

// Every WAVE resource in SWCAUDIO.DLL, ready for the mixer.
std::map<std::string, audio::Clip> loadClips(const std::string& cdDir) {
    std::map<std::string, audio::Clip> clips;
    for (const WaveResource& resource : loadAudioDll(cdDir)) {
        audio::Clip clip;
        clip.spec.channels = resource.sound.channels > 0 ? resource.sound.channels : 1;
        clip.spec.freq =
            resource.sound.sampleRate > 0 ? static_cast<int>(resource.sound.sampleRate) : 22050;
        clip.spec.format = resource.sound.bitsPerSample == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
        clip.pcm.resize(resource.sound.samples.size());
        std::memcpy(clip.pcm.data(), resource.sound.samples.data(),
                    resource.sound.samples.size());
        clips.emplace(resource.name, std::move(clip));
    }
    return clips;
}

SDL_Texture* makeTexture(SDL_Renderer* renderer, const std::uint8_t* rgba, int width,
                         int height) {
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

// Drives both players off one animation clock.
//
// The two players read the same timeline and the same interpolated sequence
// and start at the same time, so the only difference between the halves is
// which pictures each one picks.
class ReviewClock {
public:
    ReviewClock(const anim::CaptureTimeline* timeline, const anim::InterpSequence* interp)
        : timeline_(timeline), interp_(interp) {
        restart();
    }

    void setLeftIsInterp(bool leftIsInterp) {
        leftIsInterp_ = leftIsInterp;
        applyCadence();
    }
    bool leftIsInterp() const { return leftIsInterp_; }

    double animMs() const { return animMs_; }
    const anim::DrawState& left() const { return left_; }
    const anim::DrawState& right() const { return right_; }

    // Moves the clock and returns the cues the left player owes. Seeking
    // rebuilds both players and reports nothing, so a step never replays a
    // sound the reviewer already heard.
    std::vector<anim::SoundEvent> advanceTo(double ms) {
        if (ms < animMs_) {
            seek(ms);
            return {};
        }
        animMs_ = ms;
        const auto whole = static_cast<std::int64_t>(std::llround(animMs_));
        anim::PlayerUpdate leftUpdate = leftPlayer_.advance(whole);
        anim::PlayerUpdate rightUpdate = rightPlayer_.advance(whole);
        left_ = leftUpdate.draw;
        right_ = rightUpdate.draw;
        finished_ = leftUpdate.finished && rightUpdate.finished;
        return leftUpdate.sounds;
    }

    void seek(double ms) {
        animMs_ = std::max(0.0, ms);
        restart();
        const auto whole = static_cast<std::int64_t>(std::llround(animMs_));
        anim::PlayerUpdate leftUpdate = leftPlayer_.advance(whole);
        anim::PlayerUpdate rightUpdate = rightPlayer_.advance(whole);
        left_ = leftUpdate.draw;
        right_ = rightUpdate.draw;
        finished_ = leftUpdate.finished && rightUpdate.finished;
    }

    // Ends the capture now and reports the cues that never fired.
    std::vector<anim::SoundEvent> skip() {
        std::vector<anim::SoundEvent> cancelled = leftPlayer_.skip();
        rightPlayer_.skip();
        animMs_ = static_cast<double>(timeline_->endMs);
        anim::PlayerUpdate leftUpdate = leftPlayer_.advance(timeline_->endMs);
        anim::PlayerUpdate rightUpdate = rightPlayer_.advance(timeline_->endMs);
        left_ = leftUpdate.draw;
        right_ = rightUpdate.draw;
        finished_ = true;
        return cancelled;
    }

    bool finished() const { return finished_; }

private:
    void restart() {
        leftPlayer_.start(timeline_, interp_, 0);
        rightPlayer_.start(timeline_, interp_, 0);
        applyCadence();
        finished_ = false;
    }

    void applyCadence() {
        leftPlayer_.setCadence(leftIsInterp_ ? anim::Cadence::Interpolated60
                                             : anim::Cadence::Original120ms);
        rightPlayer_.setCadence(leftIsInterp_ ? anim::Cadence::Original120ms
                                              : anim::Cadence::Interpolated60);
    }

    const anim::CaptureTimeline* timeline_ = nullptr;
    const anim::InterpSequence* interp_ = nullptr;
    anim::CapturePlayer leftPlayer_;
    anim::CapturePlayer rightPlayer_;
    anim::DrawState left_;
    anim::DrawState right_;
    double animMs_ = 0.0;
    bool leftIsInterp_ = false;
    bool finished_ = false;
};

// The 60 frames per second sample before or after `ms`.
double stepSample(double ms, int direction) {
    const double sample = std::floor(ms / kSampleMs + 1e-6);
    return std::max(0.0, (sample + direction) * kSampleMs);
}

// The start time of the authored pose before or after `ms`.
double stepPose(const anim::CaptureTimeline& timeline, double ms, int direction) {
    std::size_t at = 0;
    for (std::size_t i = 0; i < timeline.poses.size(); ++i) {
        if (static_cast<double>(timeline.poses[i].startMs) <= ms) {
            at = i;
        }
    }
    if (direction > 0 && at + 1 < timeline.poses.size()) {
        ++at;
    } else if (direction < 0 && at > 0) {
        --at;
    }
    return static_cast<double>(timeline.poses[at].startMs);
}

}  // namespace

int dumpReview(const ReviewOptions& options) {
    Scene scene = loadScene(options);
    ReviewClock clock(&scene.timeline, &scene.interp);
    clock.seek(static_cast<double>(options.dumpAtMs));

    const Status status = statusFor(scene, clock.animMs(), 1.0, false, clock.leftIsInterp(),
                                    clock.left(), clock.right(), Backdrop::Real);
    Image canvas = composeCanvas(scene, clock.left(), clock.right(), Backdrop::Real, status);
    writePPM(canvas, options.dumpPath);

    const anim::InterpFrame* frame = clock.right().frame;
    std::printf("wrote %s, %s at %lld ms, left pose %zu, right frame %zu %s\n",
                options.dumpPath.c_str(), scene.timeline.name.c_str(),
                static_cast<long long>(options.dumpAtMs), clock.left().poseIndex,
                frameIndex(scene.interp, frame),
                frame != nullptr ? anim::interpKindName(frame->kind) : "none");
    return 0;
}

int runReview(const ReviewOptions& options) {
    Scene scene = loadScene(options);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window =
        SDL_CreateWindow("swchess-review", kWindowWidth, kWindowHeight,
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
    SDL_SetRenderLogicalPresentation(renderer, kWindowWidth, kWindowHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // Every authored pose goes up once. The interpolated pictures are far too
    // many to keep on the GPU, so one streaming texture carries whichever one
    // stands now.
    std::map<std::uint32_t, SDL_Texture*> poseTextures;
    for (const auto& [offset, record] : scene.timeline.anx.records) {
        std::vector<std::uint8_t> rgba = anxToRGBA(record);
        SDL_Texture* texture = makeTexture(renderer, rgba.data(), record.width, record.height);
        if (texture == nullptr) {
            std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            return 1;
        }
        poseTextures[offset] = texture;
    }
    SDL_Texture* frameTexture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                          scene.interp.frameRect.width, scene.interp.frameRect.height);
    if (frameTexture == nullptr) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetTextureBlendMode(frameTexture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(frameTexture, SDL_SCALEMODE_NEAREST);
    const anim::InterpFrame* uploaded = nullptr;

    SDL_Texture* backgroundTexture = makeTexture(renderer, scene.background.rgba.data(),
                                                 scene.background.width, scene.background.height);
    SDL_Texture* stripTexture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                          kWindowWidth, kStripHeight);
    if (stripTexture == nullptr) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetTextureBlendMode(stripTexture, SDL_BLENDMODE_NONE);

    // Only the left player's cues reach the mixer, so a sound plays once even
    // though two players run the same schedule.
    audio::Mixer mixer;
    std::map<std::string, audio::Clip> clips = loadClips(options.cdDir);

    ReviewClock clock(&scene.timeline, &scene.interp);
    Backdrop backdrop = Backdrop::Real;
    int speedIndex = kNormalSpeed;
    bool playing = true;

    using WallClock = std::chrono::steady_clock;
    WallClock::time_point last = WallClock::now();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                if (key == SDLK_ESCAPE) {
                    running = false;
                } else if (key == SDLK_SPACE) {
                    playing = !playing;
                } else if (key == SDLK_RIGHT || key == SDLK_LEFT) {
                    playing = false;
                    clock.seek(stepSample(clock.animMs(), key == SDLK_RIGHT ? 1 : -1));
                    mixer.stopAll();
                } else if (key == SDLK_PERIOD || key == SDLK_COMMA) {
                    playing = false;
                    clock.seek(stepPose(scene.timeline, clock.animMs(),
                                        key == SDLK_PERIOD ? 1 : -1));
                    mixer.stopAll();
                } else if (key == SDLK_RIGHTBRACKET) {
                    speedIndex = std::min(speedIndex + 1, kSpeedCount - 1);
                } else if (key == SDLK_LEFTBRACKET) {
                    speedIndex = std::max(speedIndex - 1, 0);
                } else if (key == SDLK_R) {
                    clock.seek(0.0);
                    mixer.stopAll();
                    playing = true;
                } else if (key == SDLK_S) {
                    std::vector<anim::SoundEvent> cancelled = clock.skip();
                    std::printf("skipped %s, %zu cues cancelled\n", scene.timeline.name.c_str(),
                                cancelled.size());
                    mixer.stopAll();
                    playing = false;
                } else if (key == SDLK_T) {
                    clock.setLeftIsInterp(!clock.leftIsInterp());
                } else if (key == SDLK_B) {
                    backdrop = nextBackdrop(backdrop);
                } else if (key >= SDLK_1 && key <= SDLK_8) {
                    const int pick = static_cast<int>(key - SDLK_1);
                    if (pick < kBackgroundCount) {
                        try {
                            scene.background =
                                loadBmp(backgroundPath(options.cdDir, kBackgrounds[pick]));
                            scene.backgroundName = kBackgrounds[pick];
                            if (backgroundTexture != nullptr) {
                                SDL_DestroyTexture(backgroundTexture);
                            }
                            backgroundTexture =
                                makeTexture(renderer, scene.background.rgba.data(),
                                            scene.background.width, scene.background.height);
                        } catch (const std::exception& error) {
                            std::fprintf(stderr, "%s\n", error.what());
                        }
                    }
                }
            }
        }

        const WallClock::time_point now = WallClock::now();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        if (playing && !clock.finished()) {
            std::vector<anim::SoundEvent> cues =
                clock.advanceTo(clock.animMs() + elapsedMs * kSpeeds[speedIndex]);
            for (const anim::SoundEvent& cue : cues) {
                if (cue.sound == nullptr || !cue.sound->resolved) {
                    continue;
                }
                auto found = clips.find(cue.sound->resource);
                if (found != clips.end()) {
                    mixer.play(found->second, audio::Channel::Effects);
                }
            }
        }
        if (clock.finished()) {
            playing = false;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        for (int side = 0; side < 2; ++side) {
            const float xOffset = side == 0 ? 0.0f : static_cast<float>(kHalfWidth);
            const SDL_FRect half{xOffset, 0.0f, static_cast<float>(kHalfWidth),
                                 static_cast<float>(kHalfHeight)};
            if (backdrop == Backdrop::Real && backgroundTexture != nullptr) {
                SDL_RenderTexture(renderer, backgroundTexture, nullptr, &half);
            } else if (backdrop == Backdrop::Checker) {
                for (int y = 0; y < kHalfHeight; y += 16) {
                    for (int x = 0; x < kHalfWidth; x += 16) {
                        const bool light = ((x / 16) + (y / 16)) % 2 == 0;
                        SDL_SetRenderDrawColor(renderer, light ? 200 : 90, light ? 200 : 90,
                                               light ? 200 : 90, 255);
                        const SDL_FRect cell{xOffset + static_cast<float>(x),
                                             static_cast<float>(y), 16.0f, 16.0f};
                        SDL_RenderFillRect(renderer, &cell);
                    }
                }
            } else {
                const std::uint8_t level = backdrop == Backdrop::White ? 255 : 0;
                SDL_SetRenderDrawColor(renderer, level, level, level, 255);
                SDL_RenderFillRect(renderer, &half);
            }

            const anim::DrawState& draw = side == 0 ? clock.left() : clock.right();
            if (!draw.visible) {
                continue;
            }
            const SDL_FRect target{xOffset + static_cast<float>(draw.x),
                                   static_cast<float>(draw.y), static_cast<float>(draw.width),
                                   static_cast<float>(draw.height)};
            if (draw.record != nullptr) {
                SDL_RenderTexture(renderer, poseTextures[draw.record->offset], nullptr, &target);
            } else if (draw.frame != nullptr) {
                if (draw.frame != uploaded) {
                    SDL_UpdateTexture(frameTexture, nullptr, draw.frame->image.pixels.data(),
                                      draw.frame->image.width * 4);
                    uploaded = draw.frame;
                }
                SDL_RenderTexture(renderer, frameTexture, nullptr, &target);
            }
        }

        // The strip goes through the same software text path the dump uses, so
        // the window and the PPM read alike.
        const Status status =
            statusFor(scene, clock.animMs(), kSpeeds[speedIndex], playing, clock.leftIsInterp(),
                      clock.left(), clock.right(), backdrop);
        Image strip;
        strip.width = kWindowWidth;
        strip.height = kWindowHeight;
        strip.rgba.assign(static_cast<std::size_t>(strip.width) * strip.height * 4, 0);
        drawStatus(strip, scene.font, status);
        SDL_UpdateTexture(stripTexture, nullptr,
                          strip.rgba.data() + static_cast<std::size_t>(kHalfHeight) *
                                                  strip.width * 4,
                          strip.width * 4);
        const SDL_FRect stripRect{0.0f, static_cast<float>(kHalfHeight),
                                  static_cast<float>(kWindowWidth),
                                  static_cast<float>(kStripHeight)};
        SDL_RenderTexture(renderer, stripTexture, nullptr, &stripRect);
        SDL_RenderPresent(renderer);
    }

    for (auto& [offset, texture] : poseTextures) {
        (void)offset;
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyTexture(frameTexture);
    SDL_DestroyTexture(stripTexture);
    if (backgroundTexture != nullptr) {
        SDL_DestroyTexture(backgroundTexture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace swchess::app
