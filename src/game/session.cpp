#include "game/session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "assets/anx.h"
#include "assets/wav.h"
#include "render/compositor.h"

namespace swchess::game {
namespace {

// The string ids the status line draws, read off tools/fonts/strings.csv.
constexpr int kIdWhiteToMove = 83;
constexpr int kIdBlackToMove = 84;
constexpr int kIdDraw = 2088;
constexpr int kIdCheck = 32776;
constexpr int kIdBlackMated = 32777;
constexpr int kIdWhiteMated = 32778;
constexpr int kIdStalemate = 32779;

// Where the promotion panel stands.
constexpr int kPanelX = 8;
constexpr int kPanelGap = 8;

// How far the walker's depth moves so it passes the stationary pieces.
// FUN_1068_0fe6 biases by 10 either way.
constexpr int kWalkDepthBias = 10;

int squareIndex(chess::Square square) { return square.rank * 8 + square.file; }

// The English text the status line falls back to when a language file has no
// entry for the id.
const char* fallbackText(int id) {
    switch (id) {
        case kIdWhiteToMove: return "WHITE TO MOVE";
        case kIdBlackToMove: return "BLACK TO MOVE";
        case kIdDraw: return "Game ended in a draw";
        case kIdCheck: return "Check!";
        case kIdBlackMated: return "Black was mated!";
        case kIdWhiteMated: return "White was mated!";
        case kIdStalemate: return "Stalemate!";
        default: break;
    }
    return "";
}

const SheetCell& cellAt(const PieceSheet& sheet, int row, int column) {
    for (const SheetCell& cell : sheet.cells) {
        if (cell.row == row && cell.column == column) {
            return cell;
        }
    }
    throw std::runtime_error("the sheet has no cell at that row and column");
}

// Draws one straight line with alpha, used for the highlight outlines.
void blendLine(Image& out, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g,
               std::uint8_t b, std::uint8_t a) {
    for (const anim::PathPoint& point : anim::linePoints(x0, y0, x1, y1)) {
        if (point.x < 0 || point.y < 0 || point.x >= out.width || point.y >= out.height) {
            continue;
        }
        std::uint8_t* pixel =
            out.rgba.data() + (static_cast<std::size_t>(point.y) * out.width + point.x) * 4;
        pixel[0] = static_cast<std::uint8_t>((pixel[0] * (255 - a) + r * a) / 255);
        pixel[1] = static_cast<std::uint8_t>((pixel[1] * (255 - a) + g * a) / 255);
        pixel[2] = static_cast<std::uint8_t>((pixel[2] * (255 - a) + b * a) / 255);
    }
}

// One sprite waiting to be drawn, sorted the way FUN_1018_0ae3 sorts them.
struct Sprite {
    const std::uint8_t* rgba = nullptr;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    int depth = 0;
};

void insertByDepth(std::vector<Sprite>& sprites, const Sprite& sprite) {
    std::size_t at = 0;
    while (at < sprites.size() && sprites[at].depth >= sprite.depth) {
        ++at;
    }
    sprites.insert(sprites.begin() + static_cast<std::ptrdiff_t>(at), sprite);
}

}  // namespace

const chess::PieceType kPromotionChoices[4] = {
    chess::PieceType::Queen,
    chess::PieceType::Rook,
    chess::PieceType::Bishop,
    chess::PieceType::Knight,
};

const char* animStateName(AnimState state) {
    switch (state) {
        case AnimState::Idle: return "idle";
        case AnimState::Walking: return "walking";
        case AnimState::Capturing: return "capturing";
        case AnimState::Promoting: return "promoting";
        case AnimState::GameOver: break;
    }
    return "game_over";
}

const char* pieceDllCode(chess::Color color, chess::PieceType type) {
    // The prefixes at file offset 0x36d1e, indexed as color * 6 + piece:
    // LS LO YO C3 CB R2 for white and EM DV AT BF SP ST for black.
    static const char* const kCodes[12] = {"LS", "LO", "YO", "C3", "CB", "R2",
                                           "EM", "DV", "AT", "BF", "SP", "ST"};
    return kCodes[static_cast<int>(color) * 6 + static_cast<int>(type)];
}

std::string captureCode(chess::Piece attacker, chess::Piece defender) {
    static const char kLetters[6] = {'K', 'Q', 'R', 'B', 'N', 'P'};
    std::string code;
    code += attacker.color == chess::Color::White ? 'W' : 'B';
    code += kLetters[static_cast<int>(attacker.type)];
    code += defender.color == chess::Color::White ? 'W' : 'B';
    code += kLetters[static_cast<int>(defender.type)];
    return code;
}

GameSession::GameSession(std::string cdDir, std::string assetsDir, Settings settings)
    : cdDir_(std::move(cdDir)), assetsDir_(std::move(assetsDir)), settings_(settings) {
    rebuildScene();
    font_ = text::loadGuiFont(cdDir_);
    setLanguage(settings_.language);
    for (const WaveResource& resource : loadAudioDll(cdDir_)) {
        audio::Clip clip;
        clip.spec.channels = resource.sound.channels > 0 ? resource.sound.channels : 1;
        clip.spec.freq =
            resource.sound.sampleRate > 0 ? static_cast<int>(resource.sound.sampleRate) : 22050;
        clip.spec.format = resource.sound.bitsPerSample == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
        clip.pcm.resize(resource.sound.samples.size());
        std::memcpy(clip.pcm.data(), resource.sound.samples.data(),
                    resource.sound.samples.size());
        clips_.emplace(resource.name, std::move(clip));
    }
    scheduler_.setMixer(&mixer_);
    resetDisplay();
}

void GameSession::rebuildScene() {
    scene_ = board::loadBoardScene(cdDir_, settings_.set);
    if (settings_.background >= 0) {
        applyBackground();
    }
}

void GameSession::applyBackground() {
    scene_.settings.background = settings_.background;
    scene_.backgroundSource = board::backgroundName(settings_.set, scene_.settings) + ".BMP";
    scene_.background = loadBmp(cdDir_ + "/" + scene_.backgroundSource);
}

void GameSession::setBackground(int background) {
    settings_.background = background;
    if (background < 0) {
        rebuildScene();
        return;
    }
    applyBackground();
}

void GameSession::resetDisplay() {
    for (std::optional<chess::Piece>& slot : display_) {
        slot.reset();
    }
    const chess::Position& position = game_.position();
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            display_[squareIndex(chess::Square{file, rank})] = position.at(chess::Square{file, rank});
        }
    }
    hidden_.reset();
}

