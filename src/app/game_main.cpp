// swchess is the playable game. It opens at the size the original window had,
// runs the title sequence, and then shows the board, the row of menu buttons
// and the status bar beside them.
//
//   swchess --cd original/win3x/cd --assets assets
//   swchess --cd original/win3x/cd --skip-title --set WHTTOP --language german
//   swchess --cd original/win3x/cd --load original/win3x/cd/STARWARS.CMG
//   swchess --cd original/win3x/cd --assets assets --cadence original \
//           --script "e2e4 d7d5 e4d5" --dump-at 3000 out.ppm
//
// --assets names the directory tools/interp writes its 60 frames per second
// capture frames into. A capture that has them plays them, and one that does
// not plays the authored poses at their original 120 ms cadence. --cadence
// picks which of the two the game asks for. --config names the directory
// SWC.INI lives in, and --skip-title opens the game screen straight away.
//
// The --script form never opens a window, so it runs on a machine with no
// display. It plays the moves through the same state machine the window
// drives and writes the picture standing on the screen at the given animation
// time. --dump-at on its own writes the title screen showing at that time.
//
// Keys: 1 to 4 pick the set, L cycles the language, W turns walking on and
// off, C turns the capture films on and off, I switches between the
// interpolated and the original capture cadence, U takes a move back, N starts
// a new game, and Esc quits. Any key or click skips a capture and ends a title
// screen.

#include <SDL3/SDL.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "game/script.h"
#include "game/session.h"
#include "game/shell.h"
#include "render/compositor.h"

namespace {

constexpr int kWindowWidth = swchess::game::kWindowWidth;
constexpr int kWindowHeight = swchess::game::kWindowHeight;

struct Options {
    swchess::game::ShellOptions shell{};
    std::string script;
    std::int64_t dumpAtMs = -1;
    std::string dumpPath;
};

void printUsage() {
    std::fprintf(stderr,
                 "usage: swchess --cd <dir> [--assets <dir>] [--config <dir>]\n"
                 "                [--skip-title] [--load <file>] [--set <SET>]\n"
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
            options.shell.cdDir = value;
        } else if (arg == "--assets") {
            const char* value = next("--assets");
            if (value == nullptr) return false;
            options.shell.assetsDir = value;
        } else if (arg == "--config") {
            const char* value = next("--config");
            if (value == nullptr) return false;
            options.shell.configDir = value;
        } else if (arg == "--load") {
            const char* value = next("--load");
            if (value == nullptr) return false;
            options.shell.loadPath = value;
        } else if (arg == "--skip-title") {
            options.shell.skipTitle = true;
        } else if (arg == "--cadence") {
            const char* value = next("--cadence");
            if (value == nullptr) return false;
            if (!parseCadence(value, &options.shell.cadence)) {
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
            options.shell.set = set;
        } else if (arg == "--language") {
            const char* value = next("--language");
            if (value == nullptr) return false;
            swchess::text::Language language = swchess::text::Language::English;
            if (!parseLanguage(value, &language)) {
                std::fprintf(stderr, "unknown language %s\n", value);
                return false;
            }
            options.shell.language = language;
        } else if (arg == "--no-walking") {
            options.shell.walking = false;
        } else if (arg == "--no-captures") {
            options.shell.captures = false;
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
    if (options.shell.cdDir.empty()) {
        printUsage();
        return false;
    }
    return true;
}

// The headless game screen. It plays the script and writes one picture of the
// board with the button row and the status bar over it.
int runScript(const Options& options) {
    const swchess::ui::Settings ini = swchess::game::loadShellSettings(options.shell);
    const swchess::game::Settings settings =
        swchess::game::sessionSettingsFor(options.shell, ini);

    swchess::ui::ButtonBar bar(options.shell.cdDir, settings.language,
                               swchess::game::barSettingsFor(settings, ini));

    swchess::game::ScriptOptions script;
    script.cdDir = options.shell.cdDir;
    script.assetsDir = options.shell.assetsDir;
    script.settings = settings;
    script.moves = swchess::game::splitScript(options.script);
    script.dumpAtMs = options.dumpAtMs;
    script.dumpPath = options.dumpPath;
    script.decorate = [&bar](swchess::game::GameSession& session, swchess::Image& picture) {
        bar.setMessageBytes(session.statusBytes());
        swchess::Image window;
        swchess::game::composeGameScreen(window, picture, bar);
        picture = std::move(window);
    };

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

// The headless title sequence. It runs the clock to the dump time and writes
// whichever screen is showing then.
int runTitleDump(const Options& options) {
    const swchess::ui::Settings ini = swchess::game::loadShellSettings(options.shell);
    const swchess::game::Settings settings =
        swchess::game::sessionSettingsFor(options.shell, ini);

    swchess::ui::TitleSequence title(options.shell.cdDir, settings.language);
    title.start(0);
    title.advance(options.dumpAtMs);

    swchess::Image canvas = swchess::ui::makeCanvas(kWindowWidth, kWindowHeight);
    title.render(canvas);
    swchess::writePPM(canvas, options.dumpPath);
    std::printf("wrote %s at %lld ms, title state %d\n", options.dumpPath.c_str(),
                static_cast<long long>(options.dumpAtMs), static_cast<int>(title.state()));
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

// Asks the player for a file. macOS has a chooser in osascript, which prints
// the path and needs no window of our own.
std::string askForPath(bool save) {
#ifdef __APPLE__
    const char* command =
        save ? "osascript -e 'POSIX path of (choose file name with prompt \"Save game\" "
               "default name \"game.json\")' 2>/dev/null"
             : "osascript -e 'POSIX path of (choose file with prompt \"Load game\")' 2>/dev/null";
    std::FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return std::string();
    }
    std::string answer;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        answer += buffer;
    }
    pclose(pipe);
    while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r')) {
        answer.pop_back();
    }
    return answer;
#else
    (void)save;
    return std::string();
#endif
}

// The character the shell reads for one SDL key.
char shellKey(SDL_Keycode key) {
    if (key == SDLK_ESCAPE) {
        return '\x1b';
    }
    if (key >= 0x20 && key < 0x7f) {
        return static_cast<char>(std::tolower(static_cast<int>(key)));
    }
    return '\0';
}

int runWindow(Options& options) {
    options.shell.chooseFile = askForPath;
    swchess::game::GameShell shell(options.shell);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Star Wars Chess", kWindowWidth, kWindowHeight,
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
    SDL_Texture* screen = makeTexture(renderer, kWindowWidth, kWindowHeight);

    // Presentation follows the display refresh. The animation clock is this
    // monotonic one, and the shell reads nothing else.
    using Clock = std::chrono::steady_clock;
    const Clock::time_point origin = Clock::now();
    auto animationMs = [&]() -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - origin)
            .count();
    };
    shell.start(animationMs());

