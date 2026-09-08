# Star Wars Chess

Star Wars Chess is a chess game The Software Toolworks published for Windows
3.1 in 1993. Every capture plays a short animated film in which one character
destroys another. This repository holds a reimplementation of that game in
C++20 and SDL3, written by decompiling the original binaries and rewriting what
they do. No emulator runs here, and no byte of the original program executes.
The game reads the artwork, sound and text from your own copy of the CD, which
this repository does not contain. It builds and runs on macOS, Windows and
Linux.

## What works

You can play a whole game against the computer or against another person at the
same keyboard. The port covers these parts of the original.

- **All four board sets.** `CM.INI [chesssets]` names them `WHTBTM_`, `WHTTOP_`
  and `FACING_` in three dimensions and `2DSET_` flat. Each square projects,
  each piece anchors and each click maps back to a square in all four.
- **All four languages.** English, French, German and Spanish come out of
  `RESENG.DLL`, `RESGER.DLL`, `RESFRN.DLL` and `RESSPN.DLL`, 528 string slots
  each. Both bitmap fonts draw, `LEGFONT` for the opening crawl and `GUITEXT`
  for the buttons and the status bar.
- **The original engine.** `src/engine/original/` is a port of `CHESSAPP.EXE`,
  the second program the 1993 game started and talked to over DDE. It searches
  and scores with the constants read out of that binary, and it plays the five
  levels the `.CMP` files on the CD configure, from Newcomer to Expert. Every
  button that reaches the engine works: hint, force move, take back, replay,
  switch sides, offer a draw, the five level buttons, and the human against
  human, human against computer and computer against computer pairings.
- **Saved games.** `src/save/cmg.cpp` reads and writes the original `.CMG`
  file. The port also writes a JSON save, because a `.CMG` drops the castling
  rights, the en passant square and the halfmove clock, and the JSON keeps
  them.
- **60 frames a second.** All 72 capture films and all twelve pieces' walk
  cycles are interpolated offline with RIFE, a neural frame interpolator. The
  films keep their original length to the millisecond. The `I` key switches
  back to the poses the artists drew, at the 120 millisecond capture cadence
  and the 100 millisecond walk cadence the original used.
- **Sound.** The 110 `WAVE` records in `SWCAUDIO.DLL` and the four loose WAV
  files on the CD play on the animation clock. Each piece speaks its own line
  as it moves, a film's cues fire at the millisecond the original fired them,
  and the victory and title lines play where they belong.

### Where the port differs from the original

Six things do not match the 1993 game. Five of them follow from a question the
decompilation left open, and `docs/research/` records what was and was not
proven. The sixth follows from the port running one program where the original
ran two.

| What differs | Why |
| --- | --- |
| A walking piece covers four pixels per 100 millisecond tick | `docs/research/board-geometry.md` reports that `11d8:84a0` receives a per-frame step budget but no instruction decrements it, so the original's pace is unknown. Four pixels lets the moving piece's voice line finish before it arrives. |
| The sound on a film's first pose plays at time zero | The player decodes pose 0 and frees it without drawing it. Three films attach a sound to that pose. The port plays it once, at the start, rather than dropping it. |
| The engine plays from a rebuilt opening book | The book compiled into `CHESSAPP.EXE` stores moves as indexes into its own generator's emission order, so reading it means reproducing that order exactly. `Book::load` replays the 169 lines of `BOOK.DAT` from the start position instead and indexes every position they pass through. |
| Two piece-square generators are approximated | The original rebuilds six tables at the start of every search. The generators at `1000:33E1` and `1000:36A3` were not decoded, so their constants are still open. |
| The opening book only promotes to a queen | `docs/research/menus.md` finds no promotion string in any resource DLL and no menu slot that offers one, so which piece the original chose is unresolved. A human player picks from a four-piece panel; a book line that reaches the last rank queens. |
| `STWPRES.WAV` plays over the Toolworks logo | The original spoke that line over a logo screen `CHESSAPP.EXE` drew before `XCHESS.EXE` started. The port runs no separate logo program, so the line moved to the logo the port draws. |

## How it was made

Ghidra 12.1.3 imported `XCHESS.EXE` and `CHESSAPP.EXE` headless with its NE
loader and the `x86:LE:16:Protected Mode` language. All 1,183 functions of the
front end and all 286 of the engine decompiled. Borland's 32-bit helper calls
`LXMUL@` and `LDIV@` lose their arguments in the decompiler output, so every
arithmetic formula in the research notes was read off the disassembly instead.

