# The chess engine

Star Wars Chess does not play chess itself. `XCHESS.EXE` draws the board and
runs the menus. A second program, `CHESSAPP.EXE`, picks the moves. The front
end starts it with `WinExec` and the two talk over DDE for the rest of the
session. This note describes that second program, the files it plays by, and
what `src/engine/` ports.

Every Ghidra address below is written segment:offset. Data segment 2 starts at
file offset 0xAE00, so a data address `1008:N` sits at file offset 0xAE00 + N
in the executable.

## 1. The engine program

`CHESSAPP.EXE` is 92,688 bytes. It is a 16-bit NE executable with two segments
and no overlays.

| Segment | Ghidra block | File offset | Length | Holds |
| --- | --- | --- | --- | --- |
| 1 | `Code1` at 1000:0000 | 0x000600 | 0xA122 | 286 functions |
| 2 | `Data2` at 1008:0000 | 0x00AE00 | 0xB7CE | every table and every global |

Borland C++ 1991 compiled it. The string at 1008:002b says so. The programmer
left the assertion messages in, so the binary names its own routines. `movegn`
generates moves, `treecn` drives the search, `itrtim` decides when to stop,
`treef` walks a node, and `getmove` picks the move to play. Two more names fix
the search constants: `INF` is 0x6400, which is 25600, and `PLYMAX` is 32,
because the binary tests `iterct >= (PLYMAX - 2)` against 30.

The program opens no file. Everything it needs sits in Data2 or arrives over
DDE.

## 2. The main functions

| Address | Name in the binary | What it does |
| --- | --- | --- |
| 1000:813C | `treecn` | the outer search control, and the iteration loop |
| 1000:849D | `itrtim` | decides when the iteration stops |
| 1000:8C71 | `treef` | one node of the search |
| 1000:4B7C | `movegn` | hands the caller one pseudo-legal move at a time |
| 1000:949E | the root driver | sets the two ply thresholds and walks the root list |
| 1000:9708 | the leaf evaluation | scores a position |
| 1000:70B4 | the per-piece scorer | one `switch` arm per piece type |
| 1000:1FC0 | the search setup | rebuilds the piece values and the piece-square tables |
| 1000:58BA | make | plays a move on the board |
| 1000:95A6 | the legality test | throws away a move that leaves the king en prise |
| 1000:892C | the book walker | descends the book compiled into Data2 |
| 1000:8A64 | the random source | the rolling read of the word table at 0xAAF6 |
| 1000:3B44 | the adjudicator | mate, the fifty move rule, repetition, dead material |
| 1000:3BB5 | `eShouldResign` | decides whether the engine gives up |

## 3. The board

The board is a 128-byte 0x88 array at 1008:717D. A square is `rank * 16 + file`
and is on the board when `(square & 0x88) == 0`. Row 0 is rank 8, so a8 is
0x00, e1 is 0x74 and h1 is 0x77.

A board byte is not a piece code. It holds an index into a piece list, and 0
means the square is empty. White uses indexes 0x10 to 0x1F and Black 0x20 to
0x2F, so `board[square] & 0x10` asks whether White stands there. The five
lists all take that index:

| Address | Width | Holds |
| --- | --- | --- |
| 1008:6DC6 | byte | the square, or 0x7F once the piece is captured |
| 1008:6E06 | byte | the type: 0 pawn, 1 king, 2 knight, 3 rook, 4 bishop, 5 queen |
| 1008:6DE6 | byte | one bit for the type, so a ray test can mask against it |
| 1008:6E36 | word | what the piece is worth right now, which also sorts the list |
| 1008:6E76 | word | the piece's positional base, rebuilt once per search |

Slot base+0 is always the king and base+1 the queen. `ColorToMove` at
1008:A325 stores the moving side's base, 0x10 or 0x20, and 1008:A323 stores the
other side's. Both flip by XOR 0x30.

