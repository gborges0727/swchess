Base the rewrite on the Windows 3.1 release, using C++20, SDL3, and CMake. Make the original DOS game playable through DOSBox-X as the first milestone.
Use emulation to preserve original behavior while the new source tree replaces the game’s implementation.

## 1. Base release

Windows filenames below refer to [original/win3x/cd](/Users/segrob/git/swchess/original/win3x/cd). DOS filenames refer to [original/dos/SWCHESS](/Users/segrob/git/swchess/original/dos/SWCHESS).
All hexadecimal offsets identify positions within files.

- Windows separates executable code, bitmap resources, capture sequences, sound, and configuration.
  All twelve piece DLLs have zero NE segments at `0x41c`, which confirms that they contain resources rather than executable code.
  `AT.DLL` identifies its character as an AT-AT at `0xd51`.
- Windows captures contain 5,423 timeline frames across 72 ANX files.
  Their tables reference 4,799 distinct frame records within those files.
  Every referenced record declares 8-bit pixels and 256 palette entries.
  Sample decoded images show recognizable Boba Fett and C-3PO artwork.
- Larger ANX files do not establish higher resolution than DOS.
  For example, `BBWB.ANX` declares a 112 × 171 first frame at `0x70c`.
  DOS `BF_C3001.IMG` has 48 palette-like bytes at `0x02`, suggesting sixteen RGB entries.
  Confirm the DOS format before claiming a color or resolution advantage.
- The presumed engine DLLs actually implement Windows controls.
  `CC.DLL` exports `INITCC` at `0x18e` and describes a custom-control library at `0x41d`.
  `CC16.DLL` and `CC256.DLL` have zero segments and contain control bitmaps.
- `CHESSAPP.EXE` contains the engine’s DDE commands, including `treecn` at `0xb63d` and `ePersonalitySet` at `0xb675`.
  The UI mentions “Kittinger's ply 1 list” at `XCHESS.EXE:0x38bd6`.
  This suggests Kittinger lineage, but does not establish an exact engine version or justify calling it The King.
- Preserve DOS as a runnable reference and an optional second asset source.
  Its monolithic executable makes selective replacement harder.
  Windows INI files document animation placement, sounds, and pauses directly.

## 2. Reverse-engineering strategy

### Reference execution

Create reproducible DOSBox-X configurations for the DOS release and for Windows 3.1 with the Windows release. Record CPU settings, display mode, audio configuration, game settings, and file hashes.
Use fixed CPU settings when measuring timing, then test CPU speed separately.
Exclude `original/`, extracted art, emulation disks, and decompiler projects from version control.

