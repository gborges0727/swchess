#include "game/script.h"

#include <algorithm>
#include <chrono>
#include <memory>

#include "render/compositor.h"

namespace swchess::game {
namespace {

// One move as the script writes them, such as "e2e4" or "e7e8q".
std::string longAlgebraicOf(const chess::Move& move) {
    std::string text = chess::squareName(move.from) + chess::squareName(move.to);
    if (move.promotion.has_value()) {
        const char letter =
            chess::pieceLetter(chess::Piece{chess::Color::Black, *move.promotion});
        text += letter;
    }
    return text;
}

}  // namespace

std::vector<std::string> splitScript(const std::string& text) {
    std::vector<std::string> moves;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
            if (!current.empty()) {
                moves.push_back(current);
                current.clear();
            }
            continue;
        }
        current += c;
    }
    if (!current.empty()) {
        moves.push_back(current);
    }
    return moves;
}

ScriptResult runScript(const ScriptOptions& options) {
    ScriptResult result;
    GameSession session(options.cdDir, options.assetsDir, options.settings);
    session.setWaitForInterp(options.waitForInterp);

    // A computer seat needs an engine behind it. The random stand-in is the
    // one every test uses, and it lives as long as the session does.
    std::unique_ptr<engine::Engine> engine;
    if (options.whiteSeat == Seat::Computer || options.blackSeat == Seat::Computer) {
        engine::Config config;
        config.cdDir = options.cdDir;
        config.level = options.level;
        engine = engine::makeRandomEngine(config, options.engineSeed);
        session.setEngine(engine.get());
        session.setSeat(chess::Color::White, options.whiteSeat);
        session.setSeat(chess::Color::Black, options.blackSeat);
    }

    std::size_t next = 0;         // the script move waiting to be played
    std::string playing;          // the move on the board right now
    AnimState last = session.state();
    result.states.push_back(StateSample{0, last, std::string()});

    std::int64_t now = 0;
    bool dumpPending = options.dumpAtMs >= 0;

    // Where the recorded move list has been read up to. Anything past it that
    // the session played on its own came from the engine.
    std::size_t seen = session.game().moves().size();
    std::chrono::steady_clock::time_point waitStart = std::chrono::steady_clock::now();

    auto takeDump = [&]() {
        result.dumped = true;
        result.dumpState = session.state();
        result.dumpCapture = session.captureName();
        const anim::DrawState& draw = session.captureDraw();
        result.dumpCadence = session.activeCadence();
        result.dumpHasRecord = draw.visible && draw.record != nullptr;
        result.dumpHasFrame = draw.visible && draw.frame != nullptr;
        result.dumpHasPose = result.dumpHasRecord || result.dumpHasFrame;
        if (result.dumpHasPose) {
            result.dumpPoseIndex = draw.poseIndex;
            result.dumpX = draw.x;
            result.dumpY = draw.y;
            result.dumpWidth = draw.width;
            result.dumpHeight = draw.height;
        }
        if (result.dumpHasRecord) {
            result.dumpRecordOffset = draw.record->offset;
        }
        if (result.dumpHasFrame) {
            result.dumpFrameKind = anim::interpKindName(draw.frame->kind);
        }
        session.render(result.dump);
        if (options.decorate) {
            options.decorate(session, result.dump);
        }
        if (!options.dumpPath.empty()) {
            writePPM(result.dump, options.dumpPath);
        }
        dumpPending = false;
    };

    auto note = [&]() {
        if (session.state() != last) {
            last = session.state();
            result.states.push_back(StateSample{now, last, playing});
        }
    };

    while (now <= options.limitMs) {
        session.advance(now);
        note();

        const std::vector<chess::Move>& allMoves = session.game().moves();
        while (seen < allMoves.size()) {
            result.engineMoves.push_back(longAlgebraicOf(allMoves[seen]));
            ++seen;
        }
        if (options.enginePlies >= 0 && session.engineMoveCount() >= options.enginePlies) {
            // The engine has played its share, so both colours go back to a
            // person and the script drives the rest.
            session.setSeat(chess::Color::White, Seat::Human);
            session.setSeat(chess::Color::Black, Seat::Human);
        }

        if (dumpPending && now >= options.dumpAtMs) {
            takeDump();
        }

        // Thinking is not something the board draws, so the animation clock
        // stands still until the engine answers.
        if (session.state() == AnimState::Idle && session.engineThinking()) {
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - waitStart);
            if (waited.count() > options.engineWaitMs) {
                result.engineStalled = true;
                break;
            }
            continue;
        }
        waitStart = std::chrono::steady_clock::now();

        const bool idle = session.state() == AnimState::Idle;
        if (idle && !session.engineToMove() && next < options.moves.size()) {
            const std::string& text = options.moves[next];
            std::optional<chess::Move> move = session.position().parseLongAlgebraic(text);
            if (!move.has_value()) {
                result.rejected = text;
                break;
            }
            playing = text;
            session.clickSquare(move->from, now);
            session.clickSquare(move->to, now);
            if (session.state() == AnimState::Promoting) {
                // Record the pause for a promotion before answering it, so the
                // caller sees the state the player would have seen.
                note();
                session.choosePromotion(move->promotion.value_or(chess::PieceType::Queen), now);
            }
            if (session.state() == AnimState::Idle) {
                result.rejected = text;
                break;
            }
            result.played.push_back(text);
            ++next;
            seen = session.game().moves().size();
            note();
        }

        const bool engineHasMore =
            session.state() != AnimState::GameOver &&
            (session.engineThinking() || session.engineToMove());
        const bool done = next >= options.moves.size() && !engineHasMore &&
                          (session.state() == AnimState::Idle ||
                           session.state() == AnimState::GameOver);
        if (done) {
            if (dumpPending) {
                // Nothing is moving any more, so the picture at the dump time
                // is the picture standing on the screen now.
                now = std::max(now, options.dumpAtMs);
                session.advance(now);
                takeDump();
            }
            break;
        }

        std::int64_t step = now + options.stepMs;
        if (dumpPending && options.dumpAtMs > now && options.dumpAtMs < step) {
            step = options.dumpAtMs;
        }
        now = step;
    }

    result.endedMs = now;
    result.finalFen = session.position().fen();
    result.soundPlays = session.soundPlays();
    result.soundLog = session.soundLog();
    return result;
}

}  // namespace swchess::game