Python decoders came first and became the oracle. `tools/reference/anx.py`
decodes the escape-byte run length encoding that the 72 `.ANX` capture files
and the 1,344 piece bitmaps share. `tools/extract/ne.py` parses NE resource
tables without loading a DLL. The board projection works in signed 32-bit fixed
point where 1.0 is 32767, reading two tables of 360 signed longs for sine and
cosine, and the Python side reproduces that arithmetic including its truncating
integer division. `tools/extract/captures.py` applies the capture timing rules:
pose `i` appears at `i * 120` milliseconds, a `pause=1` sound stops the loop
until it finishes, and a `pause=2` sound makes the next sounding pose wait.
`tools/fonts/` recovers the glyph substitution, because the string tables store
accented letters as printable ASCII codes that each bitmap font fills
differently.

The C++ decoders in `src/assets` and `src/export` were then written
independently and checked against the Python output byte for byte. Those are
the oracle tests, `tests/anx_oracle_test.cpp` and `tests/assets_oracle_test.cpp`
plus the two shell drivers `src/export/export_oracle_test.sh` and
`src/interp/interp_oracle_test.sh`.

The board geometry came out of `CMWIN.DAT`, not out of `CM.INI`. The 26 keys in
`CM.INI [board]` are dead code in this build, because the routine that reads
them has no caller. `FUN_1008_37bc` reads the 144 bytes of `CMWIN.DAT` straight
over the block of board variables, so the vanishing point is 335, the board size
535, the turn angle 360 and the tilt 307. `docs/research/board-geometry.md`
lists every byte.

The engine port keeps the original's numbers. `INF` is 25600 and `PLYMAX` is 32
because the binary tests `iterct >= (PLYMAX - 2)` against 30. Newcomer throws
away 60 percent of its candidate moves and rates its queen at 4770, which is how
the original made its easy levels easy, so the port does the same two things
rather than approximating the result.

The frame interpolation runs offline, never during play. `tools/rife/build.sh`
builds `rife-ncnn-vulkan` at a pinned commit with the `rife-v4.6` model.

That binary reads three colour channels and ignores PNG alpha. So the pipeline
places every pose of a film on one shared canvas, then splits each pose into a
colour image over black and a grayscale alpha image. It runs both sequences
through RIFE at identical interpolation times and recombines the results.

The pipeline samples the resolved timeline at `n / 60` seconds. A sample that
coincides with an authored pose copies that pose through unchanged. Sampling by
timestamp is what keeps the film's duration and every sound cue exactly where
the original put them.

The walk cycles go through the same tool in `--walk` mode. Five generated
pictures between each pair of hand drawn poses turn one 100 millisecond step
into six frames.

The game finds the CD by itself. `src/app/startup.cpp` opens the window first,
then looks for the folder, so the folder dialog has a window to belong to and
the player sees the program start.

SDL3, nlohmann-json and zlib are pinned in `cmake/Dependencies.cmake` and built
from source when `SWCHESS_VENDOR_DEPS` is on, so a released binary loads nothing
from Homebrew or a distribution package. `scripts/package-macos.sh` signs the
app and `scripts/notarize-macos.sh` sends it to Apple. `.github/workflows/build.yml`
builds on macOS, Ubuntu, Fedora and Windows on every push.

The work was done with Claude Code driving many agents in parallel. The
decompilation results and the design decisions are written up under
`docs/research/`, one note per subsystem, with the Ghidra addresses behind every
claim.

## Build from source

You need CMake 3.28, Ninja and a C++20 compiler.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `dev` preset builds against the SDL3, nlohmann-json and zlib your machine
already has, which is faster, and it turns the tests on. The four release
presets build all three from pinned source instead: `macos-release` for arm64
Macs at deployment target 12.0, `linux-release`, `windows-msvc-release`, and
`windows-mingw-release`, which cross-compiles a self-contained 64-bit Windows
executable from a Mac or a Linux machine with MinGW-w64.

To build for Linux from any machine with Docker, run the Ubuntu container with
the repository mounted read-only and keep the build inside the container.

```sh
docker run --rm -v "$PWD:/src:ro" -w /tmp ubuntu:24.04 sh -c '
  apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config zlib1g-dev \
    libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev \
    libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
    libudev-dev libdbus-1-dev &&
  cmake -S /src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release &&
  cmake --build build --target swchess'
```

