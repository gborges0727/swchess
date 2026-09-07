// Plays whole games through GameSession with a simulated clock and checks
// what the state machine, the rules module, the sound scheduler and the
// compositor did. Run it with the CD directory and a directory it may write
// pictures into.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "anim/capture.h"
#include "assets/anx.h"
#include "game/script.h"
#include "game/session.h"
#include "game/shell.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
        ++failures;
    }
    else {
        std::printf("ok   %s\n", what.c_str());
    }
}

// The states the run passed through, as one readable line.
std::string trail(const swchess::game::ScriptResult& result) {
    std::string line;
    for (const swchess::game::StateSample& sample : result.states) {
        if (!line.empty()) {
            line += " ";
        }
        line += swchess::game::animStateName(sample.state);
    }
    return line;
}

// True when `states` runs walking, then capturing, then `after` somewhere.
bool hasRun(const swchess::game::ScriptResult& result, swchess::game::AnimState after) {
    for (std::size_t i = 0; i + 2 < result.states.size(); ++i) {
        if (result.states[i].state == swchess::game::AnimState::Walking &&
            result.states[i + 1].state == swchess::game::AnimState::Capturing &&
            result.states[i + 2].state == after) {
            return true;
        }
    }
    return false;
}

// How many capture films the run played.
std::size_t captureCount(const swchess::game::ScriptResult& result) {
    std::size_t count = 0;
    for (const swchess::game::StateSample& sample : result.states) {
        if (sample.state == swchess::game::AnimState::Capturing) {
            ++count;
        }
    }
    return count;
}

// The animation time the last capture of the run started at.
std::int64_t lastCaptureStart(const swchess::game::ScriptResult& result) {
    std::int64_t at = -1;
    for (const swchess::game::StateSample& sample : result.states) {
        if (sample.state == swchess::game::AnimState::Capturing) {
            at = sample.timeMs;
        }
    }
    return at;
}

// Presses one button of the page the bar shows, the way a player does: the
// mouse goes down on the button and comes back up on it.
void pressButton(swchess::game::GameShell& shell, int slot) {
    const swchess::ui::Rect rect = swchess::ui::buttonRect(slot);
    shell.onMouseMove(rect.x + 5, rect.y + 5);
    shell.onMouseDown(rect.x + 5, rect.y + 5, 0);
    shell.onMouseUp(rect.x + 5, rect.y + 5, 0);
}

// Plays one move through a session and runs the animation out.
void playThrough(swchess::game::GameSession& session, const char* text, std::int64_t& clock) {
    std::optional<swchess::chess::Move> move = session.position().parseLongAlgebraic(text);
    if (!move.has_value()) {
        return;
    }
    session.clickSquare(move->from, clock);
    session.clickSquare(move->to, clock);
    for (int guard = 0; guard < 100000; ++guard) {
        clock += 10;
        session.advance(clock);
        if (session.state() == swchess::game::AnimState::Idle) {
            return;
        }
    }
}

// Steps the shell clock until `ready` answers true or ten real seconds pass.
// The random engine waits out 300 ms of wall time before it answers, so a
// count of simulated ticks is the wrong thing to wait on.
bool pumpUntil(swchess::game::GameShell& shell, std::int64_t& clock,
               const std::function<bool()>& ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        clock += 10;
        shell.advance(clock);
    }
    return true;
}

// Replays `moves` from the opening position and says whether every one of
// them was legal where it was played.
bool everyMoveLegal(const std::vector<std::string>& moves, std::string* endFen) {
    swchess::chess::Game replay;
    for (const std::string& text : moves) {
        const std::optional<swchess::chess::Move> move =
            replay.position().parseLongAlgebraic(text);
        if (!move.has_value() || !replay.play(*move)) {
            return false;
        }
    }
    if (endFen != nullptr) {
        *endFen = replay.position().fen();
    }
    return true;
}

