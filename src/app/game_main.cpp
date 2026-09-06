// swchess is the playable game. Two people move the pieces on the animated
// board, the pieces walk between squares, and a capture plays its film.
//
//   swchess --cd original/win3x/cd --assets assets
//   swchess --cd original/win3x/cd --set WHTTOP --language german
//   swchess --cd original/win3x/cd --assets assets --cadence original \
//           --script "e2e4 d7d5 e4d5" --dump-at 3000 out.ppm
//
// --assets names the directory tools/interp writes its 60 frames per second
// capture frames into. A capture that has them plays them, and one that does
// not plays the authored poses at their original 120 ms cadence. --cadence
// picks which of the two the game asks for.
//
// The --script form never opens a window, so it runs on a machine with no
// display. It plays the moves through the same state machine the window
// drives and writes the picture standing on the screen at the given animation
// time.
//
// Keys: 1 to 4 pick the set, L cycles the language, W turns walking on and
// off, C turns the capture films on and off, I switches between the
// interpolated and the original capture cadence, U takes a move back, N starts
// a new game, and Esc quits. Any key or click skips a capture.

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "game/script.h"
#include "game/session.h"

namespace {

constexpr int kLogicalWidth = swchess::anim::kCanvasWidth;
constexpr int kLogicalHeight = swchess::anim::kCanvasHeight;

struct Options {
    std::string cdDir;
    std::string assetsDir;
    swchess::game::Settings settings{};
    std::string script;
    std::int64_t dumpAtMs = -1;
    std::string dumpPath;
};

void printUsage() {
    std::fprintf(stderr,
                 "usage: swchess --cd <dir> [--assets <dir>] [--set <SET>]\n"
                 "                [--language <NAME>] [--cadence <NAME>]\n"
                 "                [--no-walking] [--no-captures]\n"
                 "       swchess --cd <dir> --script \"e2e4 e7e5\" "
                 "--dump-at <MS> <out.ppm>\n"
                 "       a set is WHTBTM, WHTTOP, FACING or 2D\n"
                 "       a language is english, french, german or spanish\n"
                 "       a cadence is original or interpolated\n");
}

bool parseLanguage(const std::string& name, swchess::text::Language* language) {
    if (name == "english") *language = swchess::text::Language::English;
    else if (name == "french") *language = swchess::text::Language::French;
    else if (name == "german") *language = swchess::text::Language::German;
    else if (name == "spanish") *language = swchess::text::Language::Spanish;
    else return false;
    return true;
}

bool parseCadence(const std::string& name, swchess::anim::Cadence* cadence) {
    if (name == "original") *cadence = swchess::anim::Cadence::Original120ms;
    else if (name == "interpolated") *cadence = swchess::anim::Cadence::Interpolated60;
    else return false;
    return true;
}

bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
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
        } else if (arg == "--assets") {
            const char* value = next("--assets");
            if (value == nullptr) return false;
            options.assetsDir = value;
        } else if (arg == "--cadence") {
            const char* value = next("--cadence");
            if (value == nullptr) return false;
            if (!parseCadence(value, &options.settings.cadence)) {
                std::fprintf(stderr, "unknown cadence %s\n", value);
                return false;
            }
        } else if (arg == "--set") {
            const char* value = next("--set");
            if (value == nullptr) return false;
            std::optional<swchess::board::SetId> set = swchess::board::parseSetName(value);
            if (!set.has_value()) {
                std::fprintf(stderr, "unknown set %s\n", value);
                return false;
            }
            options.settings.set = *set;
        } else if (arg == "--language") {
            const char* value = next("--language");
            if (value == nullptr) return false;
            if (!parseLanguage(value, &options.settings.language)) {
                std::fprintf(stderr, "unknown language %s\n", value);
                return false;
            }
        } else if (arg == "--no-walking") {
            options.settings.walking = false;
        } else if (arg == "--no-captures") {
            options.settings.captures = false;
        } else if (arg == "--script") {
            const char* value = next("--script");
            if (value == nullptr) return false;
            options.script = value;
        } else if (arg == "--dump-at") {
            const char* number = next("--dump-at");
            if (number == nullptr) return false;
            options.dumpAtMs = std::atoll(number);
            const char* path = next("--dump-at output path");
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
    if (options.cdDir.empty()) {
        printUsage();
        return false;
    }
    return true;
}

