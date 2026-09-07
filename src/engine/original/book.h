// The opening book reader for BOOK.DAT.
//
// BOOK.DAT holds 169 named opening lines. The engine process CHESSAPP.EXE
// never opened it. The front end XCHESS.EXE read it, matched the game so far
// against the lines, and handed the engine the reply over DDE. This port does
// the same work in one place.
//
// The engine contract passes a position and no move history, so load() replays
// every line from the standard start and files each position it passes
// through. probe() then answers straight from that index.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "chess.h"

namespace swchess::engine::original {

// One named line, exactly as the file stores it.
//
// A name is written in one of three shapes. "Polish Opening" stands on its
// own. "K=English Opening" says that the letter K is short for "English
// Opening" everywhere else in the file. "K - Four Knights Variation" uses
// that shorthand, and reads as "English Opening - Four Knights Variation".
// All 26 letters are defined inside the file, so nothing outside it is
// needed to spell a name out.
struct BookLine {
    std::string name;         // the name the file holds, shorthand and all
    std::string displayName;  // the same name with the shorthand spelled out
    int listIndex{0};   // where the front end shows the name, 0 to 168
    std::vector<chess::Move> moves;  // from the standard start, in order
};

class Book {
public:
    // Reads the file. Throws std::runtime_error when it is missing or its
    // bytes do not parse.
    static Book load(const std::string& path);

    // "Chessmaster 3000 Opening Book" and the copyright line, without the
    // 0x1A byte that ends them.
    const std::string& banner() const { return banner_; }

    using BookLineList = std::vector<BookLine>;
    const BookLineList& lines() const { return lines_; }

    // Whether the book still covers this position at all. `plyLimit` is how
    // many plies of book the level allows, which comes from the .CMP file.
    bool reaches(const chess::Position& position, int plyLimit) const;

    // The moves the book plays in this position, most played first. Empty
    // when the position is off the book.
    std::vector<chess::Move> probe(const chess::Position& position) const;

    // Picks one of those moves. The choice is spread over the candidates in
    // proportion to how many lines carry each one, so the engine varies its
    // openings the way the original did. Returns nothing off the book.
    std::optional<chess::Move> pick(const chess::Position& position,
                                    std::uint32_t& randomState) const;

    // Names the opening this position stands in. Answers with the longest
    // line that runs through the position, spelled out. Empty off the book.
    std::string openingName(const chess::Position& position) const;

    // How many distinct positions the index holds.
    std::size_t positionCount() const { return index_.size(); }

private:
    // One book move and how many lines play it here.
    struct Candidate {
        chess::Move move;
        int weight{0};
    };

    void buildIndex();

    void expandNames();

    std::string banner_;
    std::vector<BookLine> lines_;
    std::unordered_map<std::string, std::vector<Candidate>> index_;
    // The deepest line reaching each position, by its place in lines_.
    std::unordered_map<std::string, std::size_t> namedAt_;
};

}  // namespace swchess::engine::original