Only one test runs without the CD. `chess_perft_test` counts the moves of the
rules module and reads nothing. Every other test needs the original files,
because it compares a decoded pixel buffer or a resolved timeline against them,
so CMake registers those tests only when `original/win3x/cd` exists. That is
why the CI jobs run `chess_perft_test` and nothing else.

## Run

Copy your CD into a folder, so that `XCHESS.EXE`, `CC256.DLL` and `CMWIN.DAT`
sit inside it. Case does not matter, because the game indexes the folder by the
uppercased form of every name. Then start the game.

```sh
./build/dev/swchess --cd /path/to/cd
```

The game looks for the CD in five places, in this order. A build that carries
its own copy of the CD beside the program wins outright and reads nothing else.
Otherwise it takes `--cd`, then the `SWCHESS_CD` environment variable, then the
`cd=` line of `startup.conf`. When those first four come up empty it opens a
folder dialog and asks. A folder missing any of the three required files produces a
message naming the missing file, and the dialog opens again. The answer goes
into `startup.conf`, so the game asks once.

A `--script` or `--dump-at` run draws into a file and opens no window, so it
cannot ask. Those runs need `--cd` or `SWCHESS_CD` and stop with a message when
they have neither.

| System | Settings and saves | Artwork cache |
| --- | --- | --- |
| macOS | `~/Library/Application Support/Star Wars Chess` | the same folder, in `assets` |
| Windows | `%APPDATA%\FairLine\Star Wars Chess` | `%LOCALAPPDATA%\FairLine\Star Wars Chess\cache` |
| Linux | `$XDG_CONFIG_HOME/swchess`, else `~/.config/swchess` | `$XDG_CACHE_HOME/swchess`, else `~/.cache/swchess` |

The buttons along the bottom of the board are the original's, so most of the
game is played with the mouse. These keys work as well, and any key or click
skips a capture film that is playing.

| Key | What it does |
| --- | --- |
| 1 to 4 | Picks the WHTBTM, WHTTOP, FACING or 2D set |
| L | Moves to the next language |
| W | Turns walking between squares on and off |
| C | Turns the capture films on and off |
| I | Switches between the 60 fps and the original cadence |
| U | Takes the last move back |
| N | Starts a new game |
| Esc | Quits |

## The asset cache and the interpolation

The game plays straight off the CD with no cache. It decodes what it needs when
it needs it and shows the poses the artists drew. Two helper programs fill a
cache, and the game plays the 60 fps frames only when that cache holds them.

`swchess-extract` reads the CD and writes decoded PNGs, sounds, string tables
and manifests.

```sh
./build/dev/src/export/swchess-extract --cd /path/to/cd --out assets
```

That run takes about five seconds and writes 59 MB. It prints the counts it
found and exits non-zero if any of them moved: 4,799 distinct capture records,
5,414 timeline entries, 5,342 poses the player actually draws, 1,344 piece
bitmaps and 110 `WAVE` records.

`swchess-interpolate` then generates the frames between those poses.

```sh
./build/dev/src/interp/swchess-interpolate --assets assets
./build/dev/src/interp/swchess-interpolate --check assets/captures/BBWB/interp60
```

All 72 films took 71 minutes on an Apple M4 Pro and grew the cache to about
970 MB. RIFE needs a Vulkan GPU, which on macOS means MoltenVK translating
Vulkan to Metal. `tools/rife/build.sh` builds the binary and the model, and
`tools/rife/smoke.sh` proves the build by asking for the frame halfway between
a white square at x=20 and the same square at x=60. The bright pixels in the
answer must sit within 8 pixels of x=40.

The walk cycles go through the Python tool, which is the only one that
implements `--walk`.

```sh
python3 -m tools.interp --walk all --assets assets
```

To watch one film at both cadences side by side, with frame stepping and a
choice of background, use `swchess-viewer --review BBWB`.

## Release

`scripts/build-app.sh` writes `Star Wars Chess.app`, then reads every binary
inside it with `lipo`, `otool` and `vtool` and stops when one is missing a CPU,
loads a library from Homebrew, or asks for a newer macOS than you named.
`scripts/package-macos.sh` signs the helpers first and the app last and writes a
disk image. `scripts/notarize-macos.sh` sends that image to Apple, waits for the
answer, and staples the ticket. `scripts/check-installed-app.sh` mounts the
image, copies the app out, checks its signature, checks that no binary reaches
outside the bundle, checks every file against
`packaging/bundle-allowlist.txt`, and plays 500 milliseconds of a game with the
source tree out of reach. Signing and notarizing both need an Apple Developer
Program membership.