std::string readWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// The cues one run started, as "NAME@1234" in the order they started.
std::vector<std::string> cueTrail(const swchess::game::ScriptResult& result) {
    std::vector<std::string> trail;
    for (const swchess::game::GameSession::SoundPlay& play : result.soundLog) {
        trail.push_back(play.name + "@" + std::to_string(play.timeMs));
    }
    return trail;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: game_test <cd directory> [output directory] [assets directory]\n");
        return 2;
    }
    const std::string cdDir = argv[1];
    const std::string outDir = argc > 2 ? argv[2] : std::string(".");
    const std::string assetsDir = argc > 3 ? argv[3] : std::string("assets");

    try {
        // Scholar's mate. White's queen takes the f7 pawn and mates, so the
        // run walks, plays the film, and ends the game.
        swchess::game::ScriptOptions mate;
        mate.cdDir = cdDir;
        mate.moves = swchess::game::splitScript("e2e4 e7e5 f1c4 g8f6 d1h5 b8c6 h5f7");
        const swchess::game::ScriptResult mated = swchess::game::runScript(mate);
        std::printf("scholar's mate states: %s\n", trail(mated).c_str());

        check(mated.rejected.empty(), "the rules module took every move of the script");
        check(mated.played.size() == 7, "all seven moves reached the board");
        check(mated.finalFen == "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4",
              "the position after the script is the mated one");
        check(mated.states.back().state == swchess::game::AnimState::GameOver,
              "the session ends the game on checkmate");
        check(captureCount(mated) == 1, "the one capture of the script played one film");
        check(hasRun(mated, swchess::game::AnimState::GameOver),
              "the mating capture runs walking, then capturing, then game over");

        // Every cue of the queen's film fired, each exactly as often as the
        // timeline lists it, and the mate added its victory sound after it.
        const swchess::anim::CaptureTimeline queen = swchess::anim::loadCapture(cdDir, "WQBP");
        std::map<std::string, int> expected;
        for (const swchess::anim::CaptureSound& pre : queen.preSounds) {
            if (pre.resolved) {
                ++expected[pre.resource];
            }
        }
        for (const swchess::anim::CapturePose& pose : queen.poses) {
            if (pose.hasSound && pose.sound.resolved) {
                ++expected[pose.sound.resource];
            }
        }
        if (queen.hasEndSound && queen.endSound.resolved) {
            ++expected[queen.endSound.resource];
        }
        check(!expected.empty(), "WQBP carries at least one sound cue");
        // White mates here, so FUN_1008_1745's WHTVIC.WAV plays when the film
        // ends. The moves before the film start the piece voices instead, and
        // those all sound before the film begins.
        ++expected["WHTVIC.WAV"];
        std::map<std::string, int> duringFilm;
        for (const swchess::game::GameSession::SoundPlay& play : mated.soundLog) {
            if (play.timeMs >= lastCaptureStart(mated)) {
                ++duringFilm[play.name];
            }
        }
        check(duringFilm == expected,
              "every cue of WQBP started once for each time the timeline lists it");

        // A plain capture in the middle of a game: the white e pawn takes the
        // black d pawn, and the session goes back to waiting for a click.
        swchess::game::ScriptOptions pawn;
        pawn.cdDir = cdDir;
        pawn.moves = swchess::game::splitScript("e2e4 d7d5 e4d5");
        pawn.dumpPath = outDir + "/game_test_capture.ppm";

        // How long the three walks before the film take depends on the pace in
        // walk.h, so the run happens twice. The first one names the moment the
        // film starts and the second one dumps a picture 300 ms into it.
        const swchess::game::ScriptResult timing = swchess::game::runScript(pawn);
        std::int64_t filmStartMs = -1;
        for (const swchess::game::StateSample& sample : timing.states) {
            if (sample.state == swchess::game::AnimState::Capturing) {
                filmStartMs = sample.timeMs;
                break;
            }
        }
        check(filmStartMs >= 0, "the pawn capture script plays a film");
        pawn.dumpAtMs = filmStartMs + 300;
        const swchess::game::ScriptResult took = swchess::game::runScript(pawn);
        std::printf("pawn capture states: %s\n", trail(took).c_str());

        check(took.rejected.empty(), "the rules module took every move of the pawn script");
        check(took.finalFen == "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2",
              "the white pawn stands on d5 afterwards");
        check(hasRun(took, swchess::game::AnimState::Idle),
              "the capture runs walking, then capturing, then back to idle");
        check(took.states.back().state == swchess::game::AnimState::Idle,
              "the session waits for the next click when the film ends");

        // The dump caught the film in progress, and the picture on the screen
        // is the pose the player named.
        check(took.dumped, "the run wrote its picture");
        check(took.dumpState == swchess::game::AnimState::Capturing,
              "the dump caught the session playing a capture");
        check(took.dumpCapture == "WPBP",
              "the film is the white pawn taking the black pawn");
        check(took.dumpHasPose, "a film frame is on the screen at the dump time");

        if (took.dumpHasPose) {
            const swchess::anim::CaptureTimeline film = swchess::anim::loadCapture(cdDir, "WPBP");
            auto record = film.anx.records.find(took.dumpRecordOffset);
            check(record != film.anx.records.end(), "the dump names a record of WPBP.ANX");
            if (record != film.anx.records.end()) {
                const std::vector<std::uint8_t> rgba = swchess::anxToRGBA(record->second);
                std::size_t compared = 0;
                std::size_t matched = 0;
                for (int y = 0; y < took.dumpHeight; ++y) {
                    for (int x = 0; x < took.dumpWidth; ++x) {
                        const std::size_t at =
                            (static_cast<std::size_t>(y) * took.dumpWidth + x) * 4;
                        if (rgba[at + 3] == 0) {
                            continue;  // the teal matte, which lets the board through
                        }
                        const int px = took.dumpX + x;
                        const int py = took.dumpY + y;
                        if (px < 0 || py < 0 || px >= took.dump.width ||
                            py >= took.dump.height) {
                            continue;
                        }
                        const std::size_t to =
                            (static_cast<std::size_t>(py) * took.dump.width + px) * 4;
                        ++compared;
                        if (took.dump.rgba[to] == rgba[at] &&
                            took.dump.rgba[to + 1] == rgba[at + 1] &&
                            took.dump.rgba[to + 2] == rgba[at + 2]) {
                            ++matched;
                        }
                    }
                }
                std::printf("the pose covers %zu opaque pixels of the dump\n", compared);
                check(compared > 1000, "the pose paints a large part of the picture");
                check(compared == matched, "every opaque pose pixel reached the dump unchanged");
            }
        }

        // Turning the films off leaves the same position and never enters the
        // capturing state.
        swchess::game::ScriptOptions quiet = pawn;
        quiet.settings.captures = false;
        quiet.dumpAtMs = -1;
        quiet.dumpPath.clear();
        const swchess::game::ScriptResult silent = swchess::game::runScript(quiet);
        check(captureCount(silent) == 0, "no film plays when captures are off");
        check(silent.finalFen == took.finalFen,
              "the position is the same whether or not the film plays");
        // The films are off, so no film cue sounds. The three moves still
        // speak, because FUN_1008_183f starts the piece's own WAV before it
        // looks at whether a capture follows.
        std::map<std::string, int> voicesOnly;
        for (const swchess::game::GameSession::SoundPlay& play : silent.soundLog) {
            ++voicesOnly[play.name];
        }
        check(voicesOnly == silent.soundPlays, "the log and the counts agree");
        check(voicesOnly.count("R2D2.WAV") == 1 && voicesOnly.count("STORM.WAV") == 1,
              "the white pawn and the black pawn both speak");
        const std::set<std::string> pieceVoices = {
            "LUKE.WAV", "LEIA.WAV",  "YODA.WAV", "C3P0.WAV", "CHEWIE.WAV", "R2D2.WAV",
            "EMPEROR.WAV", "VADER.WAV", "ATAT.WAV", "BOBA.WAV", "SAND.WAV", "STORM.WAV"};
        bool onlyVoices = true;
        for (const auto& entry : voicesOnly) {
            if (pieceVoices.count(entry.first) == 0) {
                onlyVoices = false;
                std::printf("unexpected sound %s\n", entry.first.c_str());
            }
        }
        check(onlyVoices, "no cue of a capture film fires when captures are off");

        // Turning walking off slides the piece instead, which still commits
        // the same moves.
        swchess::game::ScriptOptions sliding = quiet;
        sliding.settings.walking = false;
        const swchess::game::ScriptResult slid = swchess::game::runScript(sliding);
        check(slid.finalFen == took.finalFen, "sliding reaches the same position as walking");

        // Undo during Idle puts the position back.
        swchess::game::GameSession session(cdDir, std::string(), swchess::game::Settings{});
        std::int64_t now = 0;
        auto playMove = [&](const char* text) {
            std::optional<swchess::chess::Move> move =
                session.position().parseLongAlgebraic(text);
            if (!move.has_value()) {
                check(false, std::string("the position allows ") + text);
                return;
            }
            session.clickSquare(move->from, now);
            session.clickSquare(move->to, now);
            for (int guard = 0; guard < 100000; ++guard) {
                now += 10;
                session.advance(now);
                if (session.state() == swchess::game::AnimState::Idle) {
                    return;
                }
            }
            check(false, std::string("the animation of ") + text + " finished");
        };

        const std::string start = session.position().fen();
        playMove("e2e4");
        const std::string afterOne = session.position().fen();
        check(afterOne != start, "the first move changed the position");
        playMove("e7e5");
        check(session.undo(), "undo takes back a move while the session is idle");
        check(session.position().fen() == afterOne, "undo restores the position it left");
        check(session.state() == swchess::game::AnimState::Idle,
              "the session is idle again after an undo");

        // Black is to move again after that undo. A click on an empty square
        // selects nothing, and a click on White's pawn does nothing either.
        check(session.position().sideToMove() == swchess::chess::Color::Black,
              "the undo handed the move back to Black");
        check(!session.clickSquare(swchess::chess::Square{3, 4}, now),
              "clicking an empty square does nothing");
        check(!session.clickSquare(swchess::chess::Square{4, 3}, now),
              "clicking the other side's pawn does nothing");
        check(session.clickSquare(swchess::chess::Square{3, 6}, now),
              "clicking one's own pawn selects it");
        check(session.highlights().size() == 2, "the d7 pawn offers its two squares");
        // A promotion offers four pieces and plays the one it is handed. This
        // line queens on a8, and the films stay off so the run is short.
        swchess::game::ScriptOptions queening;
        queening.cdDir = cdDir;
        queening.settings.captures = false;
        queening.moves =
            swchess::game::splitScript("e2e4 d7d5 e4d5 c7c6 d5c6 b8d7 c6b7 g8f6 b7a8q");
        const swchess::game::ScriptResult queened = swchess::game::runScript(queening);
        check(queened.rejected.empty(), "the rules module took every move of the queening line");
        check(queened.finalFen.substr(0, 8) == "Q1bqkb1r",
              "a white queen stands on a8 when the pawn promotes");
        bool promoted = false;
        for (const swchess::game::StateSample& sample : queened.states) {
            if (sample.state == swchess::game::AnimState::Promoting) {
                promoted = true;
            }
        }
        check(promoted, "the session asked which piece the pawn turns into");

        // Any click ends a film early, and the move it belonged to still
        // stands on the board.
        swchess::game::GameSession skipper(cdDir, std::string(), swchess::game::Settings{});
        std::int64_t clock = 0;
        for (const char* text : {"e2e4", "d7d5", "e4d5"}) {
            std::optional<swchess::chess::Move> move =
                skipper.position().parseLongAlgebraic(text);
            skipper.clickSquare(move->from, clock);
            skipper.clickSquare(move->to, clock);
            while (skipper.state() == swchess::game::AnimState::Walking) {
                clock += 10;
                skipper.advance(clock);
            }
        }
        check(skipper.state() == swchess::game::AnimState::Capturing,
              "the third move reaches the film");
        clock += 500;
        skipper.advance(clock);
        check(skipper.skipCapture(clock), "a click ends the film");
        check(skipper.state() == swchess::game::AnimState::Idle,
              "the session is idle the moment the film is skipped");
        check(skipper.position().fen() == took.finalFen,
              "skipping the film leaves the position the move made");

        // The four sets and the four languages all load, and the status line
        // says something in each of them.
        for (int set = 0; set < 4; ++set) {
            skipper.setSet(static_cast<swchess::board::SetId>(set));
            swchess::Image frame;
            skipper.render(frame);
            check(frame.width == 640 && frame.height == 480,
                  std::string("the ") + swchess::board::setKey(skipper.settings().set) +
                      " set draws a 640 by 480 picture");
        }
        skipper.setSet(swchess::board::SetId::WhiteBottom);
        for (int step = 0; step < 4; ++step) {
            skipper.cycleLanguage();
            check(!skipper.statusBytes().empty(),
                  std::string("the status line reads in ") +
                      swchess::text::languageName(skipper.settings().language));
        }
        check(skipper.settings().language == swchess::text::Language::English,
              "four steps of the language key come back to English");

        // The enhanced cadence. Black's light-square bishop reaches b7 and
        // takes the white bishop that steps to a6, which is the BBWB film.
        const std::string bishopScript = "e2e4 b7b6 f1b5 c8b7 b5a6 b7a6";
        const std::string manifest = assetsDir + "/captures/BBWB/interp60/manifest.json";
        if (!std::filesystem::exists(manifest)) {
            std::printf("skip  %s is not there, so the enhanced cadence checks do not run\n",
                        manifest.c_str());
        } else {
            swchess::game::ScriptOptions bishop;
            bishop.cdDir = cdDir;
            bishop.assetsDir = assetsDir;
            bishop.moves = swchess::game::splitScript(bishopScript);
            bishop.settings.cadence = swchess::anim::Cadence::Original120ms;
            const swchess::game::ScriptResult timed = swchess::game::runScript(bishop);
            check(timed.rejected.empty(), "the rules module took every move of the bishop line");
            check(lastCaptureStart(timed) >= 0, "the bishop line reaches a film");

            // Three seconds into the film, well inside BBWB's 10872 ms.
            const std::int64_t dumpAt = lastCaptureStart(timed) + 3000;

            bishop.dumpAtMs = dumpAt;
            bishop.dumpPath = outDir + "/game_test_bbwb_original.ppm";
            const swchess::game::ScriptResult original = swchess::game::runScript(bishop);

            bishop.settings.cadence = swchess::anim::Cadence::Interpolated60;
            bishop.dumpPath = outDir + "/game_test_bbwb_interpolated.ppm";
            const swchess::game::ScriptResult enhanced = swchess::game::runScript(bishop);

            check(original.dumpCapture == "BBWB" && enhanced.dumpCapture == "BBWB",
                  "both runs play the black bishop taking the white bishop");
            check(original.dumpCadence == swchess::anim::Cadence::Original120ms,
                  "the original run draws the authored poses");
            check(enhanced.dumpCadence == swchess::anim::Cadence::Interpolated60,
                  "the enhanced run draws the interpolated frames");
            check(original.dumpHasRecord, "the original run has an authored pose at the dump");
            check(enhanced.dumpHasFrame, "the enhanced run has an interpolated frame at the dump");
            check(enhanced.dumpFrameKind != "blank" && !enhanced.dumpFrameKind.empty(),
                  "the interpolated frame at the dump carries a picture");
            std::printf("the enhanced dump shows a %s frame\n", enhanced.dumpFrameKind.c_str());

            check(cueTrail(original) == cueTrail(enhanced),
                  "both cadences start the same cues at the same times");
            check(!cueTrail(original).empty(), "the bishop film starts at least one cue");
            check(original.endedMs == enhanced.endedMs,
                  "both cadences end the run at the same animation time");
            check(original.finalFen == enhanced.finalFen,
                  "both cadences leave the same position");

            // Toggling the cadence while the film runs neither rewinds the
            // clock nor starts a cue twice.
            swchess::game::Settings toggling;
            toggling.cadence = swchess::anim::Cadence::Interpolated60;
            swchess::game::GameSession mixed(cdDir, assetsDir, toggling);
            mixed.setWaitForInterp(true);
            std::int64_t tick = 0;
            int flips = 0;
            for (const std::string& text :
                 swchess::game::splitScript(bishopScript)) {
                std::optional<swchess::chess::Move> move =
                    mixed.position().parseLongAlgebraic(text);
                check(move.has_value(), std::string("the position allows ") + text);
                if (!move.has_value()) {
                    break;
                }
                mixed.clickSquare(move->from, tick);
                mixed.clickSquare(move->to, tick);
                for (int guard = 0; guard < 100000; ++guard) {
                    tick += 10;
                    mixed.advance(tick);
                    if (mixed.state() == swchess::game::AnimState::Capturing &&
                        (guard % 37) == 0) {
                        mixed.toggleCadence();
                        ++flips;
                    }
                    if (mixed.state() == swchess::game::AnimState::Idle) {
                        break;
                    }
                }
            }
            check(flips > 2, "the run flipped the cadence more than twice inside the film");
            check(mixed.soundPlays() == enhanced.soundPlays,
                  "flipping the cadence mid film starts no cue a second time");
            check(mixed.position().fen() == enhanced.finalFen,
                  "flipping the cadence leaves the position the moves made");
        }

        // The black knight takes the white knight on e5, which is the BNWN
        // film. Pose 16 of that film carries GRUNT2.WAV with pause=1, a sound
        // the original starts at 1920 ms and blocks on until 3132 ms. The
        // manifest and the timeline have to agree about that 1920, or the
        // player used to throw and the window had nowhere to catch it.
        {
            const std::string knightManifest =
                assetsDir + "/captures/BNWN/interp60/manifest.json";
            if (!std::filesystem::exists(knightManifest)) {
                std::printf("skip  %s is not there, so the blocking cue checks do not run\n",
                            knightManifest.c_str());
            } else {
                swchess::game::ScriptOptions knight;
                knight.cdDir = cdDir;
                knight.assetsDir = assetsDir;
                knight.settings.cadence = swchess::anim::Cadence::Interpolated60;
                knight.moves =
                    swchess::game::splitScript("e2e4 e7e5 g1f3 b8c6 f3e5 c6e5");
                knight.dumpAtMs = 20000;
                knight.dumpPath = outDir + "/game_test_bnwn_interpolated.ppm";
                const swchess::game::ScriptResult took = swchess::game::runScript(knight);
                check(took.rejected.empty(), "the rules module took every move of the knight line");
                check(took.dumped, "the knight line composited a picture at 20000 ms");
                check(took.dumpState == swchess::game::AnimState::Capturing,
                      "the knight line is playing its film at 20000 ms");
                check(took.dumpCapture == "BNWN",
                      "the film at 20000 ms is the black knight taking the white knight");
                check(took.dumpCadence == swchess::anim::Cadence::Interpolated60,
                      "BNWN draws its interpolated frames rather than falling back");
                check(took.dumpHasFrame, "an interpolated frame stands at 20000 ms");
                check(took.dumpFrameKind != "blank" && !took.dumpFrameKind.empty(),
                      "that frame carries a picture");
            }
        }

        // A capture whose interp60 folder is not there plays the authored
        // poses, and asking for the enhanced cadence changes nothing.
        const std::string bareAssets = outDir + "/game_test_no_interp";
        std::filesystem::create_directories(bareAssets + "/captures/BBWB");
        swchess::game::ScriptOptions bare;
        bare.cdDir = cdDir;
        bare.assetsDir = bareAssets;
        bare.settings.cadence = swchess::anim::Cadence::Interpolated60;
        bare.moves = swchess::game::splitScript(bishopScript);
        const swchess::game::ScriptResult noFrames = swchess::game::runScript(bare);
        check(noFrames.rejected.empty(),
              "the bishop line runs with no interpolated frames on disk");
        check(captureCount(noFrames) == 1, "the film still plays without interpolated frames");
        check(noFrames.states.back().state == swchess::game::AnimState::Idle,
              "the session waits for the next click when that film ends");
        bare.dumpAtMs = lastCaptureStart(noFrames) + 3000;
        bare.dumpPath = outDir + "/game_test_no_interp.ppm";
        const swchess::game::ScriptResult noFramesDump = swchess::game::runScript(bare);
        check(noFramesDump.dumpCadence == swchess::anim::Cadence::Original120ms,
              "a film with no interpolated frames falls back to the original cadence");
        check(noFramesDump.dumpHasRecord,
              "that film draws an authored pose");

        // The whole program: the title screens, the menu buttons, the saved
        // games and the settings file. Everything writes into its own
        // directory, so nothing here touches the player's real settings.
        const std::string shellDir = outDir + "/game_test_shell";
        std::filesystem::remove_all(shellDir);
        std::filesystem::create_directories(shellDir);

        swchess::game::ShellOptions shellOptions;
        shellOptions.cdDir = cdDir;
        shellOptions.configDir = shellDir;
        shellOptions.skipTitle = true;
        swchess::game::GameShell shell(shellOptions);
        check(shell.state() == swchess::game::ShellState::Playing,
              "--skip-title opens the game screen");

        // The bar opens on HUMAN VS. COMPUTER, so the engine would answer
        // every move the checks below play. These checks are about the saved
        // games and the settings file, so both colours go to a person.
        shell.runCommand(swchess::ui::command::kHumanHuman, 0);
        check(shell.session().seat(swchess::chess::Color::Black) ==
                  swchess::game::Seat::Human,
              "HUMAN VS. HUMAN puts a person on both colours");

        const std::string startFen = shell.session().position().fen();
        std::int64_t shellClock = 0;
        playThrough(shell.session(), "e2e4", shellClock);
        check(shell.session().position().fen() != startFen, "the shell played a move");

        // NEW GAME sits in slot 1 of page 1, which the GAME MENU button of
        // page 0 opens.
        pressButton(shell, 0);
        check(shell.bar().page() == 1, "the GAME MENU button shows page 1");
        pressButton(shell, 1);
        check(shell.session().position().fen() == startFen,
              "the NEW GAME button puts the opening position back");
        check(shell.session().state() == swchess::game::AnimState::Idle,
              "the session is idle after NEW GAME");

        // CAPTURES sits in slot 2 of page 7, which PLAY MENU then LOOK & FEEL
        // opens. The status wording offers the other state.
        shell.bar().setPage(0);
        pressButton(shell, 1);
        check(shell.bar().page() == 2, "the PLAY MENU button shows page 2");
        pressButton(shell, 1);
        check(shell.bar().page() == 7, "the LOOK & FEEL button shows page 7");
        check(shell.settings().captures == 1, "the captures setting starts on");
        check(shell.bar().statusIdFor(7, 2) == 72, "captures on shows string id 72");
        const swchess::ui::Rect capturesButton = swchess::ui::buttonRect(2);
        shell.onMouseMove(capturesButton.x + 5, capturesButton.y + 5);
        const std::string capturesOn = shell.bar().statusText();
        check(capturesOn == "CAPTURES OFF",
              "the hovered button offers to switch the films off, not " + capturesOn);
        pressButton(shell, 2);
        check(shell.settings().captures == 0, "the CAPTURES button turns the setting off");
        check(!shell.session().settings().captures, "the session stops playing the films");
        check(shell.bar().statusIdFor(7, 2) == 78, "captures off shows string id 78");
        check(shell.bar().statusText() == "CAPTURES ON",
              "the status bar now offers to switch the films back on, not " +
                  shell.bar().statusText());
        std::printf("the captures button reads %s, then %s\n", capturesOn.c_str(),
                    shell.bar().statusText().c_str());

        // A native saved game keeps the moves, so a reloaded game stands
        // exactly where it stood.
        playThrough(shell.session(), "e2e4", shellClock);
        playThrough(shell.session(), "e7e5", shellClock);
        const std::string savedFen = shell.session().position().fen();
        const std::string savePath = shellDir + "/saved.json";
        check(shell.saveGameFile(savePath), "the shell wrote a native saved game");
        shell.runCommand(swchess::ui::command::kNewGame, shellClock);
        check(shell.session().position().fen() == startFen, "the board is new again");
        check(shell.loadGameFile(savePath), "the shell read the native saved game back");
        check(shell.session().position().fen() == savedFen,
              "the loaded game stands where the saved one did");

        // The original saved game on the CD opens through readCmg.
        const std::string cmgPath = cdDir + "/STARWARS.CMG";
        check(shell.loadGameFile(cmgPath), "the shell read STARWARS.CMG");
        check(shell.session().position().fen() == startFen,
              "STARWARS.CMG stands on the opening position");

        // SAVE SETTINGS writes the seven keys the original writes.
        const std::string iniPath = shellDir + "/SWC.INI";
        check(!std::filesystem::exists(iniPath), "no settings file exists before SAVE SETTINGS");
        shell.runCommand(swchess::ui::command::kSaveSettings, shellClock);
        check(std::filesystem::exists(iniPath), "SAVE SETTINGS wrote " + iniPath);
        const std::string ini = readWholeFile(iniPath);
        check(ini.find("[look_feel]") == 0, "the file opens with [look_feel]");
        check(ini.find("captures=0") != std::string::npos,
              "the file carries the captures setting the button turned off");
        check(ini.find("walking=1") != std::string::npos, "the file carries walking=1");
        check(ini.find("play_level=464") != std::string::npos, "the file carries play_level=464");
        check(ini.find("language=") < ini.find("turn="), "language comes before turn");
        check(swchess::ui::loadSettings(iniPath) == shell.settings(),
              "the file reads back as the settings the shell holds");

        // Two engines playing each other through the script runner. Every
        // move has to be legal where it was played, and every animation has
        // to have finished by the time the run stops.
        {
            swchess::game::ScriptOptions demo;
            demo.cdDir = cdDir;
            demo.whiteSeat = swchess::game::Seat::Computer;
            demo.blackSeat = swchess::game::Seat::Computer;
            demo.enginePlies = 6;
            demo.engineSeed = 7;
            const swchess::game::ScriptResult run = swchess::game::runScript(demo);
            check(!run.engineStalled, "the engine answered every request it was given");
            check(run.engineMoves.size() == 6,
                  "the engines played six plies, not " + std::to_string(run.engineMoves.size()));
            check(run.played.empty(), "no script move was clicked, because both seats are engines");
            std::string replayFen;
            check(everyMoveLegal(run.engineMoves, &replayFen),
                  "every move the engines played was legal");
            check(replayFen == run.finalFen,
                  "replaying those moves reaches the board the run left, " + run.finalFen);
            const swchess::game::AnimState ended = run.states.back().state;
            check(ended == swchess::game::AnimState::Idle ||
                      ended == swchess::game::AnimState::GameOver,
                  std::string("the run ends with nothing animating, not on ") +
                      swchess::game::animStateName(ended));
            std::size_t walks = 0;
            for (const swchess::game::StateSample& sample : run.states) {
                if (sample.state == swchess::game::AnimState::Walking) {
                    ++walks;
                }
            }
            check(walks == 6, "each of the six moves walked its piece across the board, not " +
                                  std::to_string(walks));
            std::printf("the engines played %s\n", run.finalFen.c_str());
        }

        // The HINT, FORCE MOVE and DEMO MODE buttons against the stand-in
        // engine. Each one gets its own shell, so nothing above sees them.
        {
            swchess::game::ShellOptions engineOptions = shellOptions;
            engineOptions.configDir = shellDir + "/engine";
            swchess::game::GameShell engineShell(engineOptions);
            check(engineShell.session().seat(swchess::chess::Color::White) ==
                      swchess::game::Seat::Human,
                  "HUMAN VS. COMPUTER seats the person on White");
            check(engineShell.session().seat(swchess::chess::Color::Black) ==
                      swchess::game::Seat::Computer,
                  "and the engine on Black");

            std::int64_t clock = 0;
            const std::string openingFen = engineShell.session().position().fen();
            engineShell.runCommand(swchess::ui::command::kHint, clock);
            check(pumpUntil(engineShell, clock,
                            [&] { return engineShell.session().hintSerial() > 0; }),
                  "the HINT button got an answer from the engine");
            const std::optional<swchess::chess::Move> hint = engineShell.session().hintMove();
            check(hint.has_value(), "the hint names a move");
            check(engineShell.session().position().fen() == openingFen,
                  "HINT left the position alone");
            check(engineShell.session().game().moves().empty(), "HINT played no move");
            if (hint.has_value()) {
                const std::string named =
                    engineShell.session().position().longAlgebraic(*hint);
                const std::string bar = engineShell.bar().statusText();
                check(bar.find(named) != std::string::npos,
                      "the status bar names the hint move " + named + ", not " + bar);
            }
            engineShell.advance(clock + 2100);
            check(!engineShell.session().hintMove().has_value(),
                  "the hint stops outlining its squares after two seconds");

            // The person moves and the engine answers on its own.
            clock += 2100;
            playThrough(engineShell.session(), "e2e4", clock);
            check(pumpUntil(engineShell, clock,
                            [&] {
                                return engineShell.session().game().moves().size() == 2 &&
                                       engineShell.session().state() ==
                                           swchess::game::AnimState::Idle;
                            }),
                  "the engine answered the player's move on its own");
            check(engineShell.session().engineMoveCount() == 1,
                  "the session counted one engine move");

            // NEW GAME drops the open request, and the person still has White,
            // so nothing moves on its own afterwards.
            engineShell.runCommand(swchess::ui::command::kNewGame, clock);
            check(engineShell.session().position().fen() == openingFen,
                  "NEW GAME puts the opening position back");
            check(!engineShell.session().engineThinking(),
                  "NEW GAME dropped the engine request");
            for (int tick = 0; tick < 200; ++tick) {
                clock += 10;
                engineShell.advance(clock);
            }
            check(engineShell.session().game().moves().empty(),
                  "no engine move landed on the fresh game");

            // FORCE MOVE on the player's own turn hands White to the engine.
            const std::size_t movesBefore = engineShell.session().game().moves().size();
            engineShell.runCommand(swchess::ui::command::kForceMove, clock);
            check(engineShell.session().seat(swchess::chess::Color::White) ==
                      swchess::game::Seat::Computer,
                  "FORCE MOVE seats the engine on the colour the player was about to move");
            check(engineShell.session().seat(swchess::chess::Color::Black) ==
                      swchess::game::Seat::Human,
                  "and hands the other colour to the player");
            check(pumpUntil(engineShell, clock,
                            [&] {
                                return engineShell.session().game().moves().size() >
                                           movesBefore &&
                                       engineShell.session().state() ==
                                           swchess::game::AnimState::Idle;
                            }),
                  "FORCE MOVE made the engine play");
            check(engineShell.session().game().moves().size() == movesBefore + 1,
                  "FORCE MOVE played exactly one move");
            std::vector<std::string> forced;
            for (const swchess::chess::Move& move : engineShell.session().game().moves()) {
                forced.push_back(swchess::chess::squareName(move.from) +
                                 swchess::chess::squareName(move.to));
            }
            std::string forcedFen;
            check(everyMoveLegal(forced, &forcedFen), "the forced move was legal");
            check(forcedFen == engineShell.session().position().fen(),
                  "the board after FORCE MOVE is the board those moves reach");

            // DEMO MODE puts the engine on both colours and the next button
            // press puts the player's own pairing back.
            engineShell.runCommand(swchess::ui::command::kNewGame, clock);
            engineShell.runCommand(swchess::ui::command::kDemoMode, clock);
            check(engineShell.inDemoMode(), "DEMO MODE is running");
            check(engineShell.session().seat(swchess::chess::Color::White) ==
                      swchess::game::Seat::Computer,
                  "DEMO MODE seats the engine on White, so it opens the game");
            check(pumpUntil(engineShell, clock,
                            [&] {
                                return engineShell.session().game().moves().size() == 2 &&
                                       engineShell.session().state() ==
                                           swchess::game::AnimState::Idle;
                            }),
                  "the two engines played the first two moves of the demo");
            engineShell.runCommand(swchess::ui::command::kShowCaptured, clock);
            check(!engineShell.inDemoMode(), "a button press ends the demo");
            check(engineShell.session().seat(swchess::chess::Color::Black) ==
                      swchess::game::Seat::Computer,
                  "the pairing DEMO MODE interrupted comes back");

            // The five play level buttons reach the engine.
            engineShell.runCommand(swchess::ui::command::kLevelNewcomer, clock);
            check(engineShell.engine() != nullptr &&
                      engineShell.engine()->level() == swchess::engine::Level::Newcomer,
                  "the NEWCOMER button set the engine level");
            engineShell.runCommand(swchess::ui::command::kLevelExpert, clock);
            check(engineShell.engine() != nullptr &&
                      engineShell.engine()->level() == swchess::engine::Level::Expert,
                  "the EXPERT button set the engine level");
        }

        // The title sequence runs the four launch screens and then hands over
        // to the game screen.
        swchess::game::ShellOptions titled = shellOptions;
        titled.skipTitle = false;
        swchess::game::GameShell titleShell(titled);
        titleShell.start(0);
        check(titleShell.state() == swchess::game::ShellState::Title,
              "the program starts on the title sequence");

        std::vector<std::string> order;
        auto note = [&]() {
            const std::string now =
                titleShell.title() != nullptr
                    ? std::to_string(static_cast<int>(titleShell.title()->state()))
                    : std::string(swchess::game::shellStateName(titleShell.state()));
            if (order.empty() || order.back() != now) {
                order.push_back(now);
            }
        };
        note();
        for (std::int64_t clock = 0; clock <= 80000; clock += 500) {
            titleShell.advance(clock);
            note();
        }
        const std::vector<std::string> wanted{"0", "1", "2", "3", "playing"};
        check(order == wanted, "the title runs the logo, the legal notice, the crawl and the "
                               "title, then the game screen");
        check(titleShell.state() == swchess::game::ShellState::Playing,
              "the title sequence ends on the game screen");

        // The crawl starts the theme, and the mixer is what plays it.
        swchess::game::GameShell musicShell(titled);
        musicShell.start(0);
        // The sequence takes one screen per call, so the clock steps the way
        // the window's own loop steps it.
        for (std::int64_t clock = 0; clock <= 6500; clock += 500) {
            musicShell.advance(clock);
        }
        check(musicShell.title() != nullptr &&
                  musicShell.title()->state() == swchess::ui::TitleState::Crawl,
              "the crawl is the screen showing at 6500 ms");
        check(musicShell.session().mixer().activeVoices(swchess::audio::Channel::Music) > 0,
              "the opening crawl starts the theme through the mixer");

        // The whole window picture is the size the original window had.
        swchess::Image window;
        titleShell.render(window);
        check(window.width == 674 && window.height == 512,
              "the shell draws a 674 by 512 window");

        // QUIT OK does not close at once. It runs the credit roll first, and
        // the program ends when that roll finishes.
        titleShell.runCommand(swchess::ui::command::kQuitOk, 80000);
        check(titleShell.state() == swchess::game::ShellState::Credits,
              "QUIT OK starts the credit roll");
        check(titleShell.title() != nullptr &&
                  titleShell.title()->state() == swchess::ui::TitleState::Credits,
              "the credit roll is the screen showing");
        titleShell.advance(80000 + 140000 + 1000);
        check(titleShell.finished(), "the program ends when the credit roll ends");
        titleShell.render(window);
        check(window.width == 674 && window.height == 512,
              "the shell draws a 674 by 512 window");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL threw: %s\n", error.what());
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::printf("game_test passed\n");
    return 0;
}