    swchess::Image frame;
    while (!shell.finished()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const std::int64_t now = animationMs();
            if (event.type == SDL_EVENT_QUIT) {
                shell.quit();
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                float x = 0.0f;
                float y = 0.0f;
                SDL_RenderCoordinatesFromWindow(renderer, event.motion.x, event.motion.y, &x, &y);
                shell.onMouseMove(static_cast<int>(x), static_cast<int>(y));
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float x = 0.0f;
                float y = 0.0f;
                SDL_RenderCoordinatesFromWindow(renderer, event.button.x, event.button.y, &x, &y);
                shell.onMouseDown(static_cast<int>(x), static_cast<int>(y), now);
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                float x = 0.0f;
                float y = 0.0f;
                SDL_RenderCoordinatesFromWindow(renderer, event.button.x, event.button.y, &x, &y);
                shell.onMouseUp(static_cast<int>(x), static_cast<int>(y), now);
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                shell.onKey(shellKey(event.key.key), now);
            }
        }

        shell.advance(animationMs());
        if (shell.takeMinimize()) {
            SDL_MinimizeWindow(window);
        }
        shell.render(frame);
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
                      "Star Wars Chess  %s  %s  %s  %s  walk %s  captures %s  audio %s",
                      swchess::game::shellStateName(shell.state()),
                      swchess::board::setKey(shell.session().settings().set),
                      swchess::text::languageName(shell.session().settings().language),
                      swchess::game::animStateName(shell.session().state()),
                      shell.session().settings().walking ? "on" : "off",
                      shell.session().settings().captures ? "on" : "off",
                      shell.session().mixer().driverName().c_str());
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
        if (!options.script.empty()) {
            return runScript(options);
        }
        if (options.dumpAtMs >= 0) {
            // A dump with no script writes the title screen showing then,
            // unless the caller asked to skip the title.
            return options.shell.skipTitle ? runScript(options) : runTitleDump(options);
        }
        return runWindow(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
