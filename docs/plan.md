Build a native macOS game in C++20 with SDL3 and CMake, using only the Windows release.
Provide human-versus-human chess, all four piece sets, all four languages, and interpolated captures.
Keep the Python decoders in the repo as reference implementations.
Default to enhanced captures and retain an original-cadence toggle.

## 1. Reverse-engineering order for XCHESS.EXE

Follow the reviewer’s order.
Decompile only `XCHESS.EXE`, starting with capture timing.
Read DLL resources as data without decompiling their code.

Use Ghidra’s NE loader with `x86:LE:16:Protected Mode`.
Apply NE relocations and identify imported functions before following their callers.
The offsets below identify positions within files, not Ghidra’s segmented addresses.

| Order | Ghidra starting points | Required result |
| --- | --- | --- |
| Capture player | Follow `frame_delay` at `0x364cb`, `_OFFSET` at `0x364dd`, `wav` at `0x364e9`, and `pause` at `0x364f8`. Inspect calls to `GetPrivateProfileInt/String`. The `timeGetTime` call at `0x1d0d5` imports MMSYSTEM.607. The `sndPlaySound` call at `0x1d0fe` imports MMSYSTEM.2. | Recover frame order, signed placement offsets, pause units, sound flags, blocking behavior, final-frame duration, and cancellation. Determine whether offsets describe placement, cropping, or another coordinate system. Follow demo controls such as `attack_loop` at `0x343ba` to recover capture enumeration. |
| Board geometry and piece placement | Follow `chesssets` at `0x34018`, `vanishpt_3D` at `0x34f89`, `tilt_3D` at `0x34f9b`, `turn_3D` at `0x34fa9`, and `size_3D` at `0x34fb7`. Trace configuration readers and callers of `StretchDIBits` (GDI.439), including the call at `0xd26a`. | Recover square polygons, square centers, piece anchors, scale, draw order, and mouse hit testing for `2DSET_`, `WHTBTM_`, `WHTTOP_`, and `FACING_`. Verify each orientation independently. |
| Walking | Follow `LoadWalk DLL: %s, INI: %s` at `0x36d71` and `count` at `0x36d91`. Trace `LoadLibrary`, resource-loading calls, INI readers, and `timeGetTime` callers. | Recover direction selection, frame numbering, movement increments, rotation, and arrival behavior. Resolve discrepancies such as `AT.INI:[W]`, which lists frame 016 but declares `count=15`. |
| Menus and languages | Follow language DLL names beginning at `0x33e3a`, `language` at `0x34050`, and `LoadString` (USER.176). Trace text rendering and the `LEGFONT` reference at `0x36fd5`. | Recover string IDs, menu actions, font selection, glyph substitutions, and formatting arguments. Implement every language without replacing untranslated text silently with English. |
| Saved games | Follow `STARWARS.CMG` at `0x33f1f`, `.CMG` at `0x35d1e`, `pThreadSave` at `0x38ddf`, and `pThreadSaveSize` at `0x38deb`. Trace file-dialog imports and `_lread`, `_lwrite`, and `_llseek` from those callers. | Document the CMG header, position/history records, settings, and termination markers. The supplied `STARWARS.CMG` is only 108 bytes and begins “Starwars Chess Game”. It cannot establish every saved-game variant by itself. |

The reviewer’s description of three pre-rendered boards needs correction.
`WHTBTM_P.BMP`, `WHTTOP_P.BMP`, and `FACING_P.BMP` are piece sheets.
Their images show twelve characters in two rows.
The board geometry therefore still requires investigation.

Skip original execution and original-game recordings in this scope.
Use the decoded artwork, Python outputs, and recovered code as references.
Record the native viewer for visual reviews.
Keep unresolved timing or save-format behavior explicitly marked until the static analysis establishes it.

## 2. Asset pipeline

Promote the reviewer’s `decode_record` and `load` functions from `anx_validate.py` into `tools/reference/anx.py`.
Retain the validator and replace hardcoded directories with command-line arguments.
Keep imports free of file-writing side effects.
Implement an independent C++ decoder and compare decoded pixels and metadata against Python, rather than comparing PNG compression bytes.

