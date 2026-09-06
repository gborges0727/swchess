# Saved games: the `.CMG` format

This note documents the saved-game file that Star Wars Chess writes and reads, so
the native port can import the original files. Everything comes from decompiling
`original/win3x/cd/XCHESS.EXE` with Ghidra 12.1.3 and from decoding the one
shipped save, `original/win3x/cd/STARWARS.CMG`.

Ghidra addresses are segment:offset. Data segment 60 maps to `11d8:0000` and to
file offset `0x33bc0`, so file offset F is Ghidra address `11d8:(F - 0x33bc0)`.
The project used here lives in `.cache/ghidra-saves/`.

## Where the code lives

| What it does | Address | File offset |
| --- | --- | --- |
| Writes `STARWARS.CMG`, header block then engine block | `FUN_1008_2e80` | `0xa8e0` |
| Builds the 101-byte header block | `FUN_1008_2d89` | `0xa7e9` |
| Asks the engine to serialize itself into a buffer | `FUN_1008_2c64` | `0xa6c4` |
| Opens a `.CMG`, checks the format byte, reads it whole | `FUN_1008_30a5` | `0xab05` |
| Hands the engine block back to the engine | `FUN_1008_2cd6` | `0xa736` |
| Engine call table, `xDispatch` | `FUN_10e8_0000` | `0x26500` |
| Engine, block size then write, `xSave` | `FUN_1188_0000`, `FUN_1188_0033` | `0x2da80`, `0x2dab3` |
| Engine, write and read a linear move list | `FUN_1188_012d`, `FUN_1138_01e7` | `0x2dbad`, `0x2b527` |
| Engine, read the block, `xLoad` | `FUN_1138_0000` | `0x2b340` |
| Engine, write and read a variation tree | `FUN_11b8_09e7`, `FUN_11b8_0099` | `0x30ed7`, `0x30589` |

`XCHESS.EXE` carries its own complete copy of the Chessmaster engine in segments
1080 through 11d0, which is why engine symbol names such as `pThreadSave` sit in
the same data segment as the game's strings. That copy holds the state the save
file describes and is the copy that writes and reads the file.

The game also runs a second program. `FUN_1008_3e10` calls `WinExec` on
"CHESSAPP.EXE" (`11d8:00fe`) at `1008:3ebf`, and `FUN_11d0_0be1` opens a DDE
conversation with it through `TWRXDDE.DLL`, the Software Toolworks DDE library.
That conversation registers `CF_CHESS_INT`, `CF_CHESS_CHAR`, `CF_CHESS_UL`,
`CF_CHESS_BOARD`, `CF_CHESS_MOVE_LIST` and `CF_CHESS_MEM_BLOCK` (`11d8:52d6`
onward) as DDE data formats, links to the service "CHESSLIB" (`11d8:5332`), and
exchanges items including `white_clock`, `black_clock`, `inbook`, `nxtbook`,
`xBookEnabled` and `timed_out`.

None of that touches the save file. `FUN_1008_2c64` asks the local engine for the
block size with `FUN_11d0_085b(0x16)`, allocates a buffer, points the engine at it
with `FUN_11d0_13d9`, then calls `FUN_11d0_085b(0x17)` to fill it, and each of
those runs `FUN_10e8_0000` inside `XCHESS.EXE` without a DDE call. The name
`pThreadSave` reaches the binary only inside an assertion string, pushed once at
`11b8:09f7` into the panic printer `FUN_11d0_04e2`. The save is not a dump of
another process's memory.

## The 101-byte header

`FUN_1008_2e80` writes two blocks back to back with `_lwrite` and pads nothing
between them. `FUN_1008_2d89` builds the first. Every string goes in with
`lstrcpy`, which stops at its NUL, and the block comes from `GlobalAlloc` with
flags 0 at `1008:2dae`, which omits `GMEM_ZEROINIT`, so the bytes after each NUL
are whatever the heap held. Read each field as a NUL-terminated string inside its
slot and ignore the rest.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 32 | Game title. The game always writes "Starwars Chess Game" (`11d8:036c`). |
| 0x20 | 1 | 0x1A, so `TYPE` under DOS stops after the title. |
| 0x21 | 1 | 0x20, the format byte, and the only byte the loader checks. |
| 0x22 | 32 | White player's name. |
| 0x42 | 1 | White player type. 1 means human, 2 means computer. |
| 0x43 | 32 | Black player's name. |
| 0x63 | 1 | Black player type, same two values. |
| 0x64 | 1 | Always written as 0. |