Castling rights are two bytes at 1008:6F9C, indexed by side. Bit 1 blocks the
king side and bit 0 the queen side. There is no en passant global at all. The
generator works the target out from the previous move as `(from + to) >> 1` and
checks that the result is a legal midpoint. The halfmove counter is 1008:A3BB,
and 100 triggers the draw claim.

A move is never packed into one word inside the search. The engine keeps the
from square, the to square, the moving piece, the captured piece and a type
nibble in five separate globals and saves each into its own 32-entry array, one
slot per ply. The type nibble reads 0 for an ordinary move, 2 for castling
toward the h file, 3 for castling toward the a file, 4 and 5 for the two en
passant directions, and `8 | type` for a promotion, so 13 promotes to a queen.
The move only becomes three bytes when it leaves the search, as from, to and a
flags byte.

## 4. The move generator

`movegn` at 1000:4B7C is a state machine. Each call returns one pseudo-legal
move and remembers where it stopped. Legality is tested after the move is
played, by a full attacked-square scan when the king moves or the side is in
check, and by a cheap ray test through the vacated square otherwise.

Captures come first, and they come most valuable victim first, because the
search sorts both piece lists by value before it starts. The order runs:

1. The move worth trying first, which is the hash move at an interior node and
   the next root move at the root.
2. Pawn captures, walking the enemy list from its most valuable piece down.
3. Piece captures of that same victim.
4. Capture-promotions.
5. En passant.
6. Promotion pushes.
7. Castling, skipped when the side is in check.
8. The first killer move, then the second.
9. Every remaining quiet move.

## 5. The evaluation

Scores are in a unit where a pawn is 256, which is what makes `INF` mean a
hundred pawns. The factory values sit at 1008:72FC:

| Piece | Value | In pawns |
| --- | --- | --- |
| pawn | 256 | 1.00 |
| knight | 784 | 3.06 |
| bishop | 844 | 3.30 |
| rook | 1232 | 4.81 |
| queen | 2368 | 9.25 |
| king | 3840 | never traded |

The six piece-square tables at 1008:9AFB through 1008:A0FA are zero in the
file. The search setup at 1000:1FC0 builds all six from the position in front
of it, every search. That is why this port ships no copy of them.

The leaf terms, by piece, with squares named from the moving side's own end:

**Pawn.** A pawn earns 12 when a friendly pawn holds it up from behind and 15
when one stands beside it. It earns 9 for guarding a friendly pawn and 17 for
each enemy bishop it hits. Hitting two enemy pieces at once is worth 128. The
seventh rank is worth 48 and the sixth 28, with 40 more when the square ahead
is empty.

A pawn also pays. A doubled one pays 47 on its own, 16 when a pawn holds it up, 12 when one stands
beside it, and 8 when both do, and it pays twice over at a node that captures.
A pawn nothing supports pays 24, unless a friendly pawn two
ranks behind on a neighbouring file can still reach it. Such a pawn pays 32
instead when it stands on the files that shelter the enemy king.

**King.** The file term runs 0, 0, -2, -4, -4, -4, 0, 0 from the a file. A
king that leaves its back ranks while the other side still has a queen pays
0, 0, 272, 400, 512, 576, 576, 640 by its own rank. Once one side leads by
more than 960, the losing king is driven toward a corner by the table at
1008:74C2.

**Knight.** An outpost past the middle of the board that no enemy pawn can
reach is worth 96 on the four squares the mask `((sq + 0x11) & 0x66) == 0x44`
picks out, 40 when a friendly pawn shields it and 10 otherwise. A knight on
its own third rank in front of an unmoved d or e pawn pays 24, or 169 when
the g pawn is home too. A fork is worth 0, 11, 17, 45, 161, 255, 255 by the
count of enemy pieces a knight move away, and a rook or a queen counts twice.

**Bishop.** Enemy kings, rooks and queens on a clear diagonal are worth 0, 8,
12, 54, 192, 255 by their count. A bishop on its own third rank in front of an
unmoved pawn pays the same 24 the knight pays. A bishop also pays 18 for each of its own pawns on a central square of its own
colour, and twice that in the endgame.

