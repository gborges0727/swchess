// Tests for the saved game module. The first argument is the directory
// holding the original CD files, which the test only reads.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "save/cmg.h"
#include "save/native.h"

using namespace swchess;

namespace {

int gFailures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++gFailures;
    }
}

template <class A, class B>
void checkEq(const A& got, const B& want, const std::string& what) {
    if (!(got == want)) {
        std::cerr << "FAIL: " << what << ": got " << got << ", wanted " << want << "\n";
        ++gFailures;
    }
}

std::vector<std::byte> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) out[i] = static_cast<std::byte>(raw[i]);
    return out;
}

const std::string kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Scholar's mate. White mates on the seventh ply.
chess::Game scholarsMate() {
    chess::Game game;
    for (const char* san : {"e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6", "Qxf7#"}) {
        if (!game.playSan(san)) {
            std::cerr << "FAIL: the test could not play " << san << "\n";
            ++gFailures;
        }
    }
    return game;
}

// The move word the engine writes: destination file and rank in the low six
// bits, origin file and rank above them, then the promotion code. Rank index 0
// is rank 8.
std::uint16_t moveWord(int fromFile, int fromRank, int toFile, int toRank) {
    return static_cast<std::uint16_t>(toFile | ((7 - toRank) << 3) | (fromFile << 6) |
                                      ((7 - fromRank) << 9) | (7 << 12));
}

void pushSlot(std::vector<std::byte>& out, const std::string& text) {
    for (std::size_t i = 0; i < save::kNameSlot; ++i) {
        out.push_back(static_cast<std::byte>(i < text.size() ? text[i] : '\0'));
    }
}

void pushWord(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>(value >> 8));
}

// Builds a file by hand so a test can put something wrong in it.
std::vector<std::byte> handBuilt(std::uint8_t formatByte, std::uint16_t moveCount,
                                 std::uint16_t currentPly,
                                 const std::vector<std::uint16_t>& words) {
    std::vector<std::byte> out;
    pushSlot(out, "Starwars Chess Game");
    out.push_back(std::byte{0x1A});
    out.push_back(static_cast<std::byte>(formatByte));
    pushSlot(out, "Earthling 1");
    out.push_back(std::byte{1});
    pushSlot(out, "Newcomer");
    out.push_back(std::byte{2});
    out.push_back(std::byte{0});
    out.push_back(std::byte{0x1A});
    out.push_back(std::byte{0x20});
    out.push_back(std::byte{0xFF});
    pushWord(out, moveCount);
    pushWord(out, currentPly);
    for (std::uint16_t w : words) {
        pushWord(out, w);
        pushWord(out, 0);
    }
    return out;
}

void testShippedFile(const std::string& cdDir) {
    const std::string path = cdDir + "/STARWARS.CMG";
    auto bytes = readFile(path);
    checkEq(bytes.size(), std::size_t{108}, "the shipped file is 108 bytes");
    if (bytes.empty()) {
        std::cerr << "FAIL: could not read " << path << "\n";
        ++gFailures;
        return;
    }

    auto game = save::readCmg(bytes);
    if (!game) {
        std::cerr << "FAIL: reading the shipped file: " << game.error << "\n";
        ++gFailures;
        return;
    }
    checkEq(game->meta.title, std::string("Starwars Chess Game"), "the title");
    checkEq(game->meta.whiteName, std::string("Earthling 1"), "the white name");
    checkEq(game->meta.blackName, std::string("Newcomer"), "the black name");
    check(game->meta.whiteType == save::PlayerType::Human, "White is a person");
    check(game->meta.blackType == save::PlayerType::Computer, "Black is the program");
    checkEq(static_cast<int>(game->positionFlag), 0xFF, "the position flag");
    checkEq(game->game.moves().size(), std::size_t{0}, "the move count");
    checkEq(game->meta.currentPly, 0, "the current ply");
    check(!game->meta.gameOverCode.has_value(), "the shipped game has not ended");
    checkEq(game->game.position().fen(), kStartFen, "the position is the standard opening");

    // Writing the same game back produces a file that reads the same way. The
    // bytes differ from the original, which left stale heap after each name.
    auto written = save::writeCmg(game->game, game->meta);
    if (!written) {
        std::cerr << "FAIL: writing the shipped game back: " << written.error << "\n";
        ++gFailures;
        return;
    }
    checkEq(written->size(), std::size_t{108}, "the rewritten file is 108 bytes");
    auto again = save::readCmg(*written);
    check(static_cast<bool>(again), "the rewritten file reads back");
    if (again) {
        checkEq(again->meta.whiteName, std::string("Earthling 1"), "the rewritten white name");
        checkEq(static_cast<int>(again->positionFlag), 0xFF, "the rewritten position flag");
    }
}

void testMoveRoundTrip() {
    chess::Game game = scholarsMate();
    check(game.result() == chess::GameResult::Checkmate, "scholar's mate is mate");

    save::CmgMetadata meta;
    meta.whiteName = "Luke";
    meta.whiteType = save::PlayerType::Human;
    meta.blackName = "Vader";
    meta.blackType = save::PlayerType::Computer;
    meta.moveTimes = {12, 30, 7, 44, 9, 51, 3};
    meta.annotations = {"", "", "", "", "the queen comes out", "", ""};
    meta.gameOverCode = save::kGameOverA;

    auto written = save::writeCmg(game, meta);
    if (!written) {
        std::cerr << "FAIL: writing scholar's mate: " << written.error << "\n";
        ++gFailures;
        return;
    }
    auto read = save::readCmg(*written);
    if (!read) {
        std::cerr << "FAIL: reading scholar's mate back: " << read.error << "\n";
        ++gFailures;
        return;
    }
    checkEq(read->game.moves().size(), game.moves().size(), "the move count survives");
    check(read->game.moves() == game.moves(), "every move survives");
    checkEq(read->game.position().fen(), game.position().fen(), "the final position survives");
    checkEq(read->game.pgnMoves(), game.pgnMoves(), "the move text survives");
    checkEq(read->meta.whiteName, std::string("Luke"), "the white name survives");
    check(read->meta.blackType == save::PlayerType::Computer, "the black type survives");
    check(read->meta.moveTimes == meta.moveTimes, "the move times survive");
    check(read->meta.annotations == meta.annotations, "the annotations survive");
    check(read->meta.gameOverCode == meta.gameOverCode, "the game over code survives");
    checkEq(read->meta.currentPly, 7, "the current ply counts every move");
}

// A file whose start position is not the standard opening spells the board out
// in 64 bytes.
void testCustomPosition() {
    auto start = chess::Position::fromFen("4k3/8/8/8/8/8/4P3/4K3 b - - 0 1");
    check(start.has_value(), "the test position parses");
    if (!start) return;

    chess::Game game(*start);
    check(game.playSan("Kd7"), "Black moves");

    auto written = save::writeCmg(game, save::CmgMetadata{});
    if (!written) {
        std::cerr << "FAIL: writing a custom position: " << written.error << "\n";
        ++gFailures;
        return;
    }
    checkEq(static_cast<int>(static_cast<std::uint8_t>((*written)[save::kHeaderSize + 2])), 0x20,
            "the position flag says Black moves first");

    auto read = save::readCmg(*written);
    if (!read) {
        std::cerr << "FAIL: reading a custom position: " << read.error << "\n";
        ++gFailures;
        return;
    }
    checkEq(read->game.startPosition().fen(), std::string("4k3/8/8/8/8/8/4P3/4K3 b - - 0 1"),
            "the saved board comes back");
    checkEq(read->game.position().fen(), game.position().fen(), "the move replays");
}

void testTakeback() {
    chess::Game game = scholarsMate();
    save::CmgMetadata meta;
    meta.currentPly = 4;
    auto written = save::writeCmg(game, meta);
    check(static_cast<bool>(written), "a file with moves taken back writes");
    if (!written) return;
    auto read = save::readCmg(*written);
    check(static_cast<bool>(read), "a file with moves taken back reads");
    if (!read) return;
    checkEq(read->meta.currentPly, 4, "the current ply survives");
    chess::Game shown = save::gameAtPly(read->game, read->meta.currentPly);
    checkEq(shown.moves().size(), std::size_t{4}, "the shown game stops at the current ply");
    checkEq(shown.pgnMoves(), std::string("1. e4 e5 2. Bc4 Nc6"), "the shown moves");
}

void testRejections() {
    // A pawn cannot walk three squares.
    auto illegal = handBuilt(0x20, 1, 1, {moveWord(4, 1, 4, 4)});
    auto readIllegal = save::readCmg(illegal);
    check(!readIllegal, "an illegal move is refused");
    check(readIllegal.error.find("not legal") != std::string::npos,
          "the error names the illegal move");

    auto wrongFormat = handBuilt(0x21, 0, 0, {});
    check(!save::readCmg(wrongFormat), "a wrong format byte is refused");

    auto good = handBuilt(0x20, 0, 0, {});
    check(static_cast<bool>(save::readCmg(good)), "the hand built file otherwise reads");

    for (std::size_t n = 0; n < good.size(); ++n) {
        std::span<const std::byte> cut(good.data(), n);
        check(!save::readCmg(cut), "a file cut to " + std::to_string(n) + " bytes is refused");
    }

    // A record whose move count promises more than the file holds.
    auto shortCount = handBuilt(0x20, 3, 0, {moveWord(4, 1, 4, 3)});
    check(!save::readCmg(shortCount), "a file that ends inside the moves is refused");

    // A negative move count means a variation tree.
    auto tree = handBuilt(0x20, 0xFFFF, 0, {});
    auto readTree = save::readCmg(tree);
    check(!readTree, "a variation tree is refused");
    check(readTree.error.find("variation tree") != std::string::npos,
          "the error says the file holds a variation tree");

    // The current ply cannot sit past the move count.
    auto badPly = handBuilt(0x20, 0, 2, {});
    check(!save::readCmg(badPly), "a current ply past the move count is refused");

    // The position flag has three values and nothing else.
    auto badFlag = handBuilt(0x20, 0, 0, {});
    badFlag[save::kHeaderSize + 2] = std::byte{0x30};
    check(!save::readCmg(badFlag), "an unknown position flag is refused");

    // A name too long for its 32 byte slot cannot be written.
    save::CmgMetadata longName;
    longName.whiteName = std::string(40, 'x');
    check(!save::writeCmg(chess::Game{}, longName), "an overlong name is refused");
}