// The headless path. It plays the script and writes one picture.
int runScript(const Options& options) {
    swchess::game::ScriptOptions script;
    script.cdDir = options.cdDir;
    script.assetsDir = options.assetsDir;
    script.settings = options.settings;
    script.moves = swchess::game::splitScript(options.script);
    script.dumpAtMs = options.dumpAtMs;
    script.dumpPath = options.dumpPath;

    const swchess::game::ScriptResult result = swchess::game::runScript(script);
    if (!result.rejected.empty()) {
        std::fprintf(stderr, "the position does not allow %s\n", result.rejected.c_str());
        return 1;
    }
    std::printf("played %zu moves, ended at %lld ms, state %s\n", result.played.size(),
                static_cast<long long>(result.endedMs),
                swchess::game::animStateName(result.states.back().state));
    std::printf("fen %s\n", result.finalFen.c_str());
    for (const auto& [name, count] : result.soundPlays) {
        std::printf("sound %s played %d\n", name.c_str(), count);
    }
    if (result.dumped && !options.dumpPath.empty()) {
        std::printf("wrote %s at %lld ms, state %s", options.dumpPath.c_str(),
                    static_cast<long long>(options.dumpAtMs),
                    swchess::game::animStateName(result.dumpState));
        if (result.dumpHasPose) {
            std::printf(", capture %s cadence %s pose %zu, %dx%d at %d,%d",
                        result.dumpCapture.c_str(),
                        swchess::anim::cadenceName(result.dumpCadence), result.dumpPoseIndex,
                        result.dumpWidth, result.dumpHeight, result.dumpX, result.dumpY);
            if (result.dumpHasFrame) {
                std::printf(", interpolated frame %s", result.dumpFrameKind.c_str());
            }
        }
        std::printf("\n");
    }
    return 0;
}

SDL_Texture* makeTexture(SDL_Renderer* renderer, int width, int height) {
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STREAMING, width, height);
    if (texture != nullptr) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    return texture;
}

int runWindow(const Options& options) {
    swchess::game::GameSession session(options.cdDir, options.assetsDir, options.settings);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("swchess", kLogicalWidth, kLogicalHeight,
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
    SDL_Texture* screen = makeTexture(renderer, kLogicalWidth, kLogicalHeight);

    // Presentation follows the display refresh. The animation clock is this
    // monotonic one, and the session reads nothing else.
    using Clock = std::chrono::steady_clock;
    const Clock::time_point origin = Clock::now();
    auto animationMs = [&]() -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - origin)
            .count();
    };

    swchess::Image frame;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const std::int64_t now = animationMs();
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float x = 0.0f;
                float y = 0.0f;
                SDL_RenderCoordinatesFromWindow(renderer, event.button.x, event.button.y, &x, &y);
                session.clickPixel(static_cast<int>(x), static_cast<int>(y), now);
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                if (session.skipCapture(now)) {
                    continue;
                }
                if (key == SDLK_ESCAPE) {
                    running = false;
                } else if (key == SDLK_1) {
                    session.setSet(swchess::board::SetId::WhiteBottom);
                } else if (key == SDLK_2) {
                    session.setSet(swchess::board::SetId::WhiteTop);
                } else if (key == SDLK_3) {
                    session.setSet(swchess::board::SetId::Facing);
                } else if (key == SDLK_4) {
                    session.setSet(swchess::board::SetId::TwoD);
                } else if (key == SDLK_L) {
                    session.cycleLanguage();
                } else if (key == SDLK_W) {
                    session.setWalking(!session.settings().walking);
                } else if (key == SDLK_C) {
                    session.setCaptures(!session.settings().captures);
                } else if (key == SDLK_I) {
                    session.toggleCadence();
                } else if (key == SDLK_U) {
                    session.undo();
                } else if (key == SDLK_N) {
                    session.newGame();
                }
            }
        }

        session.advance(animationMs());
        session.render(frame);
        if (screen != nullptr) {
            SDL_UpdateTexture(screen, nullptr, frame.rgba.data(), frame.width * 4);
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (screen != nullptr) {
            SDL_RenderTexture(renderer, screen, nullptr, nullptr);
        }
        SDL_RenderPresent(renderer);

        char title[256];
        std::snprintf(title, sizeof(title),
                      "swchess  %s  %s  %s  walk %s  captures %s  cadence %s  audio %s",
                      swchess::board::setKey(session.settings().set),
                      swchess::text::languageName(session.settings().language),
                      swchess::game::animStateName(session.state()),
                      session.settings().walking ? "on" : "off",
                      session.settings().captures ? "on" : "off",
                      swchess::anim::cadenceName(session.settings().cadence),
                      session.mixer().driverName().c_str());
        SDL_SetWindowTitle(window, title);
    }

    if (screen != nullptr) {
        SDL_DestroyTexture(screen);
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
        if (!options.script.empty() || options.dumpAtMs >= 0) {
            return runScript(options);
        }
        return runWindow(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