DOSBox-X supports Apple Silicon and Windows 3.1.
The Windows reference needs separately supplied Windows installation media and compatible display and sound drivers.
Install WinG only if runtime evidence requires it.
The game’s inspected import tables do not establish a WinG dependency. [DOSBox-X installation](https://github.com/joncampbell123/dosbox-x/blob/master/INSTALL.md), [Windows 3.1 guide](https://dosbox-x.com/wiki/Guide%3AInstalling-Windows-3.1x).

Build a DOSBox-X variant with `--enable-debug=heavy` for tracing.
Use `DEBUGBOX SWC.EXE`, instruction breakpoints, memory watchpoints, interrupt breakpoints, and memory dumps.
Trace DOS file operations through `INT 21h` and Windows calls through imported API addresses. [Debugger manual](https://github.com/joncampbell123/dosbox-x/blob/master/README.debugger).

Run commands over SSH. Use screen sharing or the owner’s visible Mac for interactive play and screenshot comparisons.
Do not depend on launching a window on this unattended desktop.
Use batch execution for automated checks, since DOSBox-X’s `-silent` option does not promise interactive graphics capture. [Command-line options](https://dosbox-x.com/wiki/DOSBox%E2%80%90X%E2%80%99s-Command%E2%80%90Line-Options).

Do not make the installed Wine executable a prerequisite. Current macOS Wine cannot directly run these Win16 NE programs.
WineVDM targets Windows and would require an additional Windows environment.
Use Windows 3.1 inside DOSBox-X as the primary Windows reference. [Wine macOS discussion](https://forum.winehq.org/viewtopic.php?t=38627), [WineVDM project](https://github.com/otya128/winevdm).

### Static analysis

Use Ghidra’s built-in `MzLoader` with `x86:LE:16:Real Mode` for DOS.
Use its `NeLoader` with `x86:LE:16:Protected Mode` for Windows.
Import relocation, segment, resource, and ordinal information before assigning function types.
Use headless analysis scripts where possible. [MZ loader](https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/Features/Base/src/main/java/ghidra/app/util/opinion/MzLoader.java), [NE loader](https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/Features/Base/src/main/java/ghidra/app/util/opinion/NeLoader.java), [x86 definitions](https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/Processors/x86/data/languages/x86.ldefs).

Treat PKLITE compression as unverified. `SWC.EXE` contains “PKWARE Data Compression Library” at `0x24da8`, which could describe resource compression.
Its MZ entry resolves to `0x2000`, where ordinary startup instructions appear.
If packer detection confirms PKLITE, unpack a working copy with Deark’s `pklite` module.
Use UNP inside DOSBox-X or a debugger memory dump if that fails.
Validate reconstructed relocations and original behavior before decompiling the result. [Deark recommendation](https://github.com/jsummers/pkla/blob/master/pkla/readme.md), [UNP author](https://bencastricum.nl/unp/).

Identify Borland runtime routines before analyzing game logic. Build a Ghidra Function ID database from lawfully available Borland C++ 3.x runtime objects with matching memory models.
A licensed IDA installation may provide historical Borland 3.1 FLIRT signatures.
Verify availability instead of assuming Ghidra includes them.
Repair near/far pointers, calling conventions, and segmented addresses manually. [Function ID documentation](https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/Features/FunctionID/src/main/doc/fid.xml), [IDA signature history](https://docs.hex-rays.com/release-notes/3_x).

Decompile these routines in order:

1. Resolve bitmap decoding and transparency through `CWDIB.DLL`, especially exported `TRANSCOPYDIBBITS` (ordinal 19) and `DIBTRANSCOPYDIBBITS` (ordinal 32).
2. Follow capture loading, frame selection, INI interpretation, and sound scheduling in `XCHESS.EXE`.
   Start timing references at its `frame_delay` string at `0x364cb`.
3. Recover board projection, piece placement, walking directions, animation transitions, and capture selection.
4. Document the commands and data exchanged between `XCHESS.EXE` and `CHESSAPP.EXE`.
   Recover position, move, clock, search-stop, and result semantics.
   Treat search internals as replaceable.

Skip installer recreation, Windows custom controls, Borland runtime implementation, and chess evaluation/search decompilation.
`SCHESS.EXE` identifies itself as a Windows setup application at `0x16ca`.
Decompiler output supplies evidence for a new implementation and will not automatically become portable C++.

### File formats and decoder checks

Document formats in this order. Record byte order, field sizes, bounds, naming, timing units, and unresolved interpretations.

| Format | Evidence and next work |
| --- | --- |
| ANX captures | All 72 files place the first bitmap at `0x70c`. A little-endian frame count precedes offsets relative to that position. `BBWB.ANX` declares 80 frames at `0x00` and reuses offset `0x1d60` at `0x08` and `0x0c`. Preserve repeats and backward references. Ignore unused table entries after the count. |
| Compressed bitmap data | `BBWB.ANX:0x71c` declares compression `0x00020001`. The candidate decoder uses the upper word as an escape byte. An escape introduces a value and one-byte repeat count. Other bytes represent literal pixels. Decode tightly packed `width × height` indices, then apply the BGR palette and vertical orientation. Confirm transparency and leftover padding through drawing routines. |
| NE bitmap resources | Parse NE resource tables directly without loading the DLLs. Apply their offset and length alignment shifts. The same candidate decoder expands all 1,344 bitmap resources across the twelve piece DLLs to their expected pixel counts. Extract title and board resources through the same resource reader. |
| INI animation scripts | Preserve section order, explicit frame lists, offsets, sound cues, pauses, and direction counts. `BB.INI:[BBWB_003]` names `irregulr.wav` and `pause=2`. Trace whether pause means ticks, elapsed time, or waiting for sound. Resolve count discrepancies such as `AT.INI:[W]`, which lists frame 016 but declares `count=15`. |
| Windows audio | `SWCAUDIO.DLL` contains 110 named `WAVE` resources. Its first RIFF header begins at `0x1400`. Respect RIFF lengths rather than resource alignment padding. The four standalone WAV files declare mono, 8-bit PCM at 22,050 Hz in their format fields at `0x14`. |
| DOS TOK scripts | Treat TOK as binary bytecode containing strings, not plain text. `BFC3.TOK` starts with `00 00`, followed by a filename at `0x02`. It names `Boba_MIDI` at `0x3d`. Recover opcodes, frame references, branching, waits, and sound commands from the interpreter. |
| DOS IMG packs | Start with `BF_R2001.IMG` and `BF_C3001.IMG`. Test the proposed frame counts, palette, bitmap geometry, compression, and dependence on prior frames. The first words are `0x002b` and `0x0035`, but their meanings remain hypotheses. |
| DOS VGH/GTL resources | `CWTEST.VGH` contains the resource-file header at `0x00`, `Title_Pal` at `0x10e3`, and `XMID` at `0x59932`. `SWCHESS.GTL` does not share that header. Its first six bytes are `00 00 02 06 00 00`. Determine its purpose through callers and compare it with `RESOURCE.LST` before declaring it another resource pack. |
| DOS DSF samples | `CLANK3.DSF` begins with repeated `2a d5` pairs instead of a RIFF header. Trace AIL sample setup to recover packing, sample rate, channels, signedness, and loop behavior. Resolve why TOK scripts request `.snd` names while the supplied files use `.DSF`. |

The candidate ANX decoder reaches the expected pixel count for all 4,799 distinct records.
Sample frames 1, 39, and 59 of `BBWB.ANX` render recognizable capture poses.
These checks establish a useful decoder candidate, but do not verify original timing or compositing.

Build a command-line extractor that renders PNG frames and contact sheets. Compare them visually with matching DOSBox-X captures.
Check palette colors, background removal, vertical orientation, foot placement, frame reuse, and sound timing.
Keep extracted images and recordings private. Use synthetic fixtures for public parser tests, including truncated input and invalid offsets.

## 3. Target architecture

Use C++20 for the runtime and Python for exploratory decoding and comparison tools. Implement validated production decoders in a shared C++ library used by the game and extractor.
Use SDL3’s 2D renderer with Metal on macOS.
SDL3 supplies windowing, input, audio streams, and high-DPI support without requiring a custom graphics engine. [SDL3 renderer](https://wiki.libsdl.org/SDL3/SDL_CreateRendererWithProperties), [high-DPI behavior](https://wiki.libsdl.org/SDL3/README-highdpi).

| Module | Responsibility |
| --- | --- |
| `src/assets` | Read the owner’s files without executing their DLLs. Normalize DOS filename separators and case. Produce indexed images, palettes, PCM samples, and animation descriptions. Cache conversions by source hash and decoder version outside `original/`. |
| `src/animation` | Schedule frame changes, movement, pauses, sounds, and completion against elapsed time. Support skip, cancellation, and replay without altering chess rules. |
| `src/game` | Own positions, legal moves, history, clocks, promotion, castling, en passant, and game results. Commit each move once and emit an animation event containing the before and after positions. |
| `src/engine` | Define position, search, stop, and result operations. Support human input, a deterministic test engine, and interchangeable UCI subprocesses. |
| `src/audio` | Decode and mix samples through SDL3 audio streams. Keep speech, effects, and music volumes separate. Schedule cues from the animation timeline. |
| `src/render` | Compose boards and sprites using the original coordinates. Handle scaling, clipping, draw order, interpolation of movement, and high-DPI presentation. |
| `src/ui` | Handle menus, move input, settings, save/load, engine selection, and status. Display engine failures without blocking the window. |
| `src/platform` | Handle macOS application packaging, writable directories, and subprocess lifecycle. Keep these details outside chess and asset code. |

Use the Windows WAV music and effects by default. For DOS music, decode XMIDI events before synthesis.
Preserve note durations, tempo, loops, and controller instructions. Use FluidSynth with an explicitly selected SoundFont for portable playback.
Compare instrument mappings with the original AIL driver before claiming musical fidelity. [Exult XMIDI implementation documentation](https://exult.sourceforge.io/docs.html).

A macOS-specific alternative can use AVAudioUnitSampler with a supplied sound bank.
CoreMIDI can send events to an external synthesizer, but does not itself synthesize sound.
An offline MIDI-to-PCM conversion can avoid a runtime synthesizer when interactive music behavior permits it. [Apple sampler documentation](https://developer.apple.com/documentation/avfaudio/avaudiounitsampler).

Start the native build with the asset viewer. Finish application packaging after gameplay and engine integration.
Pin dependency versions and provide CMake targets for the extractor, tests, and application.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build
ctest --test-dir build --output-on-failure
```

Bundle required libraries without Homebrew-specific install paths.
Verify a Mach-O arm64 application on macOS 26 that runs without Wine, DOSBox, or original executable code.
CMake supports SDL builds targeting arm64 explicitly. [SDL CMake documentation](https://github.com/libsdl-org/SDL/blob/main/docs/README-cmake.md).

## 4. Frame rate

Default to display-synchronized rendering at 60 Hz, with 120 Hz when the display supports it. Keep authored sprite poses and capture durations at their measured cadence.
Interpolate piece travel, board transforms, highlights, and UI movement between updates.

A 120 ms interval permits about 8.33 frame changes per second before additional work or pauses. Changing that delay to 16.67 ms would run the same sequence about 7.2 times faster.
It would not create missing poses.
Measure `FASTCPU` behavior and Windows pause handling before translating either setting into the new player.

Use a monotonic clock and elapsed-time animation scheduling. Do not count rendered frames to advance sprites or chess clocks.
Predecode or prefetch animations so file reads cannot stall presentation.
If rendering falls behind, advance the visual timeline while delivering each scheduled sound event once.

Offer optional smooth captures through an offline interpolation experiment.
RIFE can synthesize intermediate frames, but acceptance requires inspection of this artwork. [RIFE implementation](https://github.com/hzwer/ECCV2022-RIFE).
Align frames on a common canvas before interpolation. Preserve authored timestamps, cuts, pauses, and audio cues.
Check disappearing limbs, lightsabers, overlapping figures, and transparency explicitly.

Keep interpolation and AI upscaling disabled by default. Upscaling changes spatial detail and does not increase motion detail.
Generate optional assets locally from the owner’s copy and retain the original rendering mode.

Measure presentation intervals, missed refreshes, input response, and audio drift separately from sprite cadence.
Target stable 16.67 ms presentation at 60 Hz and 8.33 ms at 120 Hz.
Do not describe repeated sprite poses as newly animated frames.

## 5. Engine replacement and opening book

Preserve the original engine by retaining the complete emulated game.
A 16-bit DLL thunk cannot supply native macOS execution, and the identified CC DLLs are controls anyway.
Using the original engine inside the new UI would require a guest-side adapter around `CHESSAPP.EXE` and its DDE protocol.

Recompiling recovered engine logic would require substantial manual reconstruction and behavioral validation. Exclude that work from the default plan.
Offer Stockfish through UCI in the native game instead.

Run each selected engine as a separate process. Implement `uci`, `isready`, `ucinewgame`, `position`, `go`, `stop`, and `quit`.
Parse output asynchronously and validate every returned move against the application’s position.
Drain the stopped search’s `bestmove` before beginning another search.
Reject results belonging to an obsolete game or position.

Query supported options during startup. Map Newcomer, Novice, Moderate, Hard, and Expert to configurable strength profiles.
Use Skill Levels 0, 4, 8, 12, and 16 as initial calibration settings when the selected engine supports that range.
Alternatively, use `UCI_LimitStrength` with `UCI_Elo` to specify a target rating.
Do not enable both rating and skill controls as independent adjustments.
Calibrate profiles through games and keep thinking time configurable.
Test whether Stockfish’s minimum strength is low enough for the original beginner modes. [Stockfish strength controls](https://official-stockfish.github.io/docs/stockfish-wiki/Stockfish-FAQ.html#how-do-skill-level-and-uci_elo-work).

Treat the CMP files as historical inputs to calibration. Their 102-byte records encode more than a rating.
`EXPERT.CMP` begins with “Chessmaster” at `0x00`.
A new profile with the same menu label will not reproduce the original personality automatically.

Support an owner-supplied engine executable.
If distribution bundles Stockfish, include its GPLv3 notices and provide corresponding source under the license.
Process separation does not remove obligations for the distributed engine. [Stockfish license](https://github.com/official-stockfish/Stockfish).

Keep `BOOK.DAT` optional. DOS `BOOK.DAT`, Windows `BOOK.DAT`, and Windows `XBOOK.DAT` are byte-identical.
All three have SHA-256 beginning `1ed094516d6e935e`.
Their header names “Chessmaster 3000 Opening Book” at `0x00`.

Trace book lookup only if preserving original opening choices is required. Recover move encoding, position identification, branching, and weights.
Validate lookup results against recorded original games.
A native book adapter can select a legal move before asking UCI to search.
Do not assume the file uses Polyglot or that UCI engines accept it directly.

## 6. Milestones

Each milestone produces an executable tool, runnable game, or independently testable module.

| Milestone | Deliverable | Completion check |
| --- | --- | --- |
| 1. Original play | Produce a DOSBox-X configuration and command-line launcher for the DOS release. | Complete a game on Apple Silicon with working input, captures, sound, and persistence. Record reference screenshots and audio. |
| 2. Windows reference | Produce a repeatable Windows 3.1 installation procedure and runtime configuration. | Launch `XCHESS.EXE` with its engine process. Record `BBWB`, walking, pauses, and sound cues. Test whether the commented demo controls in `CM.INI` can exercise captures reproducibly. |
| 3. Native asset tools | Build the C++ extractor and SDL3 viewer from source. | Decode all ANX sequences and piece DLL resources. Match selected PNGs, including `BBWB.ANX` frames 1, 39, and 59, against the Windows reference. Play extracted WAV resources. |
| 4. Modern animation | Build a native board and capture viewer with independent presentation and animation clocks. | Sustain 60 Hz and supported 120 Hz presentation. Preserve capture duration, repeated frames, placement, and audio timing across refresh rates, pause, skip, and resize. |
| 5. Native chess | Build human-versus-human play with the independent rules module. | Pass trusted move-count tests and cases covering castling, en passant, promotion, checkmate, stalemate, and repetition. Confirm that interrupted captures never apply a move twice. |
| 6. Swappable engine | Build the UCI adapter and five configurable difficulty profiles. | Complete games with Stockfish and a second UCI implementation. Test stop, restart, malformed output, process failure, stale results, and promotion moves. |
| 7. Native application | Produce the complete CMake build and macOS application bundle. | Build from a fresh checkout and dependency installation. Run on macOS 26 using only the owner’s data files and the selected native engine. Verify saves, settings, audio, and library discovery. |
| 8. DOS asset compatibility | Build separate IMG, TOK, VGH/GTL, DSF, and XMIDI tools if DOS assets are included in scope. | Render `BF_R2001.IMG` frames matching DOSBox-X screenshots. Match one complete capture and music sequence, then validate every supported file. |
| 9. Optional enhanced art | Build a local interpolation and upscaling command if approved. | Compare complete captures at unchanged duration. Reject broken silhouettes, cuts, transparency, or sound synchronization. Keep original assets selectable. |

## 7. Legal note

“Abandonware” grants no reuse rights over Lucasfilm/Disney IP or Software Toolworks (later Mindscape) code. [Copyright basics](https://www.copyright.gov/what-is-copyright/)
The public repo must contain only independently written tools and runtime code that read the owner’s copy, with no original assets, binaries, or proprietary decompilation.

## 8. Open decisions

- Confirm whether preserving the original engine through the emulated game is sufficient, or whether the native UI must also use it.
  The latter adds a Windows guest adapter or engine reconstruction.
- Confirm whether Windows assets define the native game, or whether DOS appearance and music are required too.
  Recommend Windows assets with DOS emulation retained.
- Confirm whether smooth presentation with authored sprite cadence meets the frame-rate goal.
  Requiring newly synthesized capture poses adds the optional interpolation milestone.
- Identify the visible Mac or remote desktop used for acceptance checks.
  Confirm access to Windows 3.1 installation media for the Windows reference.
- Choose between supplying an external UCI engine and distributing Stockfish with the application.
  Choose the intended beginner strength and whether matching original opening choices requires book decoding.
- Confirm the required original features, including 2D/3D boards, languages, saved-game import, sound variants, and music synthesis.
  These choices determine which UI behavior and resource formats require full compatibility.