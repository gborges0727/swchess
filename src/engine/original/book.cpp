#include "engine/original/book.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "engine/original/board.h"

namespace swchess::engine::original {
namespace {

// The byte that ends the banner. It is the DOS end-of-file mark, so TYPE
// BOOK.DAT printed the copyright line and stopped.
constexpr std::uint8_t kBannerEnd = 0x1a;
// The two bytes that end one line's move list.
constexpr std::uint8_t kLineEnd = 0xff;

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) throw std::runtime_error("cannot open " + path);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        throw std::runtime_error("empty file " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size()) throw std::runtime_error("short read on " + path);
    return bytes;
}

// Finds the legal move that runs between these two squares. The file stores
// only the two squares, so castling is an ordinary king move here and a pawn
// reaching the last rank promotes to a queen.
std::optional<chess::Move> matchMove(const chess::Position& position,
                                     chess::Square from, chess::Square to) {
    for (const chess::Move& m : position.legalMoves()) {
        if (m.from != from || m.to != to) continue;
        if (m.promotion && *m.promotion != chess::PieceType::Queen) continue;
        return m;
    }
    return std::nullopt;
}

}  // namespace

Book Book::load(const std::string& path) {
    const std::vector<std::uint8_t> bytes = readFile(path);

    std::size_t at = 0;
    while (at < bytes.size() && bytes[at] != kBannerEnd) ++at;
    if (at == bytes.size()) throw std::runtime_error(path + " has no banner");

    Book book;
    book.banner_.assign(bytes.begin(), bytes.begin() + static_cast<long>(at));
    ++at;  // step over the 0x1A

    // Each line is a NUL-terminated name, one byte saying where the front end
    // lists that name, then pairs of square bytes, then FF FF. Two bytes of
    // trailer follow the last line, so stop with two bytes to spare.
    while (at + 2 < bytes.size()) {
        BookLine line;
        const std::size_t nameStart = at;
        while (at < bytes.size() && bytes[at] != 0) ++at;
        if (at >= bytes.size()) throw std::runtime_error(path + " ends inside a name");
        line.name.assign(bytes.begin() + static_cast<long>(nameStart),
                         bytes.begin() + static_cast<long>(at));
        ++at;  // step over the NUL
        if (at >= bytes.size()) throw std::runtime_error(path + " ends after a name");
        line.listIndex = bytes[at];
        ++at;

        chess::Position position = chess::Position::start();
        while (true) {
            if (at + 1 >= bytes.size()) throw std::runtime_error(path + " ends inside a line");
            if (bytes[at] == kLineEnd && bytes[at + 1] == kLineEnd) {
                at += 2;
                break;
            }
            const auto from = decodeSquare(bytes[at]);
            const auto to = decodeSquare(bytes[at + 1]);
            at += 2;
            if (!from || !to) throw std::runtime_error(path + " holds an off-board square");
            const auto move = matchMove(position, *from, *to);
            if (!move) throw std::runtime_error(path + " holds an illegal move in " + line.name);
            position = position.apply(*move);
            line.moves.push_back(*move);
        }
        book.lines_.push_back(std::move(line));
    }

    // The last two bytes count the entries, high byte first.
    if (at + 2 != bytes.size()) {
        throw std::runtime_error(path + " has bytes left over after the last line");
    }
    const std::size_t counted = (static_cast<std::size_t>(bytes[at]) << 8) | bytes[at + 1];
    if (counted != book.lines_.size()) {
        throw std::runtime_error(path + " counts its lines wrong");
    }

    book.expandNames();
    book.buildIndex();
    return book;
}

void Book::expandNames() {
    // A name of the shape "K=English Opening" defines the letter K.
    std::unordered_map<char, std::string> family;
    for (const BookLine& line : lines_) {
        if (line.name.size() > 2 && line.name[1] == '=') {
            family[line.name[0]] = line.name.substr(2);
        }
    }
    for (BookLine& line : lines_) {
        if (line.name.size() > 2 && line.name[1] == '=') {
            // The letter's own line displays as the family name alone.
            line.displayName = line.name.substr(2);
            continue;
        }
        const auto found = line.name.size() > 1 ? family.find(line.name[0]) : family.end();
        if (line.name.size() > 1 && line.name[1] == ' ' && found != family.end()) {
            line.displayName = found->second + line.name.substr(1);
            continue;
        }
        line.displayName = line.name;
    }
}

void Book::buildIndex() {
    for (std::size_t which = 0; which < lines_.size(); ++which) {
        const BookLine& line = lines_[which];
        chess::Position position = chess::Position::start();
        for (const chess::Move& move : line.moves) {
            std::vector<Candidate>& here = index_[position.repetitionKey()];
            auto found = std::find_if(here.begin(), here.end(),
                                      [&](const Candidate& c) { return c.move == move; });
            if (found == here.end()) {
                here.push_back({move, 1});
            } else {
                ++found->weight;
            }
            position = position.apply(move);
            // The deepest line reaching a position gives it its name.
            const std::string key = position.repetitionKey();
            const auto named = namedAt_.find(key);
            if (named == namedAt_.end() ||
                lines_[named->second].moves.size() < line.moves.size()) {
                namedAt_[key] = which;
            }
        }
    }
    // Most played first, so probe() reports the main line at the front.
    for (auto& entry : index_) {
        std::stable_sort(entry.second.begin(), entry.second.end(),
                         [](const Candidate& a, const Candidate& b) {
                             return a.weight > b.weight;
                         });
    }
}

bool Book::reaches(const chess::Position& position, int plyLimit) const {
    // How many half moves the game has run, counting from the standard start.
    const int ply = (position.fullmoveNumber() - 1) * 2 +
                    (position.sideToMove() == chess::Color::Black ? 1 : 0);
    if (plyLimit > 0 && ply >= plyLimit) return false;
    return index_.count(position.repetitionKey()) != 0;
}

std::vector<chess::Move> Book::probe(const chess::Position& position) const {
    std::vector<chess::Move> out;
    const auto found = index_.find(position.repetitionKey());
    if (found == index_.end()) return out;
    for (const Candidate& c : found->second) out.push_back(c.move);
    return out;
}

std::string Book::openingName(const chess::Position& position) const {
    const auto found = namedAt_.find(position.repetitionKey());
    if (found == namedAt_.end()) return {};
    return lines_[found->second].displayName;
}

std::optional<chess::Move> Book::pick(const chess::Position& position,
                                      std::uint32_t& randomState) const {
    const auto found = index_.find(position.repetitionKey());
    if (found == index_.end()) return std::nullopt;
    const std::vector<Candidate>& candidates = found->second;
    if (candidates.empty()) return std::nullopt;

    int total = 0;
    for (const Candidate& c : candidates) total += c.weight;

    // A 32-bit linear congruential step, the same shape the original used for
    // its own move spread. The caller owns the state so a test can fix it.
    randomState = randomState * 1103515245u + 12345u;
    int roll = static_cast<int>((randomState >> 16) % static_cast<std::uint32_t>(total));
    for (const Candidate& c : candidates) {
        roll -= c.weight;
        if (roll < 0) return c.move;
    }
    return candidates.front().move;
}

}  // namespace swchess::engine::original
