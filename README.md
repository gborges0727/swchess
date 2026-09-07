# Star Wars Chess for macOS

This is a native macOS rewrite of Star Wars Chess, the Software Toolworks game
released for Windows 3.1 in 1993. Every line of code here was written from
scratch by reading the original binaries and data files. It plays the original
artwork, animations, sounds, four piece sets and four languages, and it can
play the capture animations interpolated to 60 frames per second.

## What you need

- A Mac running macOS 11 or later. The release build carries both Apple
  silicon and Intel code.
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

`--cd` and `--assets` are optional when the game opens a window. Without them
it reads `SWCHESS_CD` and `SWCHESS_ASSETS`, then the file it keeps at
`~/Library/Application Support/Star Wars Chess/startup.conf`, and then it asks
for the CD folder in a Finder chooser and remembers the answer. A folder that
does not hold `XCHESS.EXE`, `CC256.DLL` and `CMWIN.DAT` gets a message saying
which file is missing, and the chooser opens again. The decoded artwork goes
to `~/Library/Application Support/Star Wars Chess/assets` when nothing names
another folder, and a run with no decoded artwork plays the original 120 ms
capture poses.

A `--script` or `--dump-at` run draws into a file and opens no window, so it
opens no chooser either. Those runs need `--cd` or `SWCHESS_CD` and stop with
a message when they have neither.

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

The bundle holds no game data. The first time someone opens it, the game asks
for their CD folder and writes the answer to
`~/Library/Application Support/Star Wars Chess/startup.conf`. Later launches
read that file and start straight away.

## Release

Four scripts turn the source into a disk image a player can download. Run
them in this order. The build directory `build-release` is separate from
`build` so the release never reuses objects from a developer build.

```sh
./scripts/build-app.sh build-release 'arm64;x86_64' 11.0
SWCHESS_SIGN_IDENTITY='Developer ID Application: NAME (E85W63H34G)' \
    ./scripts/package-macos.sh 'build-release/Star Wars Chess.app' dist 0.6.0
SWCHESS_NOTARY_PROFILE=swchess-notary ./scripts/notarize-macos.sh dist/StarWarsChess-0.6.0.dmg
./scripts/check-installed-app.sh dist/StarWarsChess-0.6.0.dmg --cd original/win3x/cd
```

`build-app.sh` takes a build directory, a list of CPU architectures and the
oldest macOS the app must run on. It passes the last two to CMake as
`CMAKE_OSX_ARCHITECTURES` and `CMAKE_OSX_DEPLOYMENT_TARGET`. Two
architectures need SDL3 built from source, which `-DSWCHESS_VENDOR_DEPS=ON`
asks for. Afterwards it reads every binary in the bundle with `lipo`, `otool`
and `vtool`. It stops when a binary is missing a CPU, loads a library from
Homebrew or the build directory, or asks for a newer macOS than you named.
Set `SWCHESS_AUDIT=warn` to see those complaints without stopping the build.

`package-macos.sh` signs the helper programs first and the app last, verifies
the result, and writes `StarWarsChess-<version>.dmg` holding the app, a
shortcut to Applications and `packaging/Install.txt`. Leave
`SWCHESS_SIGN_IDENTITY` unset and it signs ad-hoc, which is enough to test
the steps on your own Mac. Set `SWCHESS_MAKE_ZIP=1` for a zip beside the disk
image.

`notarize-macos.sh` sends the disk image to Apple, waits for the answer,
saves the log as `StarWarsChess-<version>-notary.json`, and staples the
ticket to the disk image. It stops without stapling when Apple reports
anything other than `Accepted`. Leave `SWCHESS_NOTARY_PROFILE` unset and it
prints the command that creates the profile, then exits without submitting.

`check-installed-app.sh` treats the download the way a player does. It mounts
the disk image, copies the app to a temporary folder, checks the signature,
checks that no binary reaches outside the bundle, checks every file in the
bundle against `packaging/bundle-allowlist.txt`, and plays 500 milliseconds
of a game from that copy with the source tree out of reach. The allow-list is
what keeps a file decoded from your CD out of a release.

### The one-time setup

Both steps need an Apple Developer Program membership, which costs US$99 a
year. Create a Developer ID Application certificate at
[developer.apple.com](https://developer.apple.com/account/resources/certificates/list)
and download it, then double-click it to put it in your keychain. Check that
it arrived and copy its full name.

```sh
security find-identity -v -p codesigning
```

Then store the notarization credentials once. Apple asks for an app-specific
password, which you make at [appleid.apple.com](https://appleid.apple.com)
under Sign-In and Security.

```sh
xcrun notarytool store-credentials 'swchess-notary' \
    --apple-id 'you@example.com' --team-id E85W63H34G \
    --password 'abcd-efgh-ijkl-mnop'
```

### What Gatekeeper does

| What you built | What a player's Mac does with it |
| --- | --- |
| An unsigned or ad-hoc signed download | Gatekeeper blocks the normal launch, because nothing identifies who published the app. |
| Signed with Developer ID, not notarized | The signature names you, but Gatekeeper still refuses the launch without a notarization ticket. |
| Signed and notarized | Gatekeeper checks the app and lets it open. The player may still confirm the first launch of a download. |
| Signed, notarized and stapled | The ticket travels inside the disk image, so the launch works even when the Mac cannot reach Apple. |

Do not tell players to right-click and choose Open. macOS Sequoia removed
that way around the check.

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

The macOS app bundle built by `scripts/build-app.sh` carries `swchess-viewer`
too, at `Contents/MacOS/swchess-viewer`, so review mode works from the bundle
without a separate build tree.

## Layout

| Path | Contents |
| --- | --- |
| `assets/` | The extracted artwork, sounds and manifests, written by the extractor |
| `cmake/` | Helper modules the top-level `CMakeLists.txt` includes |
| `docs/` | The plan and the research notes |
| `original/` | Where your copy of the CD files goes |
| `packaging/` | The `Info.plist`, the icon script and the release file lists |
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