void GameSession::clearSelection() {
    selected_.reset();
    highlights_.clear();
}

void GameSession::setSet(board::SetId set) {
    settings_.set = set;
    rebuildScene();
}

void GameSession::setLanguage(text::Language language) {
    settings_.language = language;
    const int key = static_cast<int>(language);
    if (strings_.find(key) == strings_.end()) {
        strings_.emplace(key, text::loadStrings(cdDir_, language));
    }
}

void GameSession::cycleLanguage() {
    const int next = (static_cast<int>(settings_.language) + 1) % 4;
    setLanguage(static_cast<text::Language>(next));
}

void GameSession::setCadence(anim::Cadence cadence) {
    settings_.cadence = cadence;
    if (state_ != AnimState::Capturing) {
        return;
    }
    // The player refuses the enhanced cadence when this capture has no
    // interpolated frames. The preference still changes, so the next capture
    // that has them uses them.
    if (cadence == anim::Cadence::Interpolated60 && capturePlayer_.interp() == nullptr) {
        return;
    }
    capturePlayer_.setCadence(cadence);
    // The next advance draws the other cadence's picture at the same
    // animation time. Nothing else moves, and no cue fires again.
}

void GameSession::toggleCadence() {
    setCadence(settings_.cadence == anim::Cadence::Interpolated60
                   ? anim::Cadence::Original120ms
                   : anim::Cadence::Interpolated60);
}

void GameSession::startInterpLoad(const std::string& captureName) {
    interp_.reset();
    interpLoading_ = false;
    if (assetsDir_.empty()) {
        return;
    }
    // The read decodes about 650 PNGs, which takes tens of milliseconds. It
    // runs while the piece walks so the capture starts without a stall.
    interpLoad_ = std::async(std::launch::async, [dir = assetsDir_, captureName]() {
        return anim::loadInterp(dir, captureName);
    });
    interpLoading_ = true;
}

