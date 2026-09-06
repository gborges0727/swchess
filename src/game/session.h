// One playable game of Star Wars Chess: the rules, the board it is drawn on,
// and the animation that carries a piece from one square to another.
//
// GameSession owns a chess::Game and a small state machine. A click picks a
// piece, a second click picks a square, and the session commits the move to
// the rules module once and then plays the animation that shows it. Nothing
// here opens a window. The session composites one 640 by 480 picture per
// call, so the SDL window and the headless PPM dump draw the same pixels.
//
// docs/research/board-geometry.md describes the walk this follows, and
// docs/research/capture-player.md describes the capture.
#pragma once

#include <array>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "anim/capture.h"
#include "anim/interp.h"
#include "anim/player.h"
#include "anim/walk.h"
#include "assets/bmp.h"
#include "audio/audio.h"
#include "board/board_view.h"
#include "chess.h"
#include "text/font.h"
#include "text/strings.h"

namespace swchess::game {

// Where the session stands.
//
// Idle waits for a click. Walking carries the moving piece along the screen
// line between the two squares. Capturing plays the .ANX film of the attacker
// killing the defender. Promoting waits for the player to name the piece a
// pawn turns into. GameOver is checkmate, stalemate or a draw.
enum class AnimState { Idle, Walking, Capturing, Promoting, GameOver };

const char* animStateName(AnimState state);

// What the player picked, all of it changeable while the game runs.
struct Settings {
    board::SetId set = board::SetId::WhiteBottom;
    text::Language language = text::Language::English;
    bool walking = true;   // off slides the piece with no walk frames
    bool captures = true;  // off skips the capture film
    // Which background bitmap stands behind the board. 0 draws THRON256.BMP
    // and 1 draws SPACE256.BMP, the two the CHANGE BOARD button picks between.
    // -1 keeps whatever CMWIN.DAT names.
    int background = -1;
    // Which pictures a capture draws. The enhanced 60 frames per second
    // pictures are the default, and a capture with none on disk plays the
    // authored poses instead. Nothing writes this to a file, because the
    // session keeps every other setting in memory too.
    anim::Cadence cadence = anim::Cadence::Interpolated60;
};

// The four pieces a promotion offers, in the order the panel shows them.
extern const chess::PieceType kPromotionChoices[4];

// The piece DLL of one character, such as "LS" for the white king.
const char* pieceDllCode(chess::Color color, chess::PieceType type);

// The capture code of one fight, such as "WPBP". The letters are the
// attacker's colour and piece then the defender's colour and piece, which is
// the naming rule in section 1 of the capture research note.
std::string captureCode(chess::Piece attacker, chess::Piece defender);

class GameSession {
public:
    // Reads the board, the sheet, the background, the font and the sounds out
    // of `cdDir`, and the interpolated capture frames out of `assetsDir`. An
    // empty `assetsDir` turns the enhanced cadence off for every capture.
    // Throws std::runtime_error when a CD file is missing.
    GameSession(std::string cdDir, std::string assetsDir, Settings settings);

    // Moves the animation clock to `nowMs` and finishes whatever came due.
    void advance(std::int64_t nowMs);

    // A click on one screen pixel. It skips a running capture, and otherwise
    // selects a piece or plays a move. Returns true when something changed.
    bool clickPixel(int px, int py, std::int64_t nowMs);

    // The same by square, which is what the script runner uses.
    bool clickSquare(chess::Square square, std::int64_t nowMs);

    // Names the piece a promoting pawn turns into. Returns false when the
    // session is not waiting for one.
    bool choosePromotion(chess::PieceType type, std::int64_t nowMs);

    // Ends a running capture now. Any click and any key does this. Returns
    // true when a capture was running.
    bool skipCapture(std::int64_t nowMs);

    void newGame();
    // Takes back the last move. It does nothing while an animation runs.
    bool undo();

    // Puts a whole game on the board, which is how a loaded file arrives. It
    // stops any animation and shows the position the game stands at.
    void setGame(const chess::Game& game);

    void setSet(board::SetId set);
    // Picks the background bitmap. 0 draws THRON256.BMP and 1 draws
    // SPACE256.BMP, and -1 goes back to the one CMWIN.DAT names.
    void setBackground(int background);
    void setLanguage(text::Language language);
    void cycleLanguage();
    void setWalking(bool on) { settings_.walking = on; }
    void setCaptures(bool on) { settings_.captures = on; }

    // Picks the cadence. A capture already on the screen switches pictures
    // where it stands, so the animation clock does not move and no cue plays
    // twice. Asking for the enhanced cadence when this capture has no
    // interpolated frames leaves the authored poses on the screen and keeps
    // the preference for the next capture.
    void setCadence(anim::Cadence cadence);
    void toggleCadence();
    // What the next capture will try to use.
    anim::Cadence cadence() const { return settings_.cadence; }
    // What the capture on the screen is drawing right now.
    anim::Cadence activeCadence() const { return capturePlayer_.cadence(); }

    // Waits for the interpolated frames instead of starting the capture
    // without them. The window never does this, because a stall would show.
    // The script runner does, because its clock is simulated and a load that
    // finishes late would make the same script draw different pictures.
    void setWaitForInterp(bool on) { waitForInterp_ = on; }