void testNative() {
    chess::Game game = scholarsMate();

    save::NativeSave fields;
    fields.whiteName = "Luke";
    fields.whiteType = save::PlayerType::Human;
    fields.blackName = "Vader";
    fields.blackType = save::PlayerType::Computer;
    fields.settings.set = "black_bottom";
    fields.settings.language = "german";
    fields.settings.cadence = "original120ms";
    fields.settings.walking = false;
    fields.settings.captures = true;
    fields.settings.sounds = false;

    save::NativeSave save = save::nativeFromGame(game, fields, 5);
    checkEq(save.moves.size(), std::size_t{7}, "the native save holds every move");
    checkEq(save.moves[0], std::string("e2e4"), "the first move in long algebraic");
    checkEq(save.currentPly, 5, "the native current ply");

    std::string text = save::writeNative(save);
    auto read = save::readNative(text);
    if (!read) {
        std::cerr << "FAIL: reading the native save: " << read.error << "\n";
        ++gFailures;
        return;
    }
    checkEq(read->version, save::kNativeVersion, "the version");
    checkEq(read->startFen, kStartFen, "the start position");
    check(read->moves == save.moves, "the moves");
    checkEq(read->currentPly, 5, "the current ply");
    checkEq(read->whiteName, std::string("Luke"), "the white name");
    check(read->whiteType == save::PlayerType::Human, "the white type");
    checkEq(read->blackName, std::string("Vader"), "the black name");
    check(read->blackType == save::PlayerType::Computer, "the black type");
    checkEq(read->settings.set, std::string("black_bottom"), "the piece set");
    checkEq(read->settings.language, std::string("german"), "the language");
    checkEq(read->settings.cadence, std::string("original120ms"), "the cadence");
    check(!read->settings.walking, "the walking toggle");
    check(read->settings.captures, "the captures toggle");
    check(!read->settings.sounds, "the sounds toggle");

    auto replayed = save::gameFromNative(*read);
    check(static_cast<bool>(replayed), "the native save replays");
    if (replayed) {
        checkEq(replayed->position().fen(), game.position().fen(), "the replayed position");
    }

    // The native save keeps what the .CMG loses: castling rights, the en
    // passant square and the halfmove clock.
    auto tricky = chess::Position::fromFen("rnbqkbnr/pp1ppppp/8/2pP4/8/8/PPP1PPPP/RNBQKBNR w KQkq c6 0 3");
    check(tricky.has_value(), "the en passant position parses");
    if (tricky) {
        save::NativeSave one = save::nativeFromGame(chess::Game(*tricky), save::NativeSave{});
        auto back = save::readNative(save::writeNative(one));
        check(static_cast<bool>(back), "the en passant save reads");
        if (back) {
            checkEq(back->startFen, tricky->fen(), "the en passant square survives");
        }
    }

    check(!save::readNative("{not json"), "malformed JSON is refused");
    check(!save::readNative("[]"), "a JSON array is refused");
    check(!save::readNative("{\"version\": 99}"), "a later version is refused");
    check(!save::readNative("{\"start_fen\": \"nonsense\"}"), "a bad start position is refused");
    check(!save::readNative("{\"moves\": [\"e2e5\"]}"), "an illegal native move is refused");
    check(!save::readNative("{\"current_ply\": 4}"), "a current ply past the moves is refused");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string cdDir = argc > 1 ? argv[1] : "original/win3x/cd";

    testShippedFile(cdDir);
    testMoveRoundTrip();
    testCustomPosition();
    testTakeback();
    testRejections();
    testNative();

    if (gFailures > 0) {
        std::cerr << gFailures << " checks failed\n";
        return 1;
    }
    std::cout << "save_test passed\n";
    return 0;
}