void GameSession::collectInterp() {
    interp_.reset();
    if (!interpLoading_) {
        return;
    }
    if (!waitForInterp_ &&
        interpLoad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        // The walk ended first. The capture plays the authored poses rather
        // than waiting, and the worker's result is dropped when it arrives.
        interpLoading_ = false;
        return;
    }
    interpLoading_ = false;
    try {
        interp_ = interpLoad_.get();
    } catch (const std::exception&) {
        // A manifest that is there but unreadable is not worth stopping the
        // game for. The capture plays the authored poses.
        interp_.reset();
    }
}

void GameSession::newGame() {
    mixer_.stopAll();
    capturePlayer_.start(nullptr, 0);
    timeline_.reset();
    interp_.reset();
    interpLoading_ = false;
    captureName_.clear();
    captureDraw_ = anim::DrawState{};
    legs_.clear();
    leg_ = 0;
    moveCaptures_ = false;
    game_.reset();
    clearSelection();
    promotionFrom_.reset();
    promotionTo_.reset();
    resetDisplay();
    state_ = AnimState::Idle;
}

void GameSession::setGame(const chess::Game& game) {
    newGame();
    game_ = game;
    resetDisplay();
    state_ = game_.result() == chess::GameResult::Ongoing ? AnimState::Idle : AnimState::GameOver;
}

bool GameSession::undo() {
    if (state_ != AnimState::Idle && state_ != AnimState::GameOver) {
        return false;
    }
    if (game_.moves().empty()) {
        return false;
    }
    game_.undo();
    clearSelection();
    resetDisplay();
    state_ = game_.result() == chess::GameResult::Ongoing ? AnimState::Idle : AnimState::GameOver;
    return true;
}

board::ProjectedPoint GameSession::anchorOf(chess::Square square) const {
    return scene_.geometry.squareCenter(square.file, square.rank);
}

