#include "game/shell.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "board/placement.h"
#include "render/compositor.h"
#include "save/cmg.h"
#include "save/native.h"

namespace swchess::game {
namespace {

namespace cmd = ui::command;

// The string ids the shell puts in the status bar, read off
// tools/fonts/strings.csv.
constexpr int kIdLoadGameLabel = 12;
constexpr int kIdSaveGameLabel = 13;
constexpr int kIdCapturedLabel = 23;
constexpr int kIdLoadSettingsLabel = 50;
constexpr int kIdSaveSettingsLabel = 51;
constexpr int kIdRestoreSettingsLabel = 52;
constexpr int kIdChangeBoardLabel = 74;
constexpr int kIdAllMovesReplayed = 2080;
constexpr int kIdAllMovesTakenBack = 2081;
constexpr int kIdCannotRemoveKing = 2057;
constexpr int kIdComputersTurn = 2123;
constexpr int kIdNotYourTurn = 33028;
// The HINT button's own label, which the bar shows in front of the move.
constexpr int kIdHintLabel = 40;
constexpr int kIdNoDraw = 2149;
constexpr int kIdNoPieceThere = 33026;
constexpr int kIdOpponentsPiece = 33027;
constexpr int kIdKingMovesOneSpace = 33038;
constexpr int kIdCannotCaptureOwn = 33039;
constexpr int kIdKingIsInCheck = 33040;
constexpr int kIdQueenAnyDirection = 33042;
constexpr int kIdMoveLeavesCheck = 33045;
constexpr int kIdIllegalRook = 33047;
constexpr int kIdIllegalBishop = 33052;
constexpr int kIdIllegalKnight = 33057;
constexpr int kIdIllegalPawn = 33062;

// The four look and feel toggles sit on page 7, each in its own slot.
constexpr int kTogglePage = 7;
// The three SELECT PLAYERS buttons sit on page 6 and the five play level
// buttons on page 9.
constexpr int kPlayersPage = 6;
constexpr int kLevelPage = 9;

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

bool endsWithIgnoringCase(const std::string& text, const std::string& tail) {
    if (text.size() < tail.size()) {
        return false;
    }
    for (std::size_t i = 0; i < tail.size(); ++i) {
        const char a = static_cast<char>(
            std::tolower(static_cast<unsigned char>(text[text.size() - tail.size() + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(tail[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

text::Language languageOfIndex(int index) {
    if (index < 0 || index > 3) {
        return text::Language::English;
    }
    return static_cast<text::Language>(index);
}

// The status id that explains why a click from `from` to `to` played no move.
int refusedMoveId(const chess::Position& position, chess::Square from, chess::Square to) {
    const std::optional<chess::Piece> target = position.at(to);
    if (target.has_value() && target->color == position.sideToMove()) {
        return kIdCannotCaptureOwn;
    }
    if (position.inCheck()) {
        return kIdMoveLeavesCheck;
    }
    const std::optional<chess::Piece> mover = position.at(from);
    if (!mover.has_value()) {
        return kIdNoPieceThere;
    }
    switch (mover->type) {
        case chess::PieceType::King: return kIdKingMovesOneSpace;
        case chess::PieceType::Queen: return kIdQueenAnyDirection;
        case chess::PieceType::Rook: return kIdIllegalRook;
        case chess::PieceType::Bishop: return kIdIllegalBishop;
        case chess::PieceType::Knight: return kIdIllegalKnight;
        case chess::PieceType::Pawn: break;
    }
    return kIdIllegalPawn;
}

}  // namespace

engine::Level levelOfCommand(int command) {
    int index = command - cmd::kLevelNewcomer;
    if (index < 0 || index > 4) {
        index = 0;
    }
    return static_cast<engine::Level>(index);
}

int commandOfLevel(engine::Level level) {
    return cmd::kLevelNewcomer + static_cast<int>(level);
}

const char* shellStateName(ShellState state) {
    switch (state) {
        case ShellState::Title: return "title";
        case ShellState::Playing: return "playing";
        case ShellState::Credits: return "credits";
        case ShellState::Finished: break;
    }
    return "finished";
}

std::string defaultConfigDir() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return ".";
    }
    return std::string(home) + "/Library/Application Support/Star Wars Chess";
}

std::string settingsPathFor(const ShellOptions& options) {
    const std::string dir = options.configDir.empty() ? defaultConfigDir() : options.configDir;
    return joinPath(dir, "SWC.INI");
}

ui::Settings loadShellSettings(const ShellOptions& options) {
    const std::string path = settingsPathFor(options);
    if (std::filesystem::exists(path)) {
        return ui::loadSettings(path);
    }
    // Nobody has saved settings here yet. The original reads the SWC.INI the
    // installer put beside XCHESS.EXE, so read that one, and fall back to the
    // values it carries when the CD has no copy.
    const std::string onCd = joinPath(options.cdDir, "SWC.INI");
    if (std::filesystem::exists(onCd)) {
        return ui::loadSettings(onCd);
    }
    return ui::shippedSettings();
}

Settings sessionSettingsFor(const ShellOptions& options, const ui::Settings& ini) {
    Settings settings;
    settings.set = ini.whiteOnTop() ? board::SetId::WhiteTop : board::SetId::WhiteBottom;
    if (options.set.has_value()) {
        settings.set = *options.set;
    }
    settings.language = languageOfIndex(ini.language);
    if (options.language.has_value()) {
        settings.language = *options.language;
    }
    settings.walking = ini.walking != 0;
    if (options.walking.has_value()) {
        settings.walking = *options.walking;
    }
    settings.captures = ini.captures != 0;
    if (options.captures.has_value()) {
        settings.captures = *options.captures;
    }
    settings.cadence = options.cadence;
    return settings;
}

ui::Settings barSettingsFor(const Settings& live, const ui::Settings& ini) {
    ui::Settings shown = ini;
    shown.walking = live.walking ? 1 : 0;
    shown.captures = live.captures ? 1 : 0;
    shown.language = static_cast<int>(live.language);
    shown.setWhiteOnTop(board::setOrientation(live.set) == board::Orientation::WhiteTop);
    return shown;
}

void composeGameScreen(Image& canvas, const Image& board, const ui::ButtonBar& bar) {
    if (canvas.width != kWindowWidth || canvas.height != kWindowHeight) {
        canvas = ui::makeCanvas(kWindowWidth, kWindowHeight);
    }
    ui::fillRect(canvas, ui::Rect{0, 0, canvas.width, canvas.height}, 0, 0, 0);
    ui::blitClipped(canvas, board.rgba.data(), board.width, board.height, 0, 0,
                    ui::Rect{0, 0, canvas.width, canvas.height});
    bar.render(canvas);
}

GameShell::GameShell(const ShellOptions& options)
    : options_(options),
      settingsPath_(settingsPathFor(options)),
      ini_(loadShellSettings(options)),
      session_(options.cdDir, options.assetsDir, sessionSettingsFor(options, ini_)) {
    // The buttons show what the session actually runs, so a command line
    // override seeds the toggles as well as the board.
    const Settings& live = session_.settings();
    ini_ = barSettingsFor(live, ini_);

    bar_ = std::make_unique<ui::ButtonBar>(options_.cdDir, live.language, ini_);
    applySoundSettings();

    engine::Config engineConfig;
    engineConfig.cdDir = options_.cdDir;
    engineConfig.level = levelOfCommand(ini_.playLevel);
    engine_ = options_.makeEngine ? options_.makeEngine(engineConfig)
                                  : engine::makeRandomEngine(engineConfig);
    session_.setEngine(engine_.get());
    applyPlayers(bar_->players());

    state_ = options_.skipTitle ? ShellState::Playing : ShellState::Title;
    if (state_ == ShellState::Title) {
        title_ = std::make_unique<ui::TitleSequence>(options_.cdDir, live.language);
    }
    lastFen_ = session_.position().fen();
    refreshStatus();
}

void GameShell::start(std::int64_t nowMs) {
    nowMs_ = nowMs;
    if (state_ == ShellState::Title && title_ != nullptr) {
        title_->start(nowMs);
        for (const std::string& cue : title_->takeCues()) {
            playCue(cue);
        }
        return;
    }
    if (state_ == ShellState::Playing && !options_.loadPath.empty()) {
        loadGameFile(options_.loadPath);
    }
}

void GameShell::advance(std::int64_t nowMs) {
    nowMs_ = std::max(nowMs_, nowMs);
    if (state_ == ShellState::Title || state_ == ShellState::Credits) {
        if (title_ == nullptr) {
            state_ = state_ == ShellState::Title ? ShellState::Playing : ShellState::Finished;
            return;
        }
        title_->advance(nowMs_);
        // The launch run stops when the title screen ends. WM_DESTROY leaves
        // the state word at 4, so only the QUIT OK button ever shows the
        // credit roll.
        const bool launchOver =
            state_ == ShellState::Title && static_cast<int>(title_->state()) >=
                                               static_cast<int>(ui::TitleState::Credits);
        const bool creditsOver = state_ == ShellState::Credits && title_->finished();
        if (!launchOver && !creditsOver) {
            for (const std::string& cue : title_->takeCues()) {
                playCue(cue);
            }
            return;
        }
        session_.mixer().stopAll();
        if (launchOver) {
            // WM_DESTROY plays ENERGIZE.WAV as the title window closes.
            playCue("ENERGIZE.WAV");
            state_ = ShellState::Playing;
            title_.reset();
            if (!options_.loadPath.empty()) {
                loadGameFile(options_.loadPath);
            }
            refreshStatus();
        } else {
            state_ = ShellState::Finished;
        }
        return;
    }
    if (state_ != ShellState::Playing) {
        return;
    }
    session_.advance(nowMs_);
    noteHint();
    refreshStatus();
}

bool GameShell::takeMinimize() {
    const bool asked = minimize_;
    minimize_ = false;
    return asked;
}

void GameShell::playCue(const std::string& name) {
    const audio::Clip* clip = session_.clip(name);
    if (clip == nullptr) {
        auto found = looseClips_.find(name);
        if (found == looseClips_.end()) {
            std::optional<audio::Clip> loaded =
                audio::loadWav(std::filesystem::path(joinPath(options_.cdDir, name)));
            if (!loaded.has_value()) {
                return;
            }
            found = looseClips_.emplace(name, std::move(*loaded)).first;
        }
        clip = &found->second;
    }
    const audio::Channel channel = name == "ENERGIZE.WAV" ? audio::Channel::Effects
                                                          : audio::Channel::Music;
    session_.mixer().play(*clip, channel);
}

void GameShell::applySoundSettings() {
    const float level = ini_.sounds != 0 ? 1.0f : 0.0f;
    session_.mixer().setVolume(audio::Channel::Music, level);
    session_.mixer().setVolume(audio::Channel::Effects, level);
    session_.mixer().setVolume(audio::Channel::Speech, level);
    if (ini_.sounds == 0) {
        // Command 157 silences whatever is sounding when it turns music off.
        session_.mixer().stopAll();
    }
}

void GameShell::applySettings(bool includeBackground) {
    ini_ = bar_->settings();
    session_.setWalking(ini_.walking != 0);
    session_.setCaptures(ini_.captures != 0);

    // The rotation picks between the two orientable sets. The 2D and facing
    // sets keep whatever the player asked for.
    const board::SetId current = session_.settings().set;
    if (current == board::SetId::WhiteBottom || current == board::SetId::WhiteTop) {
        const board::SetId want =
            ini_.whiteOnTop() ? board::SetId::WhiteTop : board::SetId::WhiteBottom;
        if (want != current) {
            session_.setSet(want);
        }
    }
    if (includeBackground) {
        session_.setBackground(ini_.board);
    }
    applySoundSettings();
}

void GameShell::setLanguage(text::Language language) {
    session_.setLanguage(language);
    ini_.language = static_cast<int>(language);
    // The bar reads one language table, so a new language needs a new bar. The
    // page and the two radio groups carry over.
    const int page = bar_->page();
    const int players = bar_->players();
    const int side = bar_->sideToMove();
    bar_ = std::make_unique<ui::ButtonBar>(options_.cdDir, language, ini_);
    bar_->setPage(page);
    bar_->setPlayers(players);
    bar_->setSideToMove(side);
    message_.clear();
    refreshStatus();
}

void GameShell::setSet(board::SetId set) {
    session_.setSet(set);
    ini_.setWhiteOnTop(board::setOrientation(set) == board::Orientation::WhiteTop);
    bar_->setSettings(ini_);
}

void GameShell::setMessageId(int id) { setMessageBytes(session_.stringBytes(id)); }

void GameShell::setMessageBytes(std::string bytes) {
    message_ = std::move(bytes);
    bar_->setMessageBytes(message_);
}

void GameShell::refreshStatus() {
    const std::string fen = session_.position().fen();
    if (fen != lastFen_) {
        lastFen_ = fen;
        message_.clear();
    }
    if (message_.empty() && session_.engineThinking()) {
        // The engine holds the move, so the bar says so until it answers.
        bar_->setMessageBytes(session_.stringBytes(kIdComputersTurn));
        return;
    }
    bar_->setMessageBytes(message_.empty() ? session_.statusBytes() : message_);
}

void GameShell::noteHint() {
    if (session_.hintSerial() == hintShown_) {
        return;
    }
    hintShown_ = session_.hintSerial();
    const std::optional<chess::Move>& move = session_.hintMove();
    if (!move.has_value()) {
        return;
    }
    // The original wrote the suggested move into the status bar. This writes
    // the same move in front of the button's own label, and the session
    // outlines the two squares for two seconds.
    std::string text = session_.stringBytes(kIdHintLabel);
    if (!text.empty()) {
        text += " ";
    }
    text += session_.position().longAlgebraic(*move);
    setMessageBytes(std::move(text));
}

void GameShell::applyPlayers(int command) {
    // HUMAN VS. COMPUTER seats the person on White. menus.md records no key
    // and no button for the other way round, so White is the default side.
    const Seat white = command == cmd::kComputerComputer ? Seat::Computer : Seat::Human;
    const Seat black = command == cmd::kHumanHuman ? Seat::Human : Seat::Computer;
    session_.cancelRequests();
    session_.setSeat(chess::Color::White, white);
    session_.setSeat(chess::Color::Black, black);
}

void GameShell::leaveDemoMode() {
    if (demoPlayers_ == ui::kNoCommand) {
        return;
    }
    const int previous = demoPlayers_;
    demoPlayers_ = ui::kNoCommand;
    bar_->setPlayers(previous);
    applyPlayers(previous);
}

void GameShell::startCredits(std::int64_t nowMs) {
    session_.mixer().stopAll();
    title_ = std::make_unique<ui::TitleSequence>(options_.cdDir, session_.settings().language);
    title_->start(nowMs, ui::TitleState::Credits);
    for (const std::string& cue : title_->takeCues()) {
        playCue(cue);
    }
    state_ = ShellState::Credits;
}

bool GameShell::onMouseMove(int x, int y) {
    if (state_ != ShellState::Playing) {
        return false;
    }
    const int before = bar_->hoveredSlot();
    bar_->onMouseMove(x, y);
    return bar_->hoveredSlot() != before;
}

bool GameShell::onMouseDown(int x, int y, std::int64_t nowMs) {
    nowMs_ = std::max(nowMs_, nowMs);
    if (state_ == ShellState::Title || state_ == ShellState::Credits) {
        if (title_ != nullptr) {
            title_->skip();
            for (const std::string& cue : title_->takeCues()) {
                playCue(cue);
            }
        }
        advance(nowMs_);
        return true;
    }
    bar_->onMouseDown(x, y);
    if (bar_->armedSlot() != ui::kNoSlot) {
        return true;
    }
    return boardClick(x, y, nowMs_);
}

bool GameShell::onMouseUp(int x, int y, std::int64_t nowMs) {
    if (state_ != ShellState::Playing) {
        return false;
    }
    const int fired = bar_->onMouseUp(x, y);
    if (fired == ui::kNoCommand) {
        return false;
    }
    runCommand(fired, nowMs);
    return true;
}

bool GameShell::onKey(char key, std::int64_t nowMs) {
    nowMs_ = std::max(nowMs_, nowMs);
    if (state_ == ShellState::Title || state_ == ShellState::Credits) {
        if (title_ != nullptr) {
            title_->skip();
            for (const std::string& cue : title_->takeCues()) {
                playCue(cue);
            }
        }
        advance(nowMs_);
        return true;
    }
    if (session_.skipCapture(nowMs_)) {
        return true;
    }
    switch (key) {
        case '\x1b':
            quit();
            return true;
        case '1':
            setSet(board::SetId::WhiteBottom);
            return true;
        case '2':
            setSet(board::SetId::WhiteTop);
            return true;
        case '3':
            setSet(board::SetId::Facing);
            return true;
        case '4':
            setSet(board::SetId::TwoD);
            return true;
        case 'l': {
            const int next = (static_cast<int>(session_.settings().language) + 1) % 4;
            setLanguage(static_cast<text::Language>(next));
            return true;
        }
        case 'w': {
            ui::Settings flipped = bar_->settings();
            flipped.walking = flipped.walking != 0 ? 0 : 1;
            bar_->setSettings(flipped);
            runCommand(cmd::kWalkingToggle, nowMs_);
            return true;
        }
        case 'c': {
            ui::Settings flipped = bar_->settings();
            flipped.captures = flipped.captures != 0 ? 0 : 1;
            bar_->setSettings(flipped);
            runCommand(cmd::kCapturesToggle, nowMs_);
            return true;
        }
        case 'i':
            session_.toggleCadence();
            return true;
        case 'u':
            runCommand(cmd::kTakeBackMove, nowMs_);
            return true;
        case 'n':
            runCommand(cmd::kNewGame, nowMs_);
            return true;
        default:
            break;
    }
    return false;
}

bool GameShell::boardClick(int x, int y, std::int64_t nowMs) {
    // A click on the board ends the demo, the way it did in the original.
    leaveDemoMode();
    if (session_.state() == AnimState::Idle) {
        const std::optional<chess::Square> square =
            board::hitTest(session_.scene().geometry, x, y);
        if (square.has_value()) {
            noteRefusedClick(*square);
        }
    }
    const bool changed = session_.clickPixel(x, y, nowMs);
    refreshStatus();
    return changed;
}

void GameShell::noteRefusedClick(chess::Square square) {
    const chess::Position& position = session_.position();
    if (session_.engineToMove()) {
        setMessageId(kIdNotYourTurn);
        return;
    }
    const std::optional<chess::Square> selected = session_.selection();
    if (selected.has_value() && !(*selected == square)) {
        for (const chess::Move& move : position.legalMoves()) {
            if (move.from == *selected && move.to == square) {
                message_.clear();
                return;
            }
        }
        setMessageId(refusedMoveId(position, *selected, square));
        return;
    }
    if (selected.has_value()) {
        return;
    }
    const std::optional<chess::Piece> piece = position.at(square);
    if (!piece.has_value()) {
        setMessageId(kIdNoPieceThere);
        return;
    }
    if (piece->color != position.sideToMove()) {
        setMessageId(kIdOpponentsPiece);
        return;
    }
    message_.clear();
}

bool GameShell::setSideToMove(chess::Color color) {
    const chess::Position& position = session_.position();
    if (position.sideToMove() == color) {
        return true;
    }
    if (position.inCheck()) {
        // Handing the move over would leave a king standing in check, which is
        // no position at all.
        setMessageId(kIdKingIsInCheck);
        return false;
    }
    std::string fen = position.fen();
    const std::size_t at = fen.find(position.sideToMove() == chess::Color::White ? " w " : " b ");
    if (at == std::string::npos) {
        return false;
    }
    fen[at + 1] = color == chess::Color::White ? 'w' : 'b';
    std::optional<chess::Position> next = chess::Position::fromFen(fen);
    if (!next.has_value()) {
        return false;
    }
    session_.setGame(chess::Game(*next));
    message_.clear();
    return true;
}

std::string GameShell::chooseFile(bool save) {
    if (!options_.chooseFile) {
        return std::string();
    }
    return options_.chooseFile(save);
}

void GameShell::runCommand(int command, std::int64_t nowMs) {
    nowMs_ = std::max(nowMs_, nowMs);
    int page = 0;
    if (ui::isPagePush(command, &page) || command == cmd::kPop || command == cmd::kSetupBack) {
        // The bar has already moved. Nothing else answers these.
        refreshStatus();
        return;
    }
    if (command != cmd::kDemoMode) {
        // Any other button ends the demo before it runs.
        leaveDemoMode();
    }

    switch (command) {
        case cmd::kNewGame:
        case cmd::kSetupNew:
            session_.newGame();
            message_.clear();
            break;
        case cmd::kDemoMode:
            if (demoPlayers_ == ui::kNoCommand) {
                demoPlayers_ = bar_->players();
                bar_->setPlayers(cmd::kComputerComputer);
                applyPlayers(cmd::kComputerComputer);
            }
            setMessageId(kIdComputersTurn);
            break;
        case cmd::kForceMove:
            if (session_.engineToMove()) {
                session_.forceEngineMove();
            } else {
                // The engine takes over the colour the player was about to
                // move, which is what FORCE MOVE did in the original. The
                // player keeps the other colour.
                const chess::Color side = session_.position().sideToMove();
                session_.setSeat(side, Seat::Computer);
                session_.setSeat(chess::opposite(side), Seat::Human);
                session_.forceEngineMove();
            }
            setMessageId(kIdComputersTurn);
            break;
        case cmd::kHint:
            if (!session_.requestHint()) {
                setMessageId(session_.engineToMove() ? kIdComputersTurn : kIdHintLabel);
            } else {
                setMessageId(kIdHintLabel);
            }
            break;
        case cmd::kLoadGame: {
            const std::string path = chooseFile(false);
            if (path.empty()) {
                setMessageId(kIdLoadGameLabel);
            } else {
                loadGameFile(path);
            }
            break;
        }
        case cmd::kSaveGame: {
            const std::string path = chooseFile(true);
            if (path.empty()) {
                setMessageId(kIdSaveGameLabel);
            } else {
                saveGameFile(path);
            }
            break;
        }
        case cmd::kSaveSettings:
            if (saveSettingsFile()) {
                setMessageId(kIdSaveSettingsLabel);
            }
            break;
        case cmd::kLoadSettings:
            loadSettingsFile();
            setMessageId(kIdLoadSettingsLabel);
            break;
        case cmd::kRestoreSettings:
            loadSettingsFile();
            setMessageId(kIdRestoreSettingsLabel);
            break;
        case cmd::kQuitOk:
            startCredits(nowMs_);
            return;
        case cmd::kMusicToggle:
            setMessageId(bar_->statusIdFor(kTogglePage, 1));
            break;
        case cmd::kCapturesToggle:
            setMessageId(bar_->statusIdFor(kTogglePage, 2));
            break;
        case cmd::kWalkingToggle:
            setMessageId(bar_->statusIdFor(kTogglePage, 3));
            break;
        case cmd::kWhiteOnBottom:
            setMessageId(bar_->statusIdFor(kTogglePage, 0));
            break;
        case cmd::kChangeBoard:
            setMessageId(kIdChangeBoardLabel);
            break;
        case cmd::kSwitchSides:
            setSideToMove(chess::opposite(session_.position().sideToMove()));
            break;
        case cmd::kWhiteToMove:
            setSideToMove(chess::Color::White);
            break;
        case cmd::kBlackToMove:
            setSideToMove(chess::Color::Black);
            break;
        case cmd::kTakeBackMove:
            if (!session_.undo()) {
                setMessageId(kIdAllMovesTakenBack);
            } else {
                message_.clear();
            }
            break;
        case cmd::kReplayMove:
            // Nothing keeps the moves a take back removed, so there is never
            // one to play again.
            setMessageId(kIdAllMovesReplayed);
            break;
        case cmd::kOfferDraw:
            setMessageId(kIdNoDraw);
            break;
        case cmd::kShowCaptured: {
            const chess::Position& start = session_.game().startPosition();
            const chess::Position& now = session_.position();
            // Count what stands on the board now against what the game began
            // with, piece by piece.
            std::string taken;
            std::map<char, int> counts;
            for (int rank = 0; rank < 8; ++rank) {
                for (int file = 0; file < 8; ++file) {
                    const std::optional<chess::Piece> piece = start.at(chess::Square{file, rank});
                    if (piece.has_value()) {
                        ++counts[chess::pieceLetter(*piece)];
                    }
                }
            }
            for (int rank = 0; rank < 8; ++rank) {
                for (int file = 0; file < 8; ++file) {
                    const std::optional<chess::Piece> piece = now.at(chess::Square{file, rank});
                    if (piece.has_value()) {
                        --counts[chess::pieceLetter(*piece)];
                    }
                }
            }
            for (const auto& [letter, missing] : counts) {
                for (int i = 0; i < missing; ++i) {
                    taken += letter;
                }
            }
            if (taken.empty()) {
                setMessageId(kIdCapturedLabel);
            } else {
                setMessageBytes(taken);
            }
            break;
        }
        case cmd::kSetupClear:
            // The port has no board editor, so it refuses to take the kings
            // off the board.
            setMessageId(kIdCannotRemoveKing);
            break;
        case cmd::kSetupDone:
            bar_->setPage(0);
            message_.clear();
            break;
        case cmd::kMinimize:
            minimize_ = true;
            break;
        case cmd::kHumanComputer:
        case cmd::kHumanHuman:
        case cmd::kComputerComputer:
            applyPlayers(command);
            setMessageId(bar_->statusIdFor(kPlayersPage, command - cmd::kHumanComputer));
            break;
        default:
            if (command >= cmd::kLevelNewcomer && command <= cmd::kLevelExpert) {
                // The bar already wrote the id into settings_.playLevel, and
                // SAVE SETTINGS is what puts it in SWC.INI.
                if (engine_ != nullptr) {
                    engine_->setLevel(levelOfCommand(command));
                }
                setMessageId(bar_->statusIdFor(kLevelPage, command - cmd::kLevelNewcomer));
            }
            break;
    }

    // The buttons own the toggles and the two radio groups, so the settings
    // come back from the bar after every command.
    applySettings(command == cmd::kChangeBoard);
    refreshStatus();
}

void GameShell::loadSettingsFile() {
    ini_ = loadShellSettings(options_);
    bar_->setSettings(ini_);
    setLanguage(languageOfIndex(ini_.language));
    applySettings(true);
}

bool GameShell::saveSettingsFile() {
    try {
        const std::filesystem::path path(settingsPath_);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        ui::saveSettings(settingsPath_, ini_);
        return true;
    } catch (const std::exception& error) {
        setMessageBytes(error.what());
        return false;
    }
}

bool GameShell::loadGameFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setMessageBytes("cannot open " + path);
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    if (endsWithIgnoringCase(path, ".cmg")) {
        std::vector<std::byte> bytes(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
        }
        const save::Result<save::CmgGame> read = save::readCmg(bytes);
        if (!read) {
            setMessageBytes(read.error);
            return false;
        }
        session_.setGame(read->game);
        session_.cancelRequests();
        refreshStatus();
        return true;
    }

    const save::Result<save::NativeSave> read = save::readNative(text);
    if (!read) {
        setMessageBytes(read.error);
        return false;
    }
    const save::Result<chess::Game> game = save::gameFromNative(*read);
    if (!game) {
        setMessageBytes(game.error);
        return false;
    }
    session_.setGame(*game);
    session_.cancelRequests();

    // A native file carries the settings the game was saved under.
    ini_.walking = read->settings.walking ? 1 : 0;
    ini_.captures = read->settings.captures ? 1 : 0;
    ini_.sounds = read->settings.sounds ? 1 : 0;
    ini_.setWhiteOnTop(read->settings.set == "black_bottom");
    bar_->setSettings(ini_);
    applySettings(false);
    refreshStatus();
    return true;
}

bool GameShell::saveGameFile(const std::string& path) {
    try {
        const std::filesystem::path where(path);
        if (where.has_parent_path()) {
            std::filesystem::create_directories(where.parent_path());
        }
    } catch (const std::exception& error) {
        setMessageBytes(error.what());
        return false;
    }

    if (endsWithIgnoringCase(path, ".cmg")) {
        save::CmgMetadata meta;
        const save::Result<std::vector<std::byte>> bytes =
            save::writeCmg(session_.game(), meta);
        if (!bytes) {
            setMessageBytes(bytes.error);
            return false;
        }
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            setMessageBytes("cannot write " + path);
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes->data()),
                  static_cast<std::streamsize>(bytes->size()));
        setMessageId(kIdSaveGameLabel);
        return true;
    }

    save::NativeSave doc;
    doc.settings.set = board::setOrientation(session_.settings().set) ==
                               board::Orientation::WhiteTop
                           ? "black_bottom"
                           : "white_bottom";
    doc.settings.language = text::languageName(session_.settings().language);
    doc.settings.cadence = anim::cadenceName(session_.settings().cadence);
    doc.settings.walking = session_.settings().walking;
    doc.settings.captures = session_.settings().captures;
    doc.settings.sounds = ini_.sounds != 0;
    doc = save::nativeFromGame(session_.game(), doc);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        setMessageBytes("cannot write " + path);
        return false;
    }
    out << save::writeNative(doc);
    setMessageId(kIdSaveGameLabel);
    return true;
}

void GameShell::render(Image& out) {
    if (out.width != kWindowWidth || out.height != kWindowHeight) {
        out = ui::makeCanvas(kWindowWidth, kWindowHeight);
    }
    if ((state_ == ShellState::Title || state_ == ShellState::Credits) && title_ != nullptr) {
        title_->render(out);
        return;
    }
    session_.render(board_);
    composeGameScreen(out, board_, *bar_);
}

}  // namespace swchess::game
