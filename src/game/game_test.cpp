// Plays whole games through GameSession with a simulated clock and checks
// what the state machine, the rules module, the sound scheduler and the
// compositor did. Run it with the CD directory and a directory it may write
// pictures into.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include "anim/capture.h"
#include "assets/anx.h"
#include "game/script.h"
#include "game/session.h"

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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: game_test <cd directory> [output directory]\n");
        return 2;
    }
    const std::string cdDir = argv[1];
    const std::string outDir = argc > 2 ? argv[2] : std::string(".");

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
        // timeline lists it.
        const swchess::anim::CaptureTimeline queen = swchess::anim::loadCapture(cdDir, "WQBP");
        std::map<std::string, int> expected;
        for (const swchess::anim::CapturePose& pose : queen.poses) {
            if (pose.hasSound && pose.sound.resolved) {
                ++expected[pose.sound.resource];
            }
        }
        if (queen.hasEndSound && queen.endSound.resolved) {
            ++expected[queen.endSound.resource];
        }
        check(!expected.empty(), "WQBP carries at least one sound cue");
        check(mated.soundPlays == expected,
              "every cue of WQBP started once for each time the timeline lists it");

        // A plain capture in the middle of a game: the white e pawn takes the
        // black d pawn, and the session goes back to waiting for a click.
        swchess::game::ScriptOptions pawn;
        pawn.cdDir = cdDir;
        pawn.moves = swchess::game::splitScript("e2e4 d7d5 e4d5");
        pawn.dumpAtMs = 3000;
        pawn.dumpPath = outDir + "/game_test_capture.ppm";
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
              "3000 ms into the run the session is playing a capture");
        check(took.dumpCapture == "WPBP",
              "the film is the white pawn taking the black pawn");
        check(took.dumpHasPose, "a film frame is on the screen at 3000 ms");

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
        check(silent.soundPlays.empty(), "no cue fires when captures are off");

        // Turning walking off slides the piece instead, which still commits
        // the same moves.
        swchess::game::ScriptOptions sliding = quiet;
        sliding.settings.walking = false;
        const swchess::game::ScriptResult slid = swchess::game::runScript(sliding);
        check(slid.finalFen == took.finalFen, "sliding reaches the same position as walking");

        // Undo during Idle puts the position back.
        swchess::game::GameSession session(cdDir, swchess::game::Settings{});
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
        swchess::game::GameSession skipper(cdDir, swchess::game::Settings{});
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