    // Draws the whole frame: the background, the highlights, the pieces, the
    // walker, the capture film and the status line.
    void render(Image& out);

    AnimState state() const { return state_; }
    const Settings& settings() const { return settings_; }
    const chess::Game& game() const { return game_; }
    const chess::Position& position() const { return game_.position(); }
    const board::BoardScene& scene() const { return scene_; }

    std::optional<chess::Square> selection() const { return selected_; }
    // The squares the selected piece may move to.
    const std::vector<chess::Square>& highlights() const { return highlights_; }

    // The capture being played, empty when none is.
    const std::string& captureName() const { return captureName_; }
    // The film frame on the screen right now.
    const anim::DrawState& captureDraw() const { return captureDraw_; }

    // One cue that started, with the animation time it started at.
    struct SoundPlay {
        std::string name;
        std::int64_t timeMs = 0;
    };

    // How often each WAVE resource started, keyed by resource name.
    const std::map<std::string, int>& soundPlays() const { return soundPlays_; }
    // Every cue that started, in the order they started.
    const std::vector<SoundPlay>& soundLog() const { return soundLog_; }

    // What the side to move, a check, a mate, a stalemate or a draw reads as,
    // in the stored bytes of the current language. The button bar draws it.
    std::string statusBytes() const;

    // One string of the active language, as stored bytes. An id the file
    // leaves empty comes back empty.
    std::string stringBytes(int id) const;

    // One sound of SWCAUDIO.DLL, or null when the file holds no such name.
    const audio::Clip* clip(const std::string& name) const;

    audio::Mixer& mixer() { return mixer_; }

private:
    // One piece walking from one square to another. A castling move makes two,
    // the king first and then the rook.
    struct Leg {
        chess::Square from{};
        chess::Square to{};
        chess::Piece piece{};
        const anim::WalkSequence* walk = nullptr;
        int fromDepth = 0;
        int toDepth = 0;
    };

    void rebuildScene();
    // Swaps in the background bitmap settings_.background names.
    void applyBackground();
    void resetDisplay();
    void clearSelection();
    bool beginMove(chess::Move move, std::int64_t nowMs);
    void startLeg(std::size_t index, std::int64_t nowMs);
    void finishLeg();
    void startCapture(std::int64_t nowMs);
    void finishMove();
    // Reads the interpolated frames of `captureName` on a worker thread while
    // the piece walks.
    void startInterpLoad(const std::string& captureName);
    // Takes the loaded frames, or nothing when the worker is still reading.
    void collectInterp();

    const anim::WalkSequence* walkFor(chess::Piece piece, chess::Square from, chess::Square to);
    const std::vector<std::uint8_t>& walkFrameRgba(const PieceBitmap* bitmap) const;
    board::ProjectedPoint anchorOf(chess::Square square) const;

    void drawHighlights(Image& out) const;
    void drawSquareOutline(Image& out, chess::Square square, std::uint8_t r, std::uint8_t g,
                           std::uint8_t b, std::uint8_t a) const;
    void drawPieces(Image& out) const;

    std::string cdDir_;
    std::string assetsDir_;
    Settings settings_{};
    chess::Game game_{};
    board::BoardScene scene_{};

    AnimState state_ = AnimState::Idle;
    std::int64_t nowMs_ = 0;

    // What stands on the board right now. It is not the rules position while
    // an animation runs, because the mover is in the air and the piece it is
    // about to kill is still standing.
    std::array<std::optional<chess::Piece>, 64> display_{};
    std::optional<chess::Square> hidden_;  // the defender, hidden during its capture

    std::optional<chess::Square> selected_;
    std::vector<chess::Square> highlights_;
    // The move a promotion is waiting on.
    std::optional<chess::Square> promotionFrom_;
    std::optional<chess::Square> promotionTo_;

    std::vector<Leg> legs_;
    std::size_t leg_ = 0;
    anim::WalkPlayer walker_;
    anim::WalkDraw walkDraw_{};

    // The capture this move ends in, if any.
    bool moveCaptures_ = false;
    chess::Piece attacker_{};
    chess::Piece defender_{};
    chess::Square defenderSquare_{};
    std::string captureName_;
    std::unique_ptr<anim::CaptureTimeline> timeline_;
    anim::CapturePlayer capturePlayer_;
    anim::DrawState captureDraw_{};
    // The 60 frames per second pictures of the capture on the screen, and the
    // worker reading the next ones.
    std::optional<anim::InterpSequence> interp_;
    std::future<std::optional<anim::InterpSequence>> interpLoad_;
    bool interpLoading_ = false;
    bool waitForInterp_ = false;

    audio::Mixer mixer_;
    std::map<std::string, audio::Clip> clips_;
    audio::CueScheduler scheduler_;
    std::vector<std::string> cueNames_;  // one per cue, in the scheduler's order
    std::size_t cuesReported_ = 0;
    std::map<std::string, int> soundPlays_;
    std::vector<SoundPlay> soundLog_;

    text::BitmapFont font_{};
    std::map<int, text::StringTable> strings_;
    // The walk sequences already read, keyed by piece code and direction.
    std::map<std::string, anim::WalkSequence> walks_;
    // One RGBA copy of every walk frame drawn so far.
    mutable std::map<const PieceBitmap*, std::vector<std::uint8_t>> frameCache_;
};

}  // namespace swchess::game