The verified ANX layout uses 450 offset slots after the frame count.
Bitmap data begins at `0x70c`.
`BBWB.ANX:0x71c` contains compression `0x00020001`.
The upper word selects the escape byte for `escape, value, count` runs.
Decode tightly packed rows to `width × height` pixels.
The corpus contains 4,799 distinct ANX records, 5,423 timeline entries, and 624 reused references.

Emit canonical per-frame PNGs and versioned JSON manifests.
Preserve original resource names and file offsets as identifiers.
Pack runtime atlases as a separate, reproducible step.

| Output | Organization and contents |
| --- | --- |
| `catalog.json` | Record source hashes, extractor version, resource IDs, original names, dimensions, palettes, and output hashes. Include unresolved references and explicit aliases. |
| `captures/<capture>/` | Write each distinct decoded record once as RGBA PNG. Write an ordered timeline referencing those images. Retain repeated references, source frame numbers, raw INI fields, resolved placement, durations, sound events, and cut markers. |
| `pieces/<piece>/` | Organize all 1,344 bitmaps from AT, BF, C3, CB, DV, EM, LO, LS, R2, SP, ST, and YO by original resource name. Separate walking and rotation sequences in manifests derived from their INIs. |
| `sets/<set>/` | Preserve the four complete piece sheets and extract their cells using `CM.INI:[chesssets]` plus the recovered slicing rules. Record piece/color mappings and anchors. Exclude separator lines and unused canvas regions from sprite cells. |
| `backgrounds/` | Preserve `2DBDBTOP.BMP`, `2DBDWTOP.BMP`, `SPACE256.BMP`, and `THRON256.BMP` as opaque 640 × 480 images. |
| `audio/` | Extract all 110 WAVE resource records from `SWCAUDIO.DLL`, respecting RIFF lengths. Include the four standalone WAV files. Record sample format and duration. |
| `locales/<language>.json` | Preserve resource IDs and original bytes from RESENG, RESFRN, RESGER, and RESSPN. Add decoded display text after recovering the relevant glyph mappings. Preserve intentional empty entries. |
| `ui/` | Extract title images, the original font artwork, and required control images. `TITLERES.DLL` stores the 640 × 253 `LEGFONT` bitmap at `0xe2c00`. |
| `runtime/` | Build bounded atlas pages for piece sprites and individual capture clips. Record rectangles, padding, and anchors. Load only the current capture and nearby frames into GPU memory. |

The other four BMPs are `WHTBTM_P.BMP` and `WHTTOP_P.BMP` at 414 × 286, `2DSET_P.BMP` at 229 × 93, and `FACING_P.BMP` at 640 × 480.
Treat those dimensions as sheet dimensions, not board dimensions.

Set alpha to zero for palette index 0 in capture and piece sprites.
Set alpha to 255 for other source indices.
All 4,799 ANX palettes begin with BGR bytes `73 63 1b`, which represent teal RGB `#1b6373`.
Use the index to identify transparency, not a search for similar RGB colors.
Keep backgrounds opaque even where their pixel index is zero.
Store straight-alpha PNGs and premultiply their colors when preparing textures for the matching SDL blend mode.

Resolve INI sound names against WAVE resource names without regard to case.
The 110 resource records have 109 distinct names because both `GRUNT1.WAV` records contain identical audio.
Record these cue exceptions explicitly:

- Map `WN.INI:[WNBR_019]` from `atftstep.awv` to `ATFTSTEP.WAV`.
- Map `WP.INI:[WPBB_002]` from `r2alarm\.wav` to `R2ALARM.WAV`.
- Report `BB.INI:[BBWQ_007]` as unresolved because `LEIA2.WAV` is absent.
  Leave that cue silent unless code evidence identifies a replacement.

Do not assume the language resources contain ordinary Windows text.
For example, `RESFRN.DLL:0xc0b` contains `tr2s`, and `RESGER.DLL` uses `K\NIG`.
Recover substitutions in the relevant drawing routines or render through the original glyph atlas.
Avoid global character replacements that would corrupt numbers or other strings.

