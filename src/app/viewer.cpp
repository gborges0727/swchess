// swchess-viewer shows one decoded .ANX capture over one of the CD
// backgrounds. It opens an SDL3 window backed by the default macOS renderer,
// which is Metal.
//
//   swchess-viewer --cd original/win3x/cd --capture BBWB [--bg SPACE256]
//   swchess-viewer --cd original/win3x/cd --capture BBWB --dump-frame 39 out.ppm
//
// The --dump-frame form composites on the CPU and never opens a window, so it
// runs on a machine with no display.

#include <SDL3/SDL.h>

#include <chrono>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "assets/anx.h"
#include "assets/bmp.h"
#include "render/compositor.h"

namespace {

// The nine backgrounds the number keys select, in key order.
const char* const kBackgrounds[] = {
    "SPACE256", "THRON256", "2DBDBTOP", "2DBDWTOP",
    "FACING_P", "2DSET_P",  "WHTBTM_P", "WHTTOP_P",
};
constexpr int kBackgroundCount = 8;

// One timeline entry lasts this long until the real cadence is recovered from
// XCHESS.EXE. Section 5 of docs/plan.md calls this provisional.
constexpr std::int64_t kFrameMillis = 120;

constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 480;

struct Options {
    std::string cdDir;
    std::string capture;
    std::string background = "SPACE256";
    int dumpFrame = -1;
    std::string dumpPath;
};

void printUsage() {
    std::fprintf(stderr,
                 "usage: swchess-viewer --cd <dir> --capture <NAME> [--bg <NAME>]\n"
                 "       swchess-viewer --cd <dir> --capture <NAME> --dump-frame <N> <out.ppm>\n");
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
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return false;
        } else {
            std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
            return false;
        }
    }
    if (options.cdDir.empty() || options.capture.empty()) {
        printUsage();
        return false;
    }
    return true;
}

std::string capturePath(const Options& options) {
    return options.cdDir + "/" + options.capture + ".ANX";
}

std::string backgroundPath(const std::string& cdDir, const std::string& name) {
    return cdDir + "/" + name + ".BMP";
}

// Composites timeline entry `index` over the background and writes a PPM.
int dumpFrame(const Options& options) {
    swchess::AnxFile capture = swchess::loadAnx(capturePath(options));
    if (options.dumpFrame < 0 ||
        static_cast<std::size_t>(options.dumpFrame) >= capture.timeline.size()) {
        std::fprintf(stderr, "frame %d is outside the %zu entry timeline\n", options.dumpFrame,
                     capture.timeline.size());
        return 1;
    }
    swchess::Image canvas = swchess::loadBmp(backgroundPath(options.cdDir, options.background));
    const swchess::AnxRecord& record =
        capture.records.at(capture.timeline[static_cast<std::size_t>(options.dumpFrame)]);
    std::vector<std::uint8_t> rgba = swchess::anxToRGBA(record);
    swchess::Placement place = swchess::centerOn(canvas, record.width, record.height);
    swchess::blitRGBA(canvas, rgba.data(), record.width, record.height, place.x, place.y);
    swchess::writePPM(canvas, options.dumpPath);
    std::printf("wrote %s from %s frame %d, record 0x%x, %dx%d at %d,%d over %s\n",
                options.dumpPath.c_str(), options.capture.c_str(), options.dumpFrame, record.offset,
                record.width, record.height, place.x, place.y, options.background.c_str());
    return 0;
}

