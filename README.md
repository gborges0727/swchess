# Star Wars Chess for macOS

This is a native macOS rewrite of Star Wars Chess, the Software Toolworks game
released for Windows 3.1 in 1993. Every line of code here was written from
scratch by reading the original binaries and data files. It plays the original
artwork, animations, sounds, four piece sets and four languages, and it can
play the capture animations interpolated to 60 frames per second.

## What you need

- A Mac with Apple silicon running macOS 15 or later.
- [Homebrew](https://brew.sh), then `brew bundle` in this directory to install
  cmake, ninja, sdl3 and nlohmann-json.
- Python 3, which the asset extractor and the interpolation tool both use.
- Your own copy of the original CD.

This repository ships none of the game's files. Copy your CD into
`original/win3x/cd`, so that `original/win3x/cd/XCHESS.EXE` exists. Git ignores
`original/`, so nothing you put there is committed.

## Build and run

The extractor reads the CD files and writes decoded PNGs, sounds, string
tables and manifests into `assets/`. Run it first, then build, test and play.

```sh
python3 -m tools.extract --cd original/win3x/cd --out assets
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/swchess --cd original/win3x/cd --assets assets
```

These keys work while the game runs, and any key or click skips a capture.

| Key | What it does |
| --- | --- |
| 1 to 4 | Picks the WHTBTM, WHTTOP, FACING or 2D piece set |
| L | Moves to the next language |
| W | Turns walking between squares on and off |
| C | Turns the capture animations on and off |
| I | Switches between the interpolated and the original capture cadence |
| U | Takes the last move back |
| N | Starts a new game |
| Esc | Quits |

Build the double-clickable application with the packaging script. It writes
`build/Star Wars Chess.app` with SDL3 copied inside and prints its size.

```sh
./scripts/build-app.sh
```

## Interpolated captures

The original plays a capture at one pose every 120 milliseconds. The
interpolation tool fills the gaps between poses with frames from
[rife-ncnn-vulkan](https://github.com/nihui/rife-ncnn-vulkan), so a capture runs
at 60 frames per second over its original duration.

`tools/rife/build.sh` installs molten-vk and vulkan-headers, clones
rife-ncnn-vulkan at a pinned commit, builds it for arm64, and puts the binary
and the rife-v4.6 model under `.cache/rife/bin`. Run it once, then interpolate
one capture and check what came out.

```sh
./tools/rife/build.sh
python3 -m tools.interp --capture BBWB --assets assets \
    --out assets/captures/BBWB/interp60
python3 -m tools.interp --check assets/captures/BBWB/interp60
```

Repeat that for each name in `assets/captures`. All 72 captures cost about an
hour of GPU time. To watch one at both cadences side by side, with frame
stepping and a choice of background, use the viewer.

```sh
./build/swchess-viewer --cd original/win3x/cd --assets assets --review BBWB
```

## Layout

| Path | Contents |
| --- | --- |
| `assets/` | The extracted artwork, sounds and manifests, written by the extractor |
| `cmake/` | Helper modules the top-level `CMakeLists.txt` includes |
| `docs/` | The plan and the research notes |
| `original/` | Where your copy of the CD files goes |
| `packaging/` | The `Info.plist`, launcher and icon script for the app bundle |
| `scripts/` | The bundle build and the fresh checkout check |
| `src/` | The C++20 game, in the modules below |
| `tests/` | Oracle tests that compare the C++ decoders against the Python ones |
| `tools/` | The Python extractor, interpolator, font tools and RIFE build |
| `src/anim/` | Resolves capture timelines and plays poses, walks and interpolated frames |
| `src/app/` | The `swchess` and `swchess-viewer` programs and the review screen |
| `src/assets/` | Decoders for ANX, BMP, WAV, NE resources, piece DLLs and INI files |
| `src/audio/` | The SDL3 mixer and the cue scheduler |
| `src/board/` | Square geometry, hit testing and piece placement for the four sets |
| `src/chess/` | The rules, including castling, en passant, promotion, checkmate and draws |
| `src/game/` | The session state machine and the move script runner |
| `src/render/` | The software compositor that both the window and the PPM dump draw through |
| `src/save/` | The saved-game format |
| `src/text/` | The language tables and the two bitmap fonts |
| `src/ui/` | Buttons, resource bitmaps and the settings the game keeps |

## Research notes

- `docs/research/board-geometry.md` turns a square into a screen pixel, and a
  click back into a square, for each set.
- `docs/research/capture-player.md` describes the capture player decompiled
  from `XCHESS.EXE`, frame timing and sounds included.
- `docs/research/languages.md` maps the four string tables and both fonts.
- `docs/research/menus.md` lists every screen, every menu button, and every
  setting the original keeps on disk.
- `docs/research/saved-games.md` documents the `.CMG` saved-game format.

## Legal

The code in this repository is original work, and it reads the copy of the game
that you own. The artwork, sounds and binaries of Star Wars Chess belong to
Lucasfilm and Software Toolworks, and this repository never redistributes them.