Keep Python and C++ extraction available from the source tree.
Keep original files and generated artwork out of version control.
The native application reads the local asset cache without requiring Python during play.

## 3. Required capture interpolation milestone

Use a pinned source build of `rife-ncnn-vulkan` with its `rife-v4.6` model.
The v4 implementation supports arbitrary interpolation times through `-s`.
Build for arm64 with `CMAKE_OSX_ARCHITECTURES=arm64` and `USE_STATIC_MOLTENVK=ON`.
MoltenVK executes Vulkan work through Metal.
Prove the build with a two-image inference on the target Mac before processing the corpus. [RIFE build and models](https://github.com/nihui/rife-ncnn-vulkan/blob/master/README.md), [MoltenVK runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md).

Keep inference outside the game.
Record the tool revision, model hash, settings, and input hashes in each generated manifest.
Do not assume the old downloadable macOS executable contains native arm64 code.

### Geometry and transparency

1. Resolve capture offsets before interpolation.
   Place every frame on one padded canvas using the recovered coordinates.
   Use the union of placed frame bounds so changing crop sizes cannot create false movement.
2. Convert index-zero pixels to alpha before preparing model input.
   Remove the teal matte.
   Prepare RGB over black and a separate grayscale alpha image for each source frame.
3. Run RIFE on both image sequences at identical interpolation times.
   The stock executable loads three color channels and does not preserve PNG alpha. [RIFE image-loading code](https://github.com/nihui/rife-ncnn-vulkan/blob/master/src/main.cpp).
4. Recombine interpolated color and alpha into RGBA.
   Unpremultiply with a low-alpha cutoff when writing canonical PNGs.
   The color and mask passes estimate motion independently, so this method needs visual validation.
5. Inspect edges over black, white, checkerboard, and the actual capture backgrounds.
   Correct masks or rerun failing intervals.
   If separate masks remain unreliable, adapt RIFE to warp alpha using the same estimated motion as color.
   The milestone remains incomplete until the approved captures composite correctly.

### Frame times, holds, and cuts

Sample the resolved animation timeline at `t = n / 60` seconds.
For a transition from `t0` to `t1`, pass `s = (t - t0) / (t1 - t0)` to RIFE.
Copy source images directly when a sample coincides with an authored pose.

A 120 ms gap spans 7.2 display intervals at 60 Hz.
Expect roughly seven synthesized images per changing gap.
Inserting seven images uniformly between every pair would instead create 66.67 fps at the original duration.
Use timestamps to produce 60 fps without changing the capture’s duration.

For example, the sample 16.67 ms into a 120 ms transition uses this command:

```sh
rife-ncnn-vulkan -m models/rife-v4.6 -0 a.png -1 b.png -s 0.138888889 -o sample.png
```

Resolve `pause=` semantics before assigning final timestamps.
Represent genuine holds as repeated presentation of one pose.
If the original waits for sound completion, preserve that condition instead of inventing motion during the wait.
Reuse decoded images, but never delete timeline entries or their sound events.
Repeated images with different placement may still describe movement.

Mark hard cuts explicitly in the capture manifest.
Hold the previous shot until the cut timestamp, then switch to the next shot.
Never run interpolation across that boundary.
Use image differences to suggest cuts for human review.
A changed frame size or a newly appearing object does not establish a cut by itself.

### Acceptance check

Provide a native `--review-capture BBWB` mode with synchronized original and enhanced views.
Include frame stepping, slow playback, background selection, and visible timestamps.
Review `BKWB` for lightning and disintegration, and `WBBR` for a different attacker/defender combination.

A person must verify unchanged composition, stable feet, intact weapons, correct disappearing objects, clean transparency, and unchanged sound placement.
The enhanced view must show intermediate movement during ordinary transitions.
Holds and hard cuts must remain discrete.
Compare every complete capture at normal speed after the representative examples pass.
Record approval or a specific failed interval for all 72 captures.

Automated checks must verify dimensions, timestamp order, unchanged total duration, retained sound events, and exact copies wherever an authored image is sampled.
Store a shorter duration for the final generated image when necessary to match the exact manifest duration.
Do not derive duration by dividing an integer image count by 60.

## 4. Rendering and timing design

Use SDL3’s Metal renderer and a logical coordinate system derived from the original artwork.
Handle display scaling separately from square geometry.
Draw backgrounds, board elements, stationary pieces, walkers, and capture overlays in the recovered order.

Maintain separate presentation and animation clocks.
Presentation follows the display refresh rate.
Animation uses a monotonic clock and explicit timeline timestamps.
Neither chess state nor sound scheduling depends on the number of rendered frames.

Enhanced mode selects the generated 60 fps image appropriate to the current animation time.
A 120 Hz display can repeat those images while updating input, highlights, and other movement at 120 Hz.
It does not turn the capture into 120 distinct poses per second.

Original mode selects the authored image using the 120 ms base cadence and recovered pauses.
Both modes share placement, sound events, cut times, and total duration.
Changing the toggle preserves the current animation time and does not replay cues.
Skipping a capture stops its scheduled effects and completes its presentation exactly once.

Decode and upload ahead of playback.
Do not perform RIFE inference, file reads, or atlas packing in the presentation loop.
Schedule audio against the animation clock through SDL3 audio streams.

Provide human-versus-human play with an independent rules module.
The module owns legal moves, turn order, castling, en passant, promotion, checkmate, and draws.
Commit each legal move once before creating its presentation event.

Define a small `MoveProvider` interface that requests a move from an immutable position and returns a move with a request ID.
Implement only the human provider.
Keep cancellation and stale-result rejection in that interface so another provider can be added later.
Do not expose DDE memory structures in the native game.

## 5. Milestones and checks

| Milestone | Independently runnable result | Completion check |
| --- | --- | --- |
| 1. Native capture viewer | Build an arm64 SDL3 application through CMake. Display `BBWB.ANX` over `SPACE256.BMP` with frame stepping and provisional 120 ms playback. | Launch on a visible Mac running macOS 26. Verify transparent compositing and compare frames 1, 39, and 59 with Python output. No original executable runs. |
| 2. Complete asset catalog | Build the Python extractor, independent C++ extractor, and native catalog browser. Include all sets, backgrounds, languages, sprites, captures, fonts, and audio records. | Compare every decoded pixel buffer and manifest field against Python. Account for 1,344 piece bitmaps, 72 captures, eight BMPs, four language DLLs, and 110 WAVE records. Verify the two cue aliases and report the missing cue. |
| 3. Capture semantics | Build the native capture player from recovered `XCHESS.EXE` behavior. Produce positioned canvases and resolved timelines. | Exercise all captures with offsets, pauses, cues, frame reuse, cancellation, and final-frame handling. Eliminate unresolved timing assumptions before generating enhanced assets. |
| 4. Interpolated captures | Build the native arm64 RIFE tool, offline processing commands, generated assets, and comparison viewer. | Pass the human review and automated checks above for all 72 captures. Preserve the original-cadence toggle. |
| 5. All sets and playable chess | Build human-versus-human play with all four board configurations and walking behavior. | Verify placement and hit testing on all 64 squares in every set. Test legal moves, special moves, checkmate, draws, capture cancellation, and arrival positions. |
| 6. Languages, saves, and application | Finish menus in English, French, German, and Spanish. Add versioned native saves and a documented CMG importer. Produce the macOS application bundle. | Check glyphs and menus across all 16 set/language combinations. Round-trip native saves and import the supplied CMG fixture. Build from a fresh checkout and run without development tools or original executable code. |

## 6. Remaining open decisions

No design decisions block the first two milestones.
They use the supplied Windows files, the reviewed decoder, and the fixed C++20/SDL3/CMake stack.
Visual acceptance requires the owner’s visible macOS 26 machine.
Original recordings, complete save-format recovery, and final interpolation quality do not block those two milestones.