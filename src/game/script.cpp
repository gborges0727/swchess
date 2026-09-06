#include "game/script.h"

#include <algorithm>

#include "render/compositor.h"

namespace swchess::game {

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
    GameSession session(options.cdDir, options.settings);

    std::size_t next = 0;         // the script move waiting to be played
    std::string playing;          // the move on the board right now
    AnimState last = session.state();
    result.states.push_back(StateSample{0, last, std::string()});

    std::int64_t now = 0;
    bool dumpPending = options.dumpAtMs >= 0;

    auto takeDump = [&]() {
        result.dumped = true;
        result.dumpState = session.state();
        result.dumpCapture = session.captureName();
        const anim::DrawState& draw = session.captureDraw();
        result.dumpHasPose = draw.visible && draw.record != nullptr;
        if (result.dumpHasPose) {
            result.dumpPoseIndex = draw.poseIndex;
            result.dumpRecordOffset = draw.record->offset;
            result.dumpX = draw.x;
            result.dumpY = draw.y;
            result.dumpWidth = draw.width;
            result.dumpHeight = draw.height;
        }
        session.render(result.dump);
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

        if (dumpPending && now >= options.dumpAtMs) {
            takeDump();
        }

        const bool idle = session.state() == AnimState::Idle;
        if (idle && next < options.moves.size()) {
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
            note();
        }

        const bool done = next >= options.moves.size() &&
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
    return result;
}

}  // namespace swchess::game