**Rook.** The search works out what a rook is worth on each file, once per
search. It starts at 10 and adds 16, 24, 27, 28, 27, 25, 24 or 23 by file
where this side has no pawn. It adds half of that where the enemy has none on
the four middle files, and 3 for the enemy king's file. It then drops any file
whose own pawn cannot move to zero and gives that file's two neighbours 7 each.
A rook aiming at the enemy king along a rank or a file is worth 56 more, so
long as the enemy queen is still there to be hit.

**Queen.** Within three squares of the enemy king, the table at 1008:75B0
adds 0, 224, 80 or 8 by the distance, from a different block at an odd ply
and a different one again in the endgame.

Two more terms cut across the pieces. Every piece collects 0, 160, 96, 48, 16
or 0 by its side's pawn count while that side's material lead is under 0xD1,
and 16 flat once the lead passes it. A side whose weighted material falls under
3 cannot win, and the leaf refuses to score the position in its favour.

The leaf charges 16 at every ply, which is the tempo term. The score it returns
is the side to move's total minus the other side's.

The phase is three flags, with no blending between them. The endgame starts
once a side's pieces are worth less than 0x1000, or less than 0xA00 while
either queen is still on the board. The deep endgame starts once at most six
pieces are left, kings and pawns aside, and it gates the passed pawn race, the
king chase and the choice of capture table.

## 6. The search

The algorithm is fail-soft alpha-beta negamax. Interior nodes always get a full
window, so it is not principal variation search and not NegaScout. The root is
the one place a narrow window appears: once a move beats alpha, beta drops to
that score plus 0x10, and a later move that breaks the narrow window is
searched again with the top opened right up.

| Technique | In the original | Constant |
| --- | --- | --- |
| iterative deepening | yes, one ply at a time | the iteration counter stops at 30 |
| aspiration window | yes | the first iteration opens at the standing score minus 0x2E0, every later one at 0xC0 either side of the last backed-up score |
| quiescence | yes | captures at any depth, and quiet checking moves for two plies past the main search |
| transposition table | yes | 8-byte entries, sized by the front end through `hashmask` |
| killer moves | yes, two per ply | |
| history heuristic | no | `nwhist` is the command that loads the game history, not a table |
| null move | yes, reduced by one ply | allowed at 1 to 7 plies, out of check, with a margin of 0, 0x30, 0x50, 0x80 or 0x180 by how far the game has run down |
| futility pruning | no per-move margin | quiescence takes a fixed 0x10 or 0x40 off the stand-pat when the leaf reports a threat |
| check extension | yes, with no bound | a checking node does not spend a ply |
| passed pawn extension | yes | a pawn on the seventh at the last ply buys one more |

A side mated at ply `p` scores `p - (INF - 1)`, so mate in one move is 0x63FE
and a mate further off scores less. `itrtim` stops the iteration for three
reasons: a fixed depth was asked for and has been reached, the backed-up score
passed `INF - 32`, which means a mate is found, or the caller pinned the
iteration to one level.

The engine reads no clock. `chk_time` at 1000:993D is a stub that returns 1,
and all it decides is whether to pump the Windows message queue. The two game
clocks arrive over DDE once when the search starts, and they only feed the draw
score. The whole of the timing lives in `XCHESS.EXE`, which counts a one second
`WM_TIMER` down and sends `xSearchStop` at zero.

The adjudicator at 1000:3B44 returns 0x2000 for the fifty move rule, 0x2001 for
threefold repetition, 0x2002 when neither side has enough material left, and
0x4000 for a resignation. The engine resigns when it is more than 1280 behind
and has lost its queen. Both must be true.

## 7. The two opening books

The engine and the front end each read a book, and the two books are
different.