`scripts/package-windows.sh` stages the folder a player unzips. It renames
`swchess.exe` to `Star Wars Chess.exe` and copies `swchess-viewer.exe` and a
readme beside it.

Both packaging scripts take `--bundle-data`, which copies the CD files and the
decoded artwork inside the package. That build reads its own copy and never
asks the player for a folder. It contains the files from the CD, which are
copyrighted, so the allow-list check keeps them out of a package built without
that flag. The packages named `full` on the releases page were built with it.

## Legal

The code in this repository is original work under the MIT license. `LICENSE`
holds that text, and `THIRD_PARTY_NOTICES.md` lists the libraries the port
builds against.

The artwork, sound, text, animation and executables of Star Wars Chess are
copyright Lucasfilm Ltd. and The Software Toolworks. None of them is in this
repository. The code reads them from the player's own CD, and it is a clean
reimplementation written for interoperability with that data, the way ScummVM
and OpenRA read the data files of the games they support. The packages named
`full` on the releases page include the CD files so the game runs with nothing
else, and the packages without that word hold only the program. The rights
holders may ask for the full packages to be removed, and they will be.

Star Wars is a trademark of Lucasfilm Ltd. This project is not affiliated with,
authorized by or endorsed by Lucasfilm, Disney or The Software Toolworks.

## Layout

| Path | Contents |
| --- | --- |
| `cmake/` | The dependency module and the MinGW-w64 toolchain file |
| `docs/` | The plan and the research notes |
| `original/` | Where your copy of the CD goes. Git ignores it. |
| `packaging/` | The `Info.plist`, the icon, the allow-list and the Windows resources |
| `scripts/` | The bundle build, the signing, the notarization and the install check |
| `src/` | The C++20 game and its two helper programs |
| `tests/` | The oracle tests that compare the C++ decoders against the Python ones |
| `tools/` | The Python extractor, interpolator, font tools and RIFE build |
| `src/anim/` | Resolves capture timelines and plays poses, walks and generated frames |
| `src/app/` | The `swchess` and `swchess-viewer` programs, the startup search and the review screen |
| `src/assets/` | Decoders for ANX, BMP, WAV, NE resources, piece DLLs and INI files |
| `src/audio/` | The SDL3 mixer and the cue scheduler |
| `src/board/` | Square projection, hit testing and piece placement for the four sets |
| `src/chess/` | The rules, including castling, en passant, promotion, checkmate and draws |
| `src/engine/` | The engine contract and the port of `CHESSAPP.EXE` under `original/` |
| `src/export/` | `swchess-extract`, the C++ asset extractor |
| `src/game/` | The session state machine, the button shell and the move script runner |
| `src/interp/` | `swchess-interpolate`, the C++ RIFE driver |
| `src/platform/` | The per-system settings, save and cache directories |
| `src/render/` | The software compositor that both the window and the PPM dump draw through |
| `src/save/` | The `.CMG` reader and writer and the JSON save |
| `src/text/` | The language tables and the two bitmap fonts |
| `src/ui/` | The button pages, the resource bitmaps and the settings |

`docs/research/` holds the details, one note per subsystem: `board-geometry.md`
turns a square into a pixel and back, `capture-player.md` describes the film
player decompiled from `XCHESS.EXE` with its timing and its sounds,
`engine.md` covers `CHESSAPP.EXE`, `languages.md` maps the four string tables
and both fonts, `menus.md` lists every screen, button and setting,
`saved-games.md` documents the `.CMG` format, and the two `packaging-` notes
cover shipping on each system.

## Credits

The Software Toolworks made the original game in 1993, and this port only moves
their work to a machine that can still run it. nihui wrote
[rife-ncnn-vulkan](https://github.com/nihui/rife-ncnn-vulkan) and converted the
model this port interpolates with. hzwer and the rest of the RIFE team trained
that model.
Sam Lantinga and the SDL project supply the window, the input and the audio.
Niels Lohmann's nlohmann-json reads and writes every manifest. The port was
written with Claude Code.