// One preloaded capture frame ready to draw.
struct FrameTexture {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    std::uint32_t offset = 0;
};

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
    swchess::AnxFile capture = swchess::loadAnx(capturePath(options));
    if (capture.timeline.empty()) {
        std::fprintf(stderr, "%s has no timeline entries\n", options.capture.c_str());
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

    // Upload every distinct record once, then point each timeline entry at it.
    std::vector<SDL_Texture*> owned;
    std::map<std::uint32_t, std::size_t> byOffset;
    for (const auto& [offset, record] : capture.records) {
        std::vector<std::uint8_t> rgba = swchess::anxToRGBA(record);
        SDL_Texture* texture = makeTexture(renderer, rgba.data(), record.width, record.height);
        if (texture == nullptr) {
            std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            return 1;
        }
        byOffset[offset] = owned.size();
        owned.push_back(texture);
    }
    std::vector<FrameTexture> frames;
    frames.reserve(capture.timeline.size());
    for (std::uint32_t offset : capture.timeline) {
        const swchess::AnxRecord& record = capture.records.at(offset);
        frames.push_back(FrameTexture{owned[byOffset[offset]], record.width, record.height, offset});
    }

    std::string backgroundName = options.background;
    swchess::Image background = swchess::loadBmp(backgroundPath(options.cdDir, backgroundName));
    SDL_Texture* backgroundTexture =
        makeTexture(renderer, background.rgba.data(), background.width, background.height);

    using Clock = std::chrono::steady_clock;
    Clock::time_point started = Clock::now();
    std::int64_t pausedAtMillis = 0;
    bool playing = false;
    std::size_t index = 0;

    auto elapsedMillis = [&]() -> std::int64_t {
        if (!playing) {
            return pausedAtMillis;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    };
    auto seekTo = [&](std::size_t target) {
        index = target;
        pausedAtMillis = static_cast<std::int64_t>(target) * kFrameMillis;
        started = Clock::now() - std::chrono::milliseconds(pausedAtMillis);
    };

    bool running = true;
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
                    if (playing) {
                        pausedAtMillis = elapsedMillis();
                        playing = false;
                    } else {
                        started = Clock::now() - std::chrono::milliseconds(pausedAtMillis);
                        playing = true;
                    }
                } else if (key == SDLK_RIGHT) {
                    playing = false;
                    seekTo(index + 1 < frames.size() ? index + 1 : index);
                } else if (key == SDLK_LEFT) {
                    playing = false;
                    seekTo(index > 0 ? index - 1 : 0);
                } else if (key == SDLK_R) {
                    seekTo(0);
                } else if (key >= SDLK_1 && key <= SDLK_9) {
                    int pick = static_cast<int>(key - SDLK_1);
                    if (pick < kBackgroundCount) {
                        backgroundName = kBackgrounds[pick];
                        try {
                            background = swchess::loadBmp(backgroundPath(options.cdDir, backgroundName));
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

        std::int64_t millis = elapsedMillis();
        if (playing) {
            std::size_t wanted = static_cast<std::size_t>(millis / kFrameMillis);
            if (wanted >= frames.size()) {
                wanted = frames.size() - 1;
                playing = false;
                pausedAtMillis = static_cast<std::int64_t>(wanted) * kFrameMillis;
                millis = pausedAtMillis;
            }
            index = wanted;
        }

        const FrameTexture& frame = frames[index];
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (backgroundTexture != nullptr) {
            SDL_FRect whole{0.0f, 0.0f, static_cast<float>(background.width),
                            static_cast<float>(background.height)};
            SDL_RenderTexture(renderer, backgroundTexture, nullptr, &whole);
        }
        SDL_FRect target{static_cast<float>((kLogicalWidth - frame.width) / 2),
                         static_cast<float>((kLogicalHeight - frame.height) / 2),
                         static_cast<float>(frame.width), static_cast<float>(frame.height)};
        SDL_RenderTexture(renderer, frame.texture, nullptr, &target);
        SDL_RenderPresent(renderer);

        char title[256];
        std::snprintf(title, sizeof(title),
                      "swchess-viewer  %s over %s  frame %zu/%zu  record 0x%x  %lld ms  %s",
                      options.capture.c_str(), backgroundName.c_str(), index + 1, frames.size(),
                      frame.offset, static_cast<long long>(millis), playing ? "playing" : "paused");
        SDL_SetWindowTitle(window, title);
    }

    for (SDL_Texture* texture : owned) {
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
        if (options.dumpFrame >= 0) {
            return dumpFrame(options);
        }
        return runViewer(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