`BOOK.DAT` is the front end's. It is 11,879 bytes and holds 169 named opening
lines. `XBOOK.DAT` is byte for byte the same file and no program opens it. Only the
installer copies it. The engine never reads either one. `XCHESS.EXE` reads
`BOOK.DAT` at 11D0:065D, and its job is to name the opening the game is in, not
to pick a move.

The layout:

| Bytes | Field |
| --- | --- |
| 0 to the first 0x1A | the banner, "Chessmaster 3000 Opening Book" and the 1991 copyright line |
| then | 169 entries, back to back |
| last two | the entry count, 0x00 0xA9, high byte first, so 169 |

One entry is:

1. The name, ASCII, ended by a NUL byte.
2. One byte saying where the front end lists that name. The 169 values are the
   numbers 0 to 168, each used once. Sorting on it puts the openings list in
   order. Nothing else reads it.
3. The moves, two bytes each, from the standard start position in order.
4. `FF FF`.

A square is one byte, `(row << 4) | col`, where row 0 is rank 8 and column 0 is
file a. So c2 is 0x62 and c4 is 0x42. Both nibbles run 0 to 7, so a square byte
never exceeds 0x77. Two squares is all a move gets, so castling is the king's
two-square step and the rook is implied. No line in the file promotes a pawn.
One line captures en passant, `Z - Main Line` at its 21st move, and two squares
describe that fine.

A name is written one of three ways. "Polish Opening" stands alone.
"K=English Opening" says that the letter K is short for "English Opening"
everywhere below. "K - Ultra-Symmetrical Variation" uses that shorthand and
reads out as "English Opening - Ultra-Symmetrical Variation". All 26 letters
are defined inside the file, so spelling a name out needs nothing else.

The front end matches the game by narrowing a window over the entry list. It
sets the bounds to 0 and 169, and after every move it keeps the entries whose
move at this ply matches the move just played. The entries sit in the file in
tree order, so the matches are always next to each other and the window only
shrinks. When it empties, the game has left the book.

The engine's own book is compiled into Data2, from 0x09E2 to 0x666E, 23,693
bytes. It is a depth-first byte stream, one byte per node. Bits 0 to 5 hold a
move code, bit 6 says another sibling follows, and bit 7 marks a leaf. A node's
children start at the next byte and its next sibling starts after its whole
subtree. A 0xFF byte opens a three-byte jump record whose signed link resolves
to `link - 0x4100`, which lets lines that transpose share a subtree. The stream
holds 21,932 nodes and 587 jump records, and expands to 425,385 positions down
to depth 45.

This port does not read that book, for one reason: the six-bit move code is an
index into the move list the generator produced at that position, not a pair of
squares. Decoding it means reproducing the generator's emission order inside
1000:8C71 exactly, and one move out of order at any ply turns the whole subtree
below it into a different opening. `BOOK.DAT` gives real square pairs and real
opening theory, so the port plays from that instead.

## 8. The level files

Each of `NEWCOMER.CMP`, `NOVICE.CMP`, `MODERATE.CMP`, `HARD.CMP` and
`EXPERT.CMP` is exactly 102 bytes. `XCHESS.EXE` reads one at 1048:1B9D,
converts the fields, and pokes 68 bytes to the engine as the DDE command
`ePersonalitySet`. Each file is one Chessmaster personality. The front end
finds the right one by scanning `*.CMP` and matching the title inside the file,
not the file name.

| Offset | Width | Field | What the engine does with it |
| --- | --- | --- | --- |
| 0x00 | 32 | title | the name the level is found by |
| 0x20 | 2 | magic, 0x201A | nothing checks it |
| 0x22 | 2 | own centre pawn | the engine maps it to the king and skips it |
| 0x24 | 2 | own queen | tenths of a pawn, default 90 |
| 0x26 | 2 | own rook | default 50 |
| 0x28 | 2 | own bishop | default 30 |
| 0x2A | 2 | own knight | default 30 |
| 0x2C | 2 | own pawn | default 10 |
| 0x2E to 0x38 | 12 | the same six for the opponent's pieces | |
| 0x3A | 2 | contempt | the draw score is (raw - 2) * 5000 |
| 0x3C | 2 | book depth, in full moves | the engine refuses a book move past twice this |
| 0x3E | 2 | search accuracy, 0 to 60 | the engine throws away 60 minus this percent of its moves |
| 0x40 | 2 | piece against pawn | the engine takes 100 minus this, then knocks pawn * that / 400 off each piece |
| 0x42 | 2 | material weight | the engine takes 100 minus this, then multiplies every piece by 1 + that / 150 |
| 0x44 | 32 | name | the name the player sees |
| 0x64 | 1 | player type | 1 a human, 2 the computer. All five say 2 |
| 0x65 | 1 | ponder | 1 lets the engine think on the human's clock |

What the five files hold:

| File | Title | Book | Moves thrown away | Piece discount | Material weight | Ponder |
| --- | --- | --- | --- | --- | --- | --- |
| NEWCOMER.CMP | Newcomer | 6 plies | 60 percent | 26 | 1.67 times | no |
| NOVICE.CMP | Novice | 12 plies | 40 percent | 1 | 1.67 times | no |
| MODERATE.CMP | Woodpusher | 24 plies | 30 percent | 1 | 1.46 times | no |
| HARD.CMP | Kamikaze | 70 plies | none | 64 | 1.00 times | yes |
| EXPERT.CMP | Chessmaster | 70 plies | none | 0 | 1.00 times | yes |

And what each level believes a piece is worth, once the engine has scaled its
factory table:

| File | Pawn | Knight | Bishop | Rook | Queen |
| --- | --- | --- | --- | --- | --- |
| NEWCOMER.CMP | 426 | 818 | 1123 | 2830 | 4770 |
| NOVICE.CMP | 426 | 1213 | 1405 | 2461 | 4378 |
| MODERATE.CMP | 373 | 1143 | 1230 | 1797 | 3455 |
| HARD.CMP | 256 | 720 | 780 | 1168 | 2304 |
| EXPERT.CMP | 256 | 784 | 844 | 1232 | 2368 |

Newcomer rates a rook above two bishops and a queen at eleven pawns, which is
how a beginner counts. Kamikaze is the only level that rates its own pieces
below the factory table, because its piece-against-pawn setting of 0 takes 64
off each of them. Kamikaze is the one level that rates the two sides
differently. It puts its opponent's pieces above its own, so it gives material
away.

**No .CMP file sets a search depth or a clock.** Both come from `CMWIN.DAT`,
which `XCHESS.EXE` reads at 1008:37BC and insists is exactly 144 bytes. Its
word at 0x64 is 0x01F5, which asks for a fixed number of seconds a move, and
the word at 0x66 is 5. So every level thinks for five seconds and none of them
caps its depth. The word at 0x6C holds 4 plies for the fixed-depth mode the
shipped file does not select. `CMWIND.DAT` is a byte-identical spare and
nothing opens it.

The levels therefore differ in three things and not in how deep they look: how
many moves the engine throws away, what it thinks its pieces are worth, and how
long it stays in the book.

The `newcomer` flag at 1008:784E selects a sixth mode that nothing ever turns
on. When it is positive, `treecn` runs one fixed-depth pass and returns with no
iterative deepening at all. `XCHESS.EXE` only ever writes 0 into it.

## 9. The DDE conversation

`TWRXDDE.DLL` on the CD does the DDE work for both programs. Its own
description string calls it the Software Toolworks DDE Library. `CHESSAPP.EXE` imports thirteen of its
entry points by ordinal:

| Ordinal | Name | What the engine does with it |
| --- | --- | --- |
| 8 | `TWRXDEBUGSTRING` | reports that the search timed out |
| 10 | `DDEREGISTERAPP` | announces itself as `CHESSLIB` |
| 11 | `DDETERMINATEAPP` | shuts the conversation down |
| 12 | `DDEADDAPPFORMAT` | registers the six clipboard formats below |
| 13 | `DDEADDAPPTOPIC` | adds the topic, also `CHESSLIB` |
| 21, 22, 23 | `DDESTRUCTUREDATA`, `DDEUNSTRUCTUREDATA`, `DDERELEASEDATA` | pack and unpack one item |
| 25, 26 | `DDEDATABUFFERSIZE`, `DDEDATABUFFERFORMAT` | read a received item's header |
| 30 | `DDEINITIATELINK` | opens the link to the front end |
| 33 | `DDESETEXTERNALITEM` | writes a front end variable |
| 34 | `DDEGETEXTERNALITEM` | reads a front end variable |

It registers six clipboard formats, one per kind of item. `CF_CHESS_INT`
carries a two-byte integer, `CF_CHESS_CHAR` a single byte, `CF_CHESS_UL` a
four-byte unsigned long, `CF_CHESS_BOARD` a whole board, `CF_CHESS_MOVE_LIST` a
move list, and `CF_CHESS_MEM_BLOCK` a raw block.

The engine does not import `DDEEXECEXTERNALCMD`, ordinal 35. Commands therefore
travel one way only. The front end sends them and the engine carries them out,
and the engine answers by writing variables rather than by sending a command
back. A command it does not recognise draws "Unknown DDELIB_EXECUTE command".

The name table the engine matches an incoming item against runs from 1008:0770
to 1008:0929. The names beginning with a lower case `e` are the commands:

| Command | Meaning |
| --- | --- |
| `eReplay` | replay a game |
| `ePersonalitySet` | take the 68 bytes converted from a `.CMP` file |
| `eBookOn`, `eBookOff` | turn the opening book on and off |
| `eSkipAdd`, `eSkipClear` | add to and clear a list of moves to leave alone |
| `eShouldResign` | ask whether the engine wants to give up |
| `eAnalyzing` | think without playing |
| `terminate` | quit |

The rest of the table names variables. `hashmask`, `hashtb` and `andtb`
describe the transposition table. `sqarrays`, `sqfr`, `sqto`, `sqcont` and
`array_index` describe the board. `ply1list`, `ply1fr`, `ply1to`, `ply1fl`,
`ply1ln`, `ply1in` and `pl1cnt` describe the root move list. `bestvariation`,
`bkupsc`, `nodect`, `iterct`, `timerc`, `treecn`, `nwhist`, `nwmove`, `nwgame`
and `search_status` describe the search. `compst`, `humst`, `ColorToMove`,
`newcomer`, `settmo` and `chk_time` describe the game and the clock.

Five variables live on the front end's side and the engine reaches across the
link for them:

| Item | Format | Direction | Meaning |
| --- | --- | --- | --- |
| `xBookEnabled` | `CF_CHESS_INT` | engine reads | the Book menu setting, 0 or 1 |
| `inbook` | `CF_CHESS_CHAR` | engine writes | 0xFF while the engine is still in its book, 0 once it leaves |
| `nxtbook` | `CF_CHESS_INT` | engine writes | the move code of the book node's first child, or -1 when the line ends |
| `white_clock`, `black_clock` | `CF_CHESS_UL` | engine reads | the two game clocks, used only to work out the draw score |

## 10. What src/engine/ builds

`src/engine/engine.h` is the contract the game shell programs against.
`makeRandomEngine` returns a stand-in that plays a random legal move after
300 ms. `makeOriginalEngine` returns the port described here. Both answer
through `poll()` on the caller's thread, so the shell never blocks while the
engine thinks.

| File | What it does |
| --- | --- |
| `random_engine.cpp` | the stand-in, and `levelFileName` |
| `original/board.h` | the square byte the original files use, both ways |
| `original/cmp.h`, `cmp.cpp` | the `.CMP` and `CMWIN.DAT` readers |
| `original/book.h`, `book.cpp` | the `BOOK.DAT` reader, the lookup and the opening name |
| `original/eval.h`, `eval.cpp` | the piece values, the level settings and the scoring |
| `original/search.h`, `search.cpp` | the move search |
| `original/original_engine.cpp` | the worker thread and `makeOriginalEngine` |

The search runs on a worker thread. `requestMove` hands it a position,
`poll()` picks the answer up on the caller's thread, and `forceMove` tells it
to stop and report the best move it has, which is what the FORCE button did.
When `forceMove` arrives before the worker has taken the job, the worker
starts that search already stopped instead of ignoring the request.

The engine contract passes a position and no move history, so the book cannot
match the game move by move the way the front end did. `Book::load` replays all
169 lines from the standard start instead and files every position each line
passes through. `Book::probe` then answers from that index, which reaches the
same moves by a different route. A position that several lines run through
offers each of their moves, and `Book::pick` spreads its choice over them in
proportion to how many lines play each. A hint takes the most played line
instead, so asking twice gives the same answer.

`chess::Position` in `src/chess` runs about 1.7 million make-and-generate
cycles a second on this machine, hundreds of times what a 1993 386 managed, so
the port searches the rules module directly instead of keeping a second board
of its own. `original/board.h` converts between the original's square byte and
the file and rank the rules module counts in.

At Chessmaster, the port solves all five of a small tactics set inside three
seconds. At Newcomer, throwing away 60 percent of its moves and rating its
queen at 4770, it misses two of them and still finds the mates. The moves Newcomer throws away and the
piece values it uses are how the original made its easy levels easy, and this
port does the same two things rather than approximating the result.

## 11. What is not ported

- **The engine's own opening book.** Its move codes are indexes into the
  generator's emission order, so reading it means reproducing that order
  exactly. Section 7 says where it lives and how it is laid out.
- **The six piece-square tables.** The original builds them from the position
  at the start of every search, in 1000:1FC0 and the twelve generators it
  calls. Two of those generators, at 1000:33E1 and 1000:36A3, were not decoded,
  so the constants inside them are still open.
- **The root-only development terms** at 1000:1798, which add one-shot bonuses
  for pawn placement, king shelter and a developed king. What each branch tests is
  known. What chess idea it stands for is not.
- **The incremental leaf update.** The original keeps the score up to date one
  move at a time and folds a capture bonus in from four tables chosen by ply
  parity and by how far ahead it stands. This port scores each position from
  scratch, which works out the same terms more slowly.
- **Pondering.** `HARD.CMP` and `EXPERT.CMP` both set the flag at 0x65 that
  lets the engine think on the human's clock. This port does not think between
  moves.
- **Resignation and the draw claims.** The adjudicator at 1000:3B44 and
  `eShouldResign` at 1000:3BB5 are decoded, in section 6, but the engine
  contract in `engine.h` has nowhere to report either, so neither is wired up.
- **`eSkipAdd` and `eSkipClear`.** These two commands build the list of moves
  the front end tells the engine to leave alone. Nothing in the port needs it.

## 12. What stays unresolved

- The 0x74C2 entry at index 0x57 is 232 where every mirror position says 56. It
  reads 0xE8 in the file, so it is a typo in the original data rather than a
  decoding mistake, and this port keeps it.
- The root of the engine's internal book lists the move code 8 twice, once as a
  bare leaf and once with a 6,004-byte subtree below it. The walker reaches
  both.
- The flag that picks 98 against 80 for the king's castling bonus is written at
  1000:3DE5, but which of that routine's branches sets it is not known.
- One branch of the per-piece scorer is unreachable. At 1000:7167 it tests the
  fifth rank inside a block already guarded by the fourth, so its penalty never
  fires.
- The fixed-depth mode opens its root window at `[0x63D0, 0x6400]`. That is
  what the disassembly says, and no reading of it makes sense as a search
  window.
- Nothing in the binary reads the flag bit that `itrtim` sets when the engine
  is more than 1344 ahead. The value must reach the front end through a DDE
  block transfer that nobody traced.