The two type values come from `FUN_1048_1822`, which puts "Human" in the dialog
for 1 and "Computer" for 2. `FUN_1048_2209` and `FUN_1048_222d` copy the names
and types out of a runtime array of 0x44-byte player records at `11d8:756e`,
index 0 for White and index 1 for Black. `XCHESS.EXE` never reads these fields
back. `FUN_1008_30a5` seeks to offset 0x21, reads that one byte, requires 0x20,
and hands the engine everything from offset 0x65 onward. Any other byte produces
"ERROR: Unknown game file format" (`11d8:03f3`), and a file of 32000 bytes or
more is refused at `1008:30ce`.

## The engine block

`FUN_1188_0033` writes it and `FUN_1138_0000` reads it, starting at file offset
0x65.

| Offset from block start | Size | Meaning |
| --- | --- | --- |
| +0 | 2 | 0x1A then 0x20. `FUN_1138_0000` returns error 7 unless both match. |
| +2 | 1 | Position flag. |
| +3 | 0 or 64 | The starting position, present only when the flag is 0x10 or 0x20. |
| next | 2 | Signed little-endian move count. |
| next | 2 | Signed little-endian current ply. |
| next | rest | The moves. |

The position flag takes three values, and a fourth makes `FUN_1138_0000` return
error 3. 0xFF means the standard opening with White to move and no board bytes
follow, a case `FUN_11a0_084b` recognizes by comparing the board against a
freshly built opening array. 0x10 means 64 board bytes follow and White moves
first, and 0x20 means the same with Black first.

The 64 board bytes run a8, b8 through h8, then a7 through h7, down to a1 through
h1. The writer at `1188:0072` walks the engine's 0x88 board from index 0 to 0x7f
and skips every index where `index & 0x88` is nonzero, which gives that order.

Each board byte encodes one square. Zero means empty, bit 0x10 means a white
piece and bit 0x20 a black one, and the low three bits name the piece: 0 King,
1 Queen, 2 Rook, 3 Bishop, 4 Knight, 5 Pawn. So a white pawn is 0x15, a black
rook is 0x22, a white king is 0x10. `FUN_11a0_089d` turns a colour letter and a
piece letter into exactly these bits, and the back-rank table at `11d8:509c`
(file offset `0x38c5c`) reads 2, 4, 3, 1, 0, 3, 4, 2.

The move count is signed, and a negative value means a variation tree whose node
count is the absolute value. The current ply says how many moves are played when
the file opens, so a smaller value means the player had taken moves back.
`FUN_1138_0000` returns error 4 unless both numbers are non-negative and the
current ply is at most the move count.

## One move record

A linear move list is the move count repeated four-byte records, each optionally
followed by a NUL-terminated annotation. `FUN_1188_012d` writes them and
`FUN_1138_01e7` reads them. Bytes 0 and 1 hold the move as a little-endian 16-bit
word, bytes 2 and 3 hold the time, and an annotation follows only when bit 15 of
the move is set.

| Bits of the move word | Meaning |
| --- | --- |
| 0 to 2 | Destination file, 0 for the a-file. |
| 3 to 5 | Destination rank, 0 for rank 8 and 7 for rank 1. |
| 6 to 8 | Origin file, same encoding. |
| 9 to 11 | Origin rank, same encoding. |
| 12 to 14 | Promotion piece, using the same 0 to 5 codes as the board. The game writes 7 for no promotion and the reader treats 6 the same way. |
| 15 | An annotation string follows the four bytes. |

`FUN_1148_00f9` turns a file index into a letter by adding 0x61, and
`FUN_1148_06a1` turns a rank index into a digit by subtracting it from 0x38, so
rank index 0 really is rank 8. When the origin square equals the destination the
record is a game-over marker rather than a move, and bits 12 to 14 carry the
reason. `FUN_1138_01e7` accepts only three values there and passes them to
`FUN_1150_0782`, and anything else produces "xLoad: weird game over"
(`11d8:40eb`).

| Move word | String id `FUN_1148_031e` posts | English text |
| --- | --- | --- |
| 0x4000 | 32785 | blank in this build |
| 0x5000 | 32781 for White, 32782 for Black | blank in this build |
| 0x6000 | 32783 for White, 32784 for Black | blank in this build |

All five slots are blank in all four language DLLs, so this build cannot display
these endings, though the engine still writes and reads them. The reasons the
game does display, checkmate at 0x1000, stalemate at 0x2000 and the four draws at
0x3000, never reach a saved record, because `FUN_1150_0782` runs only for the
other three.

The two time bytes are a difference, not an absolute. `FUN_1188_012d` writes the
mover's running clock minus the same side's clock two plies earlier, which is
what that side spent on this one move. `FUN_1138_01e7` adds each value back into
a 32-bit total per side, `11d8:3c48` for White and `11d8:3c43` for Black. The
unit is one timer tick. `FUN_1008_3ef6` calls `SetTimer` with 0x64 at
`1008:43df`, which is 100 milliseconds, and `FUN_10b8_0000` adds one per tick.

## The variation tree

When the move count is negative the moves become a token stream. `FUN_11b8_05c5`
writes it and `FUN_11b8_0099` reads it. `'M'` (0x4D) introduces one node, and the
same four-byte record and optional annotation follow it. `'T'` (0x54) closes a
subtree, followed by two bytes giving the little-endian depth to return to.
`'X'` (0x58) ends the stream. `FUN_11b8_09e7`, the function named `pThreadSave`
(`11d8:521f`), writes one node and recurses, taking every sibling that is not the
principal continuation first, then the principal one, then the closing `'T'`.

## The shipped `STARWARS.CMG`, byte by byte

The file is 108 bytes, 101 of header and 7 of engine block, and every byte is
accounted for.

| Offset | Bytes | Field and value |
| --- | --- | --- |
| 0x00 | `53 74 61 ... 00` | Title, "Starwars Chess Game", then twelve unwritten zeros |
| 0x20 | `1a` | DOS end-of-file |
| 0x21 | `20` | Format byte, correct, so the file loads |
| 0x22 | `45 61 72 ... 00` | White name, "Earthling 1" |
| 0x2e | `0a 00 6e 00 46 00 19 00 14 00 0a 00 02 00 03 00 00 00 3a 00` | uninitialized heap, ignore it |
| 0x42 | `01` | White type, human |
| 0x43 | `4e 65 77 ... 00` | Black name, "Newcomer", then twenty-three zeros |
| 0x63 | `02` | Black type, computer |
| 0x64 | `00` | spare |
| 0x65 | `1a 20` | engine block markers |
| 0x67 | `ff` | position flag, standard opening, White to move |
| 0x68 | `00 00` | move count, 0 |
| 0x6a | `00 00` | current ply, 0 |

So the shipped file is a new game between a human called "Earthling 1" and the
computer personality "Newcomer", with no moves played. The twenty bytes at 0x2e
look structured only because the heap block previously held personality weights,
the same run of small words that fills the default weight table at `0x35e14`.

## What the save leaves out, and what a port must watch

Four things an importer might expect are absent, and the engine rebuilds them.
`FUN_11a0_00f8` derives castling rights after the load by checking whether the
king still stands on e1 or e8 and whether a rook stands on a1, a8, h1 or h8, so a
custom position whose king moved and came home returns with rights it should not
have. `FUN_11a0_0270` sets the en passant square at `11d8:3c7f` to 0xFFFF,
meaning none. The halfmove clock and repetition history restart from the saved
position, so a fifty-move or threefold claim resting on earlier moves is lost.
Difficulty, time control and search depth live in `SWC.INI` under `[look_feel]
play_level`, along with language, walking, captures and sounds.

Nothing else blocks a clean import. No field is a pointer, a handle or a machine
address, and there is no hash state, transposition table or checksum, only
little-endian 16-bit values and byte arrays. The two traps for a reader are the
uninitialized bytes after each name, and the signed move count whose negative
values mean a tree.

## How the player reaches a save

The game never shows a Load dialog. `FUN_1008_3281` loads the hardcoded name
"STARWARS.CMG" (`11d8:0436`) at startup and `FUN_1008_2e80` writes that same name
back, so one save slot exists. A COMMDLG dialog does exist, `FUN_1048_327a`, and
in this build it runs only as Delete Game. It fills a 72-byte `OPENFILENAME` at
`1048:32f9`, calls `GetOpenFileName` at `1048:3523` with the filter "Chess
Games\0*.cmg" (`11d8:26e7`) and its own template "CommFileOpen" (`11d8:26c0`), and
its one live caller `FUN_1008_32d4` reopens the chosen file with `OF_DELETE` at
`1008:331f`.

That dialog lists game titles rather than file names. Its hook subclasses the
file list box with `COMMONFILELISTBOXPROC` at `1048:307c`, which intercepts every
`LB_ADDSTRING` and calls `FUN_1048_2fd8`. That routine opens the file, does one
`_lread` of 0x20 bytes with no preceding `_llseek`, closes it, and cuts the result
at the first carriage return. So the 32-byte title slot at offset 0 is what a
user sees, and a port that writes `.CMG` files should fill it.

## The other Chessmaster file extensions

The string block at file offset `0x35d1e` names five extensions, three of them
dead leftovers from the Chessmaster 3000 codebase.