const anim::WalkSequence* GameSession::walkFor(chess::Piece piece, chess::Square from,
                                               chess::Square to) {
    if (!settings_.walking) {
        return nullptr;
    }
    const std::string code = pieceDllCode(piece.color, piece.type);
    const bool turned = board::setOrientation(settings_.set) == board::Orientation::WhiteTop;
    const anim::Direction wanted =
        anim::walkDirection(to.file - from.file, to.rank - from.rank, turned);

    // Several characters walk along four axes only, so a diagonal move falls
    // back to the nearest heading that has frames. FUN_1068_019d turns by the
    // shorter way round, and this picks its neighbours in the same order.
    static const int kOrder[8] = {0, 1, -1, 2, -2, 3, -3, 4};
    for (int offset : kOrder) {
        const int index = (static_cast<int>(wanted) + offset + 8) % 8;
        const anim::Direction direction = static_cast<anim::Direction>(index);
        const std::string key = code + "/" + anim::directionName(direction);
        auto found = walks_.find(key);
        if (found == walks_.end()) {
            found = walks_.emplace(key, anim::loadWalk(cdDir_, code, direction)).first;
        }
        if (!found->second.steps.empty()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool GameSession::clickPixel(int px, int py, std::int64_t nowMs) {
    if (state_ == AnimState::Capturing) {
        return skipCapture(nowMs);
    }
    if (state_ == AnimState::Promoting) {
        const int cellW = scene_.sheet.cellWidth;
        const int cellH = scene_.sheet.cellHeight;
        const int top = anim::kCanvasHeight - cellH - 4;
        for (int i = 0; i < 4; ++i) {
            const int left = kPanelX + i * (cellW + kPanelGap);
            if (px >= left && px < left + cellW && py >= top && py < top + cellH) {
                return choosePromotion(kPromotionChoices[i], nowMs);
            }
        }
        return false;
    }
    std::optional<chess::Square> square = board::hitTest(scene_.geometry, px, py);
    if (!square.has_value()) {
        return false;
    }
    return clickSquare(*square, nowMs);
}

bool GameSession::clickSquare(chess::Square square, std::int64_t nowMs) {
    if (state_ == AnimState::Capturing) {
        return skipCapture(nowMs);
    }
    if (state_ != AnimState::Idle) {
        return false;
    }
    const chess::Position& position = game_.position();

    // A click on a destination plays the move. Anything else that is not one
    // of the side's own pieces does nothing at all.
    if (selected_.has_value()) {
        std::vector<chess::Move> matches;
        for (const chess::Move& move : position.legalMoves()) {
            if (move.from == *selected_ && move.to == square) {
                matches.push_back(move);
            }
        }
        if (!matches.empty()) {
            if (matches.size() > 1 && matches.front().promotion.has_value()) {
                promotionFrom_ = *selected_;
                promotionTo_ = square;
                state_ = AnimState::Promoting;
                return true;
            }
            return beginMove(matches.front(), nowMs);
        }
    }

    std::optional<chess::Piece> piece = position.at(square);
    if (!piece.has_value() || piece->color != position.sideToMove()) {
        clearSelection();
        return false;
    }
    selected_ = square;
    highlights_.clear();
    for (const chess::Move& move : position.legalMoves()) {
        if (move.from == square) {
            if (std::find(highlights_.begin(), highlights_.end(), move.to) == highlights_.end()) {
                highlights_.push_back(move.to);
            }
        }
    }
    return true;
}

bool GameSession::choosePromotion(chess::PieceType type, std::int64_t nowMs) {
    if (state_ != AnimState::Promoting || !promotionFrom_.has_value() ||
        !promotionTo_.has_value()) {
        return false;
    }
    for (const chess::Move& move : game_.position().legalMoves()) {
        if (move.from == *promotionFrom_ && move.to == *promotionTo_ &&
            move.promotion.has_value() && *move.promotion == type) {
            promotionFrom_.reset();
            promotionTo_.reset();
            state_ = AnimState::Idle;
            return beginMove(move, nowMs);
        }
    }
    return false;
}

bool GameSession::beginMove(chess::Move move, std::int64_t nowMs) {
    const chess::Position before = game_.position();
    std::optional<chess::Piece> mover = before.at(move.from);
    if (!mover.has_value()) {
        return false;
    }

    // The rules module takes the move once, before anything is animated. The
    // display board then shows the position it left behind.
    if (!game_.play(move)) {
        return false;
    }
    clearSelection();
    nowMs_ = nowMs;

    moveCaptures_ = false;
    attacker_ = *mover;
    if (move.capture) {
        defenderSquare_ = move.enPassant ? chess::Square{move.to.file, move.from.rank} : move.to;
        std::optional<chess::Piece> victim = before.at(defenderSquare_);
        if (victim.has_value()) {
            defender_ = *victim;
            moveCaptures_ = true;
        }
    }
    if (moveCaptures_ && settings_.captures) {
        startInterpLoad(captureCode(attacker_, defender_));
    }

    for (std::optional<chess::Piece>& slot : display_) {
        slot.reset();
    }
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            display_[squareIndex(chess::Square{file, rank})] = before.at(chess::Square{file, rank});
        }
    }
    hidden_.reset();

    legs_.clear();
    Leg first;
    first.from = move.from;
    first.to = move.to;
    first.piece = *mover;
    legs_.push_back(first);
    display_[squareIndex(move.from)].reset();

    if (move.castling) {
        // The king walks first and the rook follows it. A king to the g file
        // moves the h file rook to f, and a king to the c file moves the a
        // file rook to d.
        const int rank = move.from.rank;
        const bool kingSide = move.to.file > move.from.file;
        Leg rook;
        rook.from = chess::Square{kingSide ? 7 : 0, rank};
        rook.to = chess::Square{kingSide ? 5 : 3, rank};
        std::optional<chess::Piece> rookPiece = before.at(rook.from);
        if (rookPiece.has_value()) {
            rook.piece = *rookPiece;
            legs_.push_back(rook);
            display_[squareIndex(rook.from)].reset();
        }
    }

    leg_ = 0;
    startLeg(0, nowMs);
    state_ = AnimState::Walking;
    return true;
}

void GameSession::startLeg(std::size_t index, std::int64_t nowMs) {
    Leg& leg = legs_[index];
    leg.walk = walkFor(leg.piece, leg.from, leg.to);
    const board::ProjectedPoint from = anchorOf(leg.from);
    const board::ProjectedPoint to = anchorOf(leg.to);
    leg.fromDepth = from.depth;
    leg.toDepth = to.depth;
    walker_.start(leg.walk, from.x, from.y, to.x, to.y, nowMs);
    walkDraw_ = walker_.advance(nowMs).draw;
}

void GameSession::finishLeg() {
    const Leg& leg = legs_[leg_];
    display_[squareIndex(leg.to)] = leg.piece;
}

void GameSession::startCapture(std::int64_t nowMs) {
    captureName_ = captureCode(attacker_, defender_);
    timeline_ = std::make_unique<anim::CaptureTimeline>(
        anim::loadCapture(cdDir_, captureName_, anim::sharedSoundCatalog(cdDir_)));

    std::vector<audio::Cue> cues;
    cueNames_.clear();
    for (const anim::CapturePose& pose : timeline_->poses) {
        if (!pose.hasSound || !pose.sound.resolved) {
            continue;
        }
        auto clip = clips_.find(pose.sound.resource);
        if (clip == clips_.end()) {
            continue;
        }
        cues.push_back(audio::Cue{pose.sound.startMs, &clip->second, audio::Channel::Effects,
                                  1.0f, false});
        cueNames_.push_back(pose.sound.resource);
    }
    if (timeline_->hasEndSound && timeline_->endSound.resolved) {
        auto clip = clips_.find(timeline_->endSound.resource);
        if (clip != clips_.end()) {
            cues.push_back(audio::Cue{timeline_->endSound.startMs, &clip->second,
                                      audio::Channel::Effects, 1.0f, false});
            cueNames_.push_back(timeline_->endSound.resource);
        }
    }
    scheduler_.setCues(std::move(cues));
    cuesReported_ = 0;

    collectInterp();

    // Both fighters leave the board for the film. The attacker is not on it
    // yet, and the defender hides where it stands.
    hidden_ = defenderSquare_;
    try {
        capturePlayer_.start(timeline_.get(), interp_.has_value() ? &*interp_ : nullptr, nowMs);
    } catch (const std::exception& problem) {
        // Whatever the interpolated sequence got wrong, the film still plays.
        // The authored poses need nothing from disk beyond the ANX.
        std::fprintf(stderr, "swchess: %s, playing the original cadence instead\n",
                     problem.what());
        interp_.reset();
        capturePlayer_.start(timeline_.get(), nullptr, nowMs);
    }
    capturePlayer_.setCadence(capturePlayer_.interp() != nullptr
                                  ? settings_.cadence
                                  : anim::Cadence::Original120ms);
    captureDraw_ = anim::DrawState{};
    state_ = AnimState::Capturing;
}

void GameSession::finishMove() {
    mixer_.stopAll();
    capturePlayer_.start(nullptr, 0);
    timeline_.reset();
    interp_.reset();
    interpLoading_ = false;
    captureName_.clear();
    captureDraw_ = anim::DrawState{};
    legs_.clear();
    leg_ = 0;
    moveCaptures_ = false;
    resetDisplay();
    state_ = game_.result() == chess::GameResult::Ongoing ? AnimState::Idle : AnimState::GameOver;
}

bool GameSession::skipCapture(std::int64_t nowMs) {
    if (state_ != AnimState::Capturing) {
        return false;
    }
    nowMs_ = nowMs;
    capturePlayer_.skip();
    if (timeline_ != nullptr) {
        scheduler_.skipTo(timeline_->endMs);
    }
    cuesReported_ = scheduler_.firedOrder().size();
    finishMove();
    return true;
}

void GameSession::advance(std::int64_t nowMs) {
    if (nowMs > nowMs_) {
        nowMs_ = nowMs;
    }
    if (state_ == AnimState::Walking) {
        anim::WalkUpdate update = walker_.advance(nowMs_);
        if (update.draw.visible) {
            walkDraw_ = update.draw;
        }
        if (!update.finished) {
            return;
        }
        finishLeg();
        if (leg_ + 1 < legs_.size()) {
            ++leg_;
            startLeg(leg_, nowMs_);
            return;
        }
        if (moveCaptures_ && settings_.captures) {
            // The attacker never lands on the square before the film. It is
            // put there when the film ends.
            display_[squareIndex(legs_.back().to)].reset();
            startCapture(nowMs_);
            return;
        }
        finishMove();
        return;
    }
    if (state_ == AnimState::Capturing) {
        anim::PlayerUpdate update = capturePlayer_.advance(nowMs_);
        scheduler_.advance(nowMs_);
        const std::vector<std::size_t>& fired = scheduler_.firedOrder();
        while (cuesReported_ < fired.size()) {
            const std::size_t cue = fired[cuesReported_];
            if (cue < cueNames_.size()) {
                ++soundPlays_[cueNames_[cue]];
                soundLog_.push_back(SoundPlay{cueNames_[cue], nowMs_});
            }
            ++cuesReported_;
        }
        if (update.draw.visible) {
            captureDraw_ = update.draw;
        }
        if (update.finished) {
            finishMove();
        }
    }
}

const std::vector<std::uint8_t>& GameSession::walkFrameRgba(const PieceBitmap* bitmap) const {
    auto found = frameCache_.find(bitmap);
    if (found == frameCache_.end()) {
        found = frameCache_.emplace(bitmap, pieceToRGBA(*bitmap)).first;
    }
    return found->second;
}

void GameSession::drawSquareOutline(Image& out, chess::Square square, std::uint8_t r,
                                    std::uint8_t g, std::uint8_t b, std::uint8_t a) const {
    const int row = board::rowForRank(square.rank);
    const int col = square.file;
    const board::ProjectedPoint corners[4] = {
        scene_.geometry.corner(row, col),
        scene_.geometry.corner(row, col + 1),
        scene_.geometry.corner(row + 1, col + 1),
        scene_.geometry.corner(row + 1, col),
    };
    for (int i = 0; i < 4; ++i) {
        const board::ProjectedPoint& one = corners[i];
        const board::ProjectedPoint& two = corners[(i + 1) % 4];
        blendLine(out, one.x, one.y, two.x, two.y, r, g, b, a);
    }
}

void GameSession::drawHighlights(Image& out) const {
    if (!selected_.has_value()) {
        return;
    }
    drawSquareOutline(out, *selected_, 255, 220, 40, 160);
    for (chess::Square square : highlights_) {
        drawSquareOutline(out, square, 60, 220, 90, 130);
    }
}

void GameSession::drawPieces(Image& out) const {
    const bool flat = board::setIsFlat(settings_.set);
    std::vector<Sprite> sprites;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const chess::Square square{file, rank};
            const std::optional<chess::Piece>& piece = display_[squareIndex(square)];
            if (!piece.has_value()) {
                continue;
            }
            if (hidden_.has_value() && *hidden_ == square) {
                continue;
            }
            const board::SheetPosition cell =
                board::pieceCell(settings_.set, piece->type, piece->color);
            const SheetCell& source = cellAt(scene_.sheet, cell.row, cell.column);
            const board::ProjectedPoint anchor = anchorOf(square);
            const board::SpriteRect rect =
                board::spriteRect(board::ScreenPoint{anchor.x, anchor.y}, scene_.sheet.cellWidth,
                                  scene_.sheet.cellHeight, flat);
            insertByDepth(sprites, Sprite{source.rgba.data(), source.width, source.height,
                                          rect.left, rect.top, anchor.depth});
        }
    }

    if (state_ == AnimState::Walking && walkDraw_.visible) {
        const Leg& leg = legs_[leg_];
        // The depth follows the piece down the line, then moves by ten so the
        // walker passes in front of the pieces it goes by, or behind them when
        // it is walking away from the viewer.
        const double mixed = static_cast<double>(leg.fromDepth) +
                             (static_cast<double>(leg.toDepth) - leg.fromDepth) *
                                 walkDraw_.progress;
        int depth = static_cast<int>(mixed);
        depth += leg.toDepth < leg.fromDepth ? -kWalkDepthBias : kWalkDepthBias;

        int width = walkDraw_.width;
        int height = walkDraw_.height;
        const std::uint8_t* rgba = nullptr;
        const SheetCell* cell = nullptr;
        if (walkDraw_.bitmap != nullptr) {
            rgba = walkFrameRgba(walkDraw_.bitmap).data();
        } else {
            const board::SheetPosition at =
                board::pieceCell(settings_.set, leg.piece.type, leg.piece.color);
            cell = &cellAt(scene_.sheet, at.row, at.column);
            rgba = cell->rgba.data();
            width = scene_.sheet.cellWidth;
            height = scene_.sheet.cellHeight;
        }
        const board::SpriteRect rect = board::spriteRect(
            board::ScreenPoint{walkDraw_.x, walkDraw_.y}, width, height, flat);
        insertByDepth(sprites, Sprite{rgba, width, height, rect.left, rect.top, depth});
    }

    for (const Sprite& sprite : sprites) {
        blitRGBA(out, sprite.rgba, sprite.width, sprite.height, sprite.x, sprite.y);
    }
}

std::string GameSession::stringBytes(int id) const {
    auto found = strings_.find(static_cast<int>(settings_.language));
    if (found != strings_.end() && found->second.has(id)) {
        return std::string(found->second.get(id));
    }
    return fallbackText(id);
}

const audio::Clip* GameSession::clip(const std::string& name) const {
    auto found = clips_.find(name);
    return found == clips_.end() ? nullptr : &found->second;
}

std::string GameSession::statusBytes() const {
    auto lookup = [&](int id) { return stringBytes(id); };

    const chess::Position& position = game_.position();
    switch (game_.result()) {
        case chess::GameResult::Checkmate:
            return lookup(position.sideToMove() == chess::Color::White ? kIdWhiteMated
                                                                      : kIdBlackMated);
        case chess::GameResult::Stalemate:
            return lookup(kIdStalemate);
        case chess::GameResult::DrawFiftyMove:
        case chess::GameResult::DrawInsufficientMaterial:
        case chess::GameResult::DrawThreefoldRepetition:
            return lookup(kIdDraw);
        case chess::GameResult::Ongoing:
            break;
    }
    std::string line =
        lookup(position.sideToMove() == chess::Color::White ? kIdWhiteToMove : kIdBlackToMove);
    if (position.inCheck()) {
        line += "  ";
        line += lookup(kIdCheck);
    }
    return line;
}

void GameSession::render(Image& out) {
    out = scene_.background;
    drawHighlights(out);
    drawPieces(out);
    if (state_ == AnimState::Capturing && captureDraw_.visible) {
        if (captureDraw_.frame != nullptr) {
            // An interpolated picture is already RGBA, and it sits at the one
            // rectangle every frame of the sequence shares.
            const anim::PngImage& image = captureDraw_.frame->image;
            blitRGBA(out, image.pixels.data(), image.width, image.height, captureDraw_.x,
                     captureDraw_.y);
        } else if (captureDraw_.record != nullptr) {
            const std::vector<std::uint8_t> rgba = anxToRGBA(*captureDraw_.record);
            blitRGBA(out, rgba.data(), captureDraw_.width, captureDraw_.height, captureDraw_.x,
                     captureDraw_.y);
        }
    }
    if (state_ == AnimState::Promoting) {
        const int cellW = scene_.sheet.cellWidth;
        const int cellH = scene_.sheet.cellHeight;
        const int top = anim::kCanvasHeight - cellH - 4;
        // The move is not played yet, so the side to move is the side whose
        // pawn is promoting.
        const chess::Color color = game_.position().sideToMove();
        for (int i = 0; i < 4; ++i) {
            const board::SheetPosition at =
                board::pieceCell(settings_.set, kPromotionChoices[i], color);
            const SheetCell& source = cellAt(scene_.sheet, at.row, at.column);
            blitRGBA(out, source.rgba.data(), source.width, source.height,
                     kPanelX + i * (cellW + kPanelGap), top);
        }
    }
}

}  // namespace swchess::game