| Extension | Description string | Status in this build |
| --- | --- | --- |
| `.CMG` | "Chessmaster Games" | live, but only through the Delete dialog |
| `.CMT` | "Chessmaster Tournaments" | dead, nothing passes the type selecting it at `1048:3412` |
| `.CMO` | "Chessmaster Opponents" | live, read at `1048:03e0` and `1048:2ed9` |
| `.CMP` | "Chessmaster Opponents" | live, `EXPERT.CMP` is the startup library, `1048:1d03` |
| `.TXT` | "Chessmaster Move List" | dead, cases 3 to 5 of the type switch have no caller |

There is no tournament format in this build. `.CMO` and `.CMP` hold opponent
personalities, which is why a `.CMG` header names a personality such as
"Newcomer". `EXPERT.CMP` sits on the CD at 102 bytes and begins "Chessmaster",
so the startup library is present. Its record layout is unresolved.

## `SWC.INI`, `TWRX.INI` and `CM.INI`

`FUN_1008_3954` calls `GetModuleFileName`, cuts the path at the last backslash
and keeps the directory. `FUN_1008_37bc` joins that directory with either
"SWC.INI" (`11d8:00e7`) or "TWRX.INI" (`11d8:00de`), choosing by its argument at
`1008:3837`. Startup passes 0 at `1008:40c3`, so the running game reads
`SWC.INI`, and only the menu handler at `1040:0823` ever passes nonzero. Writes
always go to `SWC.INI`, because `FUN_1008_36c7` hardcodes it at `1008:36d5`,
which is why the two shipped files hold identical text.

| `[look_feel]` key | Read at | Default | Meaning |
| --- | --- | --- | --- |
| `language` | `1008:3898` | 0 | language index, tried first in `SWCMPC.INI` with sentinel 99 |
| `turn` | `1008:38af` | 0 | board rotation in degrees, nonzero modulo 360 sets a flag at `1008:3944` |
| `walking` | `1008:38c6` | 0 | 0 or 1, menu check item at `1040:085f` |
| `captures` | `1008:38dd` | 0 | 0 or 1, menu check item at `1040:084b` |
| `sounds` | `1008:38f4` | 0 | 0 or 1, menu check item at `1040:0873` |
| `play_level` | `1008:390c` | 464 | menu command id from 460 to 464 |
| `board` | `1008:3923` | 0 | written at `1008:37a2`, absent from both shipped files |

Exactly one `WritePrivateProfileString` call site exists in the program, at
`1008:36bb` inside `FUN_1008_3683`, which formats the value with "%d" and writes
one key. Its only caller runs from the menu handler at `1040:08da`, so the file
is rewritten when the player changes a menu setting and at no other time, and
there is no `WritePrivateProfileInt` import. `SWCMPC.INI` (`11d8:04a2`) is a
third file, and the code passes no directory with it, so Windows reads and writes
it in the Windows directory. It carries only `[language] language`, read at
`1008:387a` and written at `1008:36f2`.

`CM.INI` holds one key the save path cares about. `FUN_1008_3209` reads
`[defaults] game_in_progress` (`11d8:041b`) with the default "none", once, from
startup at `1008:5119`, and passes any other value straight to `FUN_1008_30a5` as
a file name. So the key names a resume file, but nothing in `XCHESS.EXE` ever
writes it and the shipped `CM.INI` has no such line, so the resume path never
runs.

## `CMWIN.DAT`

`CMWIN.DAT` and `CMWIND.DAT` are 144 bytes each and byte for byte identical.
Nothing in `XCHESS.EXE` writes either one. The single reference is a read-only
`OpenFile` at `1008:37d8` inside `FUN_1008_37bc`, which rejects the file unless
its size is exactly 0x90, then does one `_lread` of 0x90 bytes into `11d8:67ff`
and closes it. The `SWC.INI` values then overwrite part of what it loaded. The
last six words repeat the INI settings, `+0x84` board through `+0x8e` language,
reading 1, 1, 1, 1, 484 and 0 in the shipped file, and the word at `+0x32` is
`turn` and holds 360. The file is shipped data from Chessmaster 3000 or from the
installer, and the port does not need to write it.

## Unresolved

- Game-over codes 0x4000, 0x5000 and 0x6000 point at string ids blank in all four
  language DLLs, so their meanings are not recoverable from this build.
- The clock unit is one timer tick and `SetTimer` runs at 100 milliseconds, but I
  did not trace `WM_TIMER` through to `FUN_10b8_0000`, so a tenth of a second is
  likely rather than proven.
- No variation-tree `.CMG` exists to test the `'M'`, `'T'` and `'X'` replay order.
- Bytes 0x00 to 0x83 of `CMWIN.DAT`, and the record layout of the 102-byte
  `EXPERT.CMP`.
