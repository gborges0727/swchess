# The capture animation player in XCHESS.EXE

This describes the code in `original/win3x/cd/XCHESS.EXE` that plays a capture animation
when one piece takes another. Addresses come two ways. `1058:0a0a` is Ghidra's segmented
address, where segment number `n` maps to `(0x1000 + (n-1)*8):0000`. A file offset like
`0x1d0aa` is a byte position inside `XCHESS.EXE`. The Ghidra project, the dump scripts,
and the segment-to-file-offset table are in `.cache/ghidra/`.

## Where the code lives

| What it does | Address | File offset |
| --- | --- | --- |
| Builds the INI path and drives one capture | `FUN_1008_56c4` @ `1008:56c4` | `0xa184` |
| Reads the ANX and the INI into arrays | `FUN_1058_0e15` @ `1058:0e15` | `0x1d4b5` |
| Runs the frame loop | `FUN_1058_0a0a` @ `1058:0a0a` | `0x1d0aa` |
| Frees the old frame, unions the rects | `FUN_1058_08b4` @ `1058:08b4` | `0x1cf54` |
| Repaints one rectangle | `FUN_1058_03f7` @ `1058:03f7` | `0x1ca97` |
| Copies a block of pixels, opaque or keyed | `FUN_1058_0150` @ `1058:0150` | `0x1c7f0` |
| Computes a pixel address in a DIB | `FUN_1058_00b2` @ `1058:00b2` | `0x1c752` |
| Sends the composite to the screen | `FUN_1058_02fe` @ `1058:02fe` | `0x1c99e` |
| Loads the backdrop bitmap | `FUN_1058_0ce6` @ `1058:0ce6` | `0x1d386` |
| Finds a WAVE resource by name | `FUN_1058_0c4d` @ `1058:0c4d` | `0x1d2ed` |
| Expands one RLE frame into a padded DIB | `FUN_1010_1928` @ `1010:1928` | `0xdec8` |
| Transparent byte copy | `1050:0000` | `0x1c500` |
| Opaque byte copy | `1050:00f0` | `0x1c5f0` |
| Pumps pending Windows messages | `FUN_1008_3dd0` @ `1008:3dd0` | `0x8890` |
| Plays a sound by name | `FUN_1008_1519` @ `1008:1519` | `0x5fd9` |
| Picks the next capture for the cheat demo | `FUN_1008_588a` @ `1008:588a` | `0xa34a` |
| Sets the capture code after a real move | `FUN_1008_183f` @ `1008:183f` | `0x62ff` |
| Sets the capture code for checkmate | `FUN_1008_1745` @ `1008:1745` | `0x6205` |
| Plays the white king's extra line | `FUN_1008_1694` @ `1008:1694` | `0x6154` |
| Draws one walk frame and repeats the sound | `FUN_1068_0fe6` @ `1068:0fe6` | `0x1f5e6` |
| Draws one turn frame and repeats the sound | `FUN_1068_1140` @ `1068:1140` | `0x1f740` |
| The twelve move sound names | `DS:0x58` | `0x33c18` |

`XCHESS.EXE` imports eleven modules, including `CWDib`, but the capture player never
calls into it. The transparent blit is the routine at `1050:0000` inside `XCHESS.EXE`
itself.

## 1. How a capture is selected

The game writes a four-character code into a fixed buffer at `DS:0x12b` and then calls
`FUN_1008_56c4`. The four characters are, in order: the attacker's colour, the
attacker's piece, the defender's colour, the defender's piece.

`FUN_1008_183f` fills the buffer for an ordinary capture at `1008:1922`. It copies
`DAT_11d8_3d1d` into `DS:0x12b` and `DAT_11d8_3d1e` into `DS:0x12c` for the attacker,
then `DAT_11d8_3d1b` and `DAT_11d8_3d1c` for the defender, and writes a NUL into
`DS:0x12f`. If the defender's colour byte reads `'-'`, it clears `DS:0x12b`, which means
the move was not a capture.

`FUN_1008_1745` fills the same buffer for the checkmate animation. The defender's piece
is hardcoded to `'K'`, and the colours are `'W'` then `'B'` for a white win or `'B'`
then `'W'` for a black win. The attacker's letter comes from a switch at `1008:17d6`
that maps 0 to `'K'`, 1 to `'Q'`, 2 to `'R'`, 3 to `'B'`, 4 to `'N'`, and 5 to `'P'`.

`FUN_1008_56c4` turns that code into two names at `1008:56dd`. It copies the code to two
stack buffers, writes a zero over the third character of the first buffer at
`1008:5700`, and appends `".INI"` (the string at `DS:0x7a0`). So `"WNBK"` becomes the
file `WN.INI` and the section `[WNBK]`. The file name uses the attacker only, and the
section name uses all four characters. The full path is the data directory from
`DAT_11d8_67e5:67e7`, a backslash, and `WN.INI`.

`WN.INI` holds every capture with a white knight attacking, and `BN.INI` every capture
with a black knight attacking, so the two colours read different files. The `.ANX` file
is named after the section, so `[WNBK]` in `WN.INI` pairs with `WNBK.ANX` in the same
directory. All 72 combinations exist, twelve attackers with six defenders each.

The animation only plays when `DAT_11d8_6887` is non-zero. `FUN_1008_183f` clears
`DS:0x12b` when that flag is zero, which suppresses the capture. `FUN_1060_00fe` at
`1060:0142` shows the same flag driving the check mark on menu item 72, and `CM.INI`
stores the persisted setting under `[look_feel] captures`. A second flag,
`DAT_11d8_6889`, decides whether sound plays, and it is menu item 71.

## 2. What the loader reads

`FUN_1058_0e15` takes two far pointers. The first, at `[BP+6]`, is the path of the
capture INI. The second, at `[BP+0xa]`, is the section name such as `"WNBK"`.

It reads seven things through the profile API. The calls below are listed in the order
the code makes them, with the real Windows argument order.

- `GetPrivateProfileInt("demo", "pause", 0, CM.INI)` at `1058:0e37`, stored in
  `DAT_11d8_286a`. `CM.INI` is the file whose path sits in `DAT_11d8_67e1:67e3`.
- `GetPrivateProfileInt("defaults", "frame_delay", 180, CM.INI)` at `1058:114b`, stored
  in `DAT_11d8_2866`. The built-in default is 180 milliseconds. The shipped `CM.INI`
  overrides it with 120.
- `GetPrivateProfileString("WNBK", NULL, "", buffer, 4048, WN.INI)` at `1058:1170`.
  Passing NULL for the key returns every key name in the section as a run of
  NUL-terminated strings. That list, in file order, is the timeline.
- `GetPrivateProfileInt("WNBK_OFFSET", "x", 215, WN.INI)` at `1058:11bf`. When the key
  is absent the value is 215.
- `GetPrivateProfileInt("WNBK_OFFSET", "y", 100, WN.INI)` at `1058:11d9`. When the key
  is absent the value is 100.
- `GetPrivateProfileString("WNBK_OFFSET", "wav", "", DS:0x7c08, 80, WN.INI)` at
  `1058:1202`, skipped entirely when sound is off.
- `GetPrivateProfileInt("WNBK_OFFSET", "hold", 1000, WN.INI)` at `1058:1221`, stored in
  `DAT_11d8_2868`. The built-in default is 1000 milliseconds.

Then, for each entry in the timeline, it reads two more values using the entry's own key
name as a section name:

- `GetPrivateProfileString("WNBK_003", "wav", "", buffer, 80, WN.INI)` at `1058:1279`.
- `GetPrivateProfileInt("WNBK_003", "pause", 0, WN.INI)` at `1058:12ab`, but only when
  the `wav` string came back non-empty. The branch at `1058:1283` jumps straight to
  zeroing both arrays when there is no `wav`. A frame that declares `pause` without a
  `wav` is ignored. Two frames in the shipped data do that, `[WBBR_001]` and
  `[WKBK_003]`, and both are silent no-ops.

The per-frame `x` and `y` keys are never read. The strings `"x"` at `DS:0x2925` and
`"y"` at `DS:0x2927` each appear exactly once in the whole executable, in the two
`_OFFSET` calls above. At run time the per-frame placement comes out of the `.ANX` file,
described in the next section.

The runtime key list is therefore short. The section-wide `[XXXX_OFFSET]` accepts `x`
(default 215), `y` (default 100), `wav` (default empty), and `hold` (default 1000
milliseconds). Each per-frame section accepts `wav` (default empty) and `pause` (default
0, honoured only alongside a `wav`). Everything else in the INI, including `art=` and
the per-frame `x=`/`y=`, is authoring data that the packer consumed.

## 3. Where the frame positions come from

The `.ANX` header is a 32-bit frame count at offset 0 followed by three tables of 150
32-bit entries each. `FUN_1058_0e15` allocates three 600-byte arrays starting at
`1058:0e45`, then fills them with three identical 150-iteration loops that walk the file
in order. Bitmap data starts at `0x70c`, which is `4 + 3 * 150 * 4`.

- Table 1, at file offset `0x004`, holds each record's byte offset relative to `0x70c`.
  At `1058:12d0` the loader adds it to the base pointer and stores the far pointer for
  frame `i`.
- Table 2, at file offset `0x25c`, holds the frame position as two signed 16-bit values,
  x then y. The loader adds the `[XXXX_OFFSET]` x to the first and the `[XXXX_OFFSET]` y
  to the second, then stores both in the frame record.
- Table 3, at file offset `0x4b4`, holds each record's compressed byte length. The
  loader copies it into an array and never reads that array again. It frees the array at
  the end of the function. Only the packer needed it.

Table 2 matches the per-frame `x=` and `y=` keys exactly in 48 of the 72 captures. In
the other 24 some frames differ, so the shipped INIs and the shipped ANX files were
built from different revisions of the artwork data. A port must take x and y from table
2, because that is what the original reads.

The x and y are the top-left corner of the frame bitmap on the canvas, in pixels, with y
increasing downward. `FUN_1058_0150` builds the inclusive rectangle `(x, y, x + width -
1, y + height - 1)` at `1058:01a9` and intersects it with `(0, 0, canvasWidth - 1,
canvasHeight - 1)`. `FUN_1058_00b2` converts a top-down y to the bottom-up DIB row at
`1058:010e` with `(biHeight - y - 1) * stride`, where `stride` is `(biWidth + 3) & ~3`.

The offsets are signed, and negative values are common. Twelve captures set a negative
`[XXXX_OFFSET] y`, from `-5` in `BNWB` down to `-50` in `BRWB`, and `WQBR` sets `x=-50`.
Twenty of the 72 captures place at least one frame partly outside the 640 by 480 canvas.
`WRBK` reaches x 687 and `WNBR` reaches y
445. The code clips instead of failing.

Each loaded frame is a six-word record, and the 150 records start at `DS:0x7c58` with a
12-byte stride. The words are the bitmap pointer offset, the pointer segment, x, y,
width, and height, with width and height copied out of the `BITMAPINFOHEADER` at `+4`
and `+8`.

## 4. How a timeline entry maps to an ANX record

The timeline index and the record index are the same number. The loop counter in
`FUN_1058_0e15` starts at zero, advances one entry per key name in the section list, and
indexes table 1 directly with `SI * 4`. The number in the key name, the `005` in
`BBWB_005`, is never parsed. It only names the per-frame INI section that holds `wav`
and `pause`.

`[BPWP]` lists 131 keys but only 91 distinct numbers, because 40 entries repeat an
earlier pose and reuse its per-frame section. `BPWP.ANX` still holds 131 table-1 slots,
and duplicate poses show up as repeated offsets in that table. The packer also
deduplicates across different pose numbers when the pixels happen to be identical, so
the repeats in table 1 and the repeats in the key list do not have to line up.

The loader stops when it runs out of key names, and before each entry it checks the
counter against the 32-bit frame count at `1058:1231`, failing if the timeline is longer
than the ANX. `WNBR.ANX` declares 80 records while `[WNBR]` lists 71 keys, so nine
records go unused. The arrays hold 150 entries, and the longest shipped capture, `WPBR`,
uses 143. On failure `FUN_1008_56c4` shows the string `"LOW MEMORY"` at `DS:0x7a7` and
plays nothing.

## 5. Timing

`FUN_1058_0a0a` runs one iteration per timeline entry. One iteration, disassembled from
`1058:0a35`, does this:

1. Read `timeGetTime` and keep it as the iteration's start time `t0`.
2. If this frame has a sound, handle it as described below.
3. Pump messages once through `FUN_1008_3dd0`.
4. Stop if the frame pointer is NULL.
5. Expand the frame with `FUN_1010_1928` and store the result back.
6. If the frame index is not zero, call `FUN_1058_08b4` to free the previous frame and
   draw this one, then spin on `timeGetTime` until it reaches `t0 + frame_delay`.
7. If `[demo] pause` is set, print `"pause frame %d"` and spin until the right mouse
   button goes down.

The guard at `1058:0add` skips both the draw and the wait for index zero. The first
authored pose is decompressed, its sound is started, and then it is freed at the next
iteration without ever reaching the screen, so the original shows one fewer pose than
the timeline lists. That pose's rectangle still joins the first repaint region, because
`FUN_1058_08b4` unions the previous frame's rectangle with the current one.

The frame delay is measured from the top of the iteration, so decompression and drawing
time come out of the wait rather than adding to it. A frame that takes longer than
`frame_delay` to decode makes the wait fall through immediately.

`pause` selects one of three sound behaviours, at `1058:0a44` through `1058:0a9d`. It
counts neither frame delays nor timer ticks.

- No `pause`, or `pause=0`. The frame's sound plays with `sndPlaySound` flags 5, which
  is `SND_MEMORY | SND_ASYNC`. Nothing waits.
- `pause=1`. The sound plays with flags 4, which is `SND_MEMORY | SND_SYNC`. The call
  blocks until the sound finishes, and the animation stops for that long. Windows does
  not pump messages during that call.
- `pause=2`. The sound plays asynchronously, and the value is remembered. The next frame
  that carries a sound spins at `1058:0a58` calling `sndPlaySound(NULL, SND_NOSTOP |
  SND_NODEFAULT)` and pumping messages until that call returns non-zero, which happens
  once the earlier sound has finished.

Across all 72 captures only 37 frames use `pause=1` and 11 use `pause=2`.

The last frame gets extra time. After the loop ends at `1058:0b63`, the player reads
`timeGetTime` and spins until `t0 + hold`, with the final frame still on screen for that
whole wait. Eight captures override the 1000 millisecond default, from 100 in `WKBK` and
`WQBR` up to 2000 in `WNBK`.

Then, at `1058:0b98`, if `[XXXX_OFFSET] wav` was set, the player calls `FUN_1008_1519`
with that name and flags 0. Inside, at `1008:1519`, the name is tried first as a file on
disk and then as a `WAVE` resource played with `flags | 4`, which is `SND_MEMORY |
SND_SYNC`, so the call blocks. Only two captures use this: `BKWR` plays `emplgh.wav` and
`BQWN` plays `breath.wav`. Both play after the hold, with the last pose still visible.

Only then does the player erase the last frame at `1058:0bc4`, calling `FUN_1058_08b4`
with a zeroed terminator record, then `sndPlaySound(NULL, 0)` to silence everything, and
finally freeing the sound resources and the ANX image.

The nominal length of a capture is therefore `(frameCount - 1) * frame_delay + hold`,
plus whatever the blocking sounds add. `BBWB` with 80 frames, a 120 millisecond delay,
and the default hold comes to 10,480 milliseconds. `WNBK` with 45 frames and `hold=2000`
comes to 7,280.

## 6. Sound lookup

`FUN_1058_0c4d` resolves a per-frame `wav` name. It calls `AnsiUpper` on the name, then
`FindResource(hSoundLib, name, "WAVE")`, then `LoadResource` and `LockResource`. It
stores the resource handle in the array at `DS:0x7884` and the locked far pointer in the
array at `DS:0x79b0`. There is no file fallback and no extension repair on this path.

The sound library is `SWCAUDIO.DLL`. The player reuses the instance in `DAT_11d8_009b`
when it is non-zero, otherwise it loads the DLL at `1058:0f13` and frees it when the
capture ends.

`SWCAUDIO.DLL` holds 110 `WAVE` resources. Checking their names against the three cue
exceptions listed in `docs/plan.md` section 2 settles all three.

- `WN.INI:[WNBR_019]` asks for `atftstep.awv`. The uppercased name `ATFTSTEP.AWV` is not
  a resource. `ATFTSTEP.WAV` is. The original plays nothing on that frame.
- `WP.INI:[WPBB_002]` asks for `r2alarm\.wav`. `R2ALARM\.WAV` is not a resource.
  `R2ALARM.WAV` is. The original plays nothing.
- `BB.INI:[BBWQ_007]` asks for `LEIA2.WAV`, which is absent. The original plays nothing.

All three are silent in the shipped game. Substituting the near-miss names would add
sounds the original never made.

The name in the INI is the whole lookup. There is no ordinal table, no per-piece table
and no string table inside a `RES*.DLL`. Three captures show it. `BB.INI:[BBWB_003]`
writes `wav=irregulr.wav` and the resource is `IRREGULR.WAV`. `WP.INI:[WPBP_008]` writes
`wav=ricochet.wav` and the resource is `RICOCHET.WAV`. `BK.INI:[BKWQ_042]` writes
`wav=leia1.wav` and the resource is `LEIA1.WAV`. Only `SWCAUDIO.DLL` holds resources of
type `WAVE`. Every piece DLL holds bitmaps and the four `RES*.DLL` hold string tables.

Only one capture sound is audible at a time. `FUN_1058_0a0a` calls `sndPlaySound` at
`1058:0a9d` with flags 4 for a `pause=1` cue and flags 5 for every other cue. Flag 0x10
is `SND_NOSTOP`. Neither value carries it, so each call stops the sound still playing.
`BBWB` sounds `CLANK3.WAV` on poses 39, 44, 47 and 48, and that clip runs 2593
milliseconds. Those poses stand 120 to 600 milliseconds apart, so the original cuts three
of the four short and never lets two sound together.

The player also silences the move sound before its first frame. `FUN_1058_0a0a` opens at
`1058:0a19` with `FUN_1008_1519(0, 0, 0)`, which reaches `sndPlaySound(NULL,
SND_NODEFAULT)` at `1008:1591` and stops whatever the walk left playing.

While loading, at `1058:1249`, the player calls `FUN_1008_1519` once per timeline entry
with the sentinel string `"!repeat!"` at `DS:0xcd`, which restarts the sound already in
memory so the move sound keeps playing while the ANX loads.

## 7. Drawing

`FUN_1058_0ce6` loads the backdrop before the first frame. The path is built by
`wsprintf` at `1058:1123` from the data directory plus `"\SPACE256.BMP"` when
`DAT_11d8_6883` is non-zero, or `"\THRON256.BMP"` when it is zero. Both are 640 by 480
8-bit bitmaps.

`FUN_1058_0ce6` creates four objects and keeps them for the whole capture: the loaded
backdrop DIB in `DAT_11d8_8370:8372`, a logical palette in `DAT_11d8_8366`, a working
copy of the backdrop in `DAT_11d8_836c:836e`, and a memory device context created with
`CreateDC("DIB", ...)` in `DAT_11d8_8368`. It also builds a 552-byte `BITMAPINFO` at
`DAT_11d8_8362:8364` whose colour table is 256 words holding 0 through 255, which is the
identity index table for `DIB_PAL_COLORS`.

`FUN_1058_03f7` repaints one rectangle in three steps at `1058:0436`, `1058:0455`, and
`1058:0463`:

1. `FUN_1058_0150(backdrop, 0, 0, rect, 0, 0)` copies the untouched backdrop over the
   rectangle. The last argument, 0, selects the opaque copy at `1050:00f0`, which is a
   `rep movsd` loop.
2. `FUN_1058_02be` calls `FUN_1058_0150(frame, frame.x, frame.y, rect, 0, 1)`. The
   pushes at `1058:02d1` show the transparent index is the literal 0 and the mode is 1,
   which selects the transparent copy at `1050:0000`. That routine compares each source
   byte with the key in `AH` and skips the matching ones. Palette index 0 is the
   transparency key, hardcoded, not derived from the palette contents.
3. `FUN_1058_02fe` blits the rectangle to the screen with `StretchDIBits`, using
   `SRCCOPY` and `DIB_PAL_COLORS`, after `SelectPalette` and `RealizePalette`. The
   source and destination widths are the same variable, and so are the heights, so there
   is no scaling. The source y is `dibHeight - rect.bottom`, which is the bottom-up
   conversion of the top-down rectangle.

Both blit routines in segment 11 use DPMI `INT 31h` functions 0x000B and 0x000C to set
the big bit on a descriptor, then address pixels with 32-bit registers, which lets one
loop span a bitmap larger than 64 KB.

The capture canvas is the full 640 by 480 backdrop, and the compositing happens in
memory. Only the union of the outgoing and incoming frame rectangles reaches the screen
on each step.

## 8. Cancellation

Nothing cancels a capture. `FUN_1058_0a0a` contains exactly one input check, the
`GetAsyncKeyState(VK_RBUTTON)` at `1058:0b4e`, and it sits inside the demo single-step
branch. The normal path has no key test, no abort flag, and no early exit other than a
NULL frame pointer.

`FUN_1008_3dd0` does pump messages between frames, so the window keeps responding.
`FUN_1008_56c4` sets `DAT_11d8_6787` to 1 at `1008:5785` before calling the player and
clears it at `1008:57ae`. The window procedure reads that flag at `1008:476b`, inside
the region Ghidra left undisassembled. The bytes at file offset `0x9217` decode to this.
When two other flags are clear, the handler calls `1018:26fd`, which blits the board
background. Only when `DAT_11d8_6787` is zero does it go on to call `1020:0948` and
`1008:124c`, which redraw the pieces and the overlay. The flag stops the board redraw
from painting over the capture, and it never stops the capture itself.

`DAT_11d8_6785`, cleared at `1008:57bb`, would post `WM_COMMAND` 3020 after the capture
if anything set it. Nothing in the executable does, so that path is dead.

## 9. The `[demo]` cheat

`FUN_1008_588a` picks the next capture. The colour, attacker, and defender live in
`DAT_11d8_01f6`, `DAT_11d8_01f8`, and `DAT_11d8_01fa`. `DAT_11d8_01fc` marks the first
call.

On the first call it reads three keys from `[demo]` in `CM.INI`, each defaulting to 0:
`color`, `attack`, `defend`. It then clears the first-call marker.

On every later call it advances the counters at `1008:5896`. The defender increments. At
6 it wraps to 0 and the attacker increments. At 6 the attacker wraps to 0 and the colour
flips between 0 and 1. That enumerates all 72 captures with the defender as the
innermost loop.

It then reads three more keys from `[demo]`, each using the freshly advanced counter as
its default: `color_loop`, `attack_loop`, and `defend_loop`. Whatever those keys hold
overrides the counter. Setting `attack_loop=2` keeps the attacker a rook while the
defender still cycles. `defend_loop` exists in the code at `1008:595e` but is not
mentioned in the comments in `CM.INI`.

Piece numbers are 0 for K, 1 for Q, 2 for R, 3 for B, 4 for N, and 5 for P, read from
the table `"KQRBNP"` at `DS:0x1fd`, file offset `0x33dbd`. Colour 0 is W and colour 1 is
B, from `"WB"` at `DS:0x204`, file offset `0x33dc4`.

A colour of 2 does not name a colour. At `1008:58f6` the code opens a dialog with the
prompt `"Enter a capture string:"` at `DS:0x817` and writes the typed text straight into
the capture-code buffer at `DS:0x12b`, so a person can type any four-character code by
hand. `CM.INI` does not document that.

Otherwise it formats the code with `wsprintf(DS:0x12b, "%c%c%c%c", ...)` at `1008:5966`,
using the attacker's colour, the attacker's piece, the opposite colour, and the
defender's piece, then sends `WM_COMMAND` with command id 123 to the main window, which
starts the capture.

Two more `[demo]` keys act on the player itself. `pause`, read at `1058:0e37`, makes
`FUN_1058_0a0a` stop after every frame, print `"pause frame %d"`, and wait for a right
mouse click, which turns the capture into a manual frame stepper. `loop_delay` is read
at `1008:5809` with a default of `0xffff`, that is `-1`, and stored in `DAT_11d8_00dc`
after the capture ends. `CM.INI` describes it as the delay before the demo repeats. I
did not trace the timer that consumes it, so its units are unresolved.

A harness that wants all 72 captures does not need the demo. It can call `FUN_1058_0e15`
and `FUN_1058_0a0a` with each of the 72 section names, or write `attack`, `defend`, and
`color` into `[demo]` and let the enumeration run.

## 10. What the walk animation shares

The walking code lives in segment 14 and starts at `FUN_1068_01d5`, with the `"LoadWalk
DLL: %s, INI: %s"` trace at `1068:0288`. It shares no drawing code with the capture
player, because every internal relocation in segment 14 points somewhere other than
segment 12, where the capture compositor lives.

Walkers go through the sprite layer in segment 4 instead, whose errors run from `"Too many sprites in AddSprite"` at `DS:0xfda`
to `"Inactive sprite in ShowSprite!"` at `DS:0x11bc`, so they composite over the live
board rather than a full-screen backdrop.

The two players share only general helpers:
`FUN_1008_1519` to play a sound by name, `FUN_1008_3dd0` to pump messages,
`FUN_1008_3d02` to load a DLL, `FUN_1008_0236` to test for a file, and the same
busy-wait against `timeGetTime`.

Walk frames come from a per-piece DLL, not an ANX, and
the INI keys are `count` plus names built from `"%s_%s%03d"` and `"%s_R%03d"` across
eight compass directions.

## 11. The sounds outside a capture

Every sound the game plays outside the capture player goes through `FUN_1008_1519` at
`1008:1519`. That routine holds one loaded `WAVE` resource at a time. It reads the sound
switch in `DAT_11d8_6889` first and returns without playing anything when the switch is
off. Then it takes one of two paths.

- The name `"!repeat!"`, the string at `DS:0xcd`, restarts the resource already in
  memory. The code calls `sndPlaySound(NULL, SND_NODEFAULT | SND_NOSTOP)` at
  `1008:1562`, which answers non-zero only when nothing is playing, and then calls
  `sndPlaySound(pointer, SND_ASYNC | SND_NODEFAULT | SND_MEMORY)` at `1008:157c`. A
  sound still running is left alone.
- Any other name first calls `sndPlaySound(NULL, SND_NODEFAULT)`, which stops whatever
  is playing, then frees the resource it held. `FUN_1008_0236` tests the name as a file
  on disk. A file plays from disk with the caller's flags. Otherwise
  `FindResource(SWCAUDIO.DLL, name, "WAVE")` finds it and `sndPlaySound` plays it with
  the caller's flags plus `SND_MEMORY`.

### The moving piece speaks

`FUN_1008_183f` starts a sound for every move, at `1008:18e6`. It indexes a table of
twelve far pointers at `DS:0x58` with `colour * 24 + piece * 4`. It passes flag 1, which
is `SND_ASYNC`. The twelve slots hold `0xffff` on disk because the loader fills them.
The type 3 relocation records of segment 60 point them at `DS:0x207` through `DS:0x270`.

| Piece | White | Black |
| --- | --- | --- |
| King | `LUKE.WAV` | `EMPEROR.WAV` |
| Queen | `LEIA.WAV` | `VADER.WAV` |
| Rook | `YODA.WAV` | `ATAT.WAV` |
| Bishop | `C3P0.WAV` | `BOBA.WAV` |
| Knight | `CHEWIE.WAV` | `SAND.WAV` |
| Pawn | `R2D2.WAV` | `STORM.WAV` |

That order matches the piece DLL table, so the white rook plays `YODA.WAV` and walks out
of `YO.DLL`. The sound starts before anything walks and before the code decides whether
a capture follows, so a move with the films switched off still speaks.

### The walk keeps it going

`FUN_1068_0fe6`, the walk stepper, calls `FUN_1008_1519("!repeat!", 0)` at `1068:1099`
once per frame, and `FUN_1068_1140`, the turn-in-place stepper, does the same at
`1068:1247`. A frame lasts 100 milliseconds, so the piece's line restarts on the first
100 millisecond boundary after it ends. It sounds for the whole walk that way. No walk
INI names a sound and no piece DLL holds a `WAVE` resource, so the game has no footstep
and no slide sound. The character's voice is the whole of it.

Two loaders call `"!repeat!"` for the same reason. `FUN_1058_0e15` calls it once per
timeline entry at `1058:1249` while it reads the ANX, and `FUN_1068_01d5` calls it once
per walk frame it loads at `1068:04d4`. Both keep the move sound alive across a load
that takes time.

The move sound ends in one of two places. When the move takes nothing, `FUN_1008_183f`
calls `FUN_1008_1519(0, 0, 0)` and the sound stops. When the move takes
something, `FUN_1058_0a0a` stops it before the film's first frame instead.

### The events that have their own sound

- Checkmate. `FUN_1008_1745` plays `WHTVIC.WAV`, named at `DS:0x340`, when white mates,
  and `BLKVIC.WAV` at `DS:0x34b` when black does, both with flag 1. Both sit on the CD
  as loose files, so `FUN_1008_0236` finds them on disk.
- A white king that captures or mates. `FUN_1008_1694` runs only when the capture code
  starts `WK`. It draws an extra sprite 20 pixels from the king, plays `BEN2.WAV` with
  flag 0, which is `SND_SYNC` and freezes the game until the line ends, and then plays
  `LUKE.WAV` with flag 1.
- The title screens. `FUN_1070_030a` plays `STWPRES.WAV` and then `SWTHEME.WAV` when the
  opening crawl starts, and `SWTHEME.WAV` again for the credit roll. `WM_DESTROY` plays
  `ENERGIZE.WAV`. The port plays `STWPRES.WAV` over the Toolworks logo instead, because
  in the original that line was heard over the logo screen `CHESSAPP.EXE` drew before
  `XCHESS.EXE` started, and the port runs no separate logo program.

### The events that have no sound

`XCHESS.EXE` spells 21 WAV names in its data segment, 19 of them distinct, and no more.
An illegal move, a check, a promotion, a draw and a button click all play nothing. The words `capture`,
`illegal`, `check`, `checkmate`, `draw` and `promote` do sit together at `DS:0x535e`
through `DS:0x539a`, but they are item names the `TWRXDDE` remote-control interface
accepts under its `CHESSSND` topic, not sounds this program plays.

## 12. Unresolved

- `loop_delay` in `[demo]` reaches `DAT_11d8_00dc` at `1008:5809`. The timer that uses it
  is untraced, so its units and the meaning of the `-1` default are unproven.
- Which `CM.INI` key persists `DAT_11d8_6887` and `DAT_11d8_6889`. Their behaviour is
  certain and their menu items are 72 and 71. Mapping them to `[look_feel] captures` and
  `[look_feel] sounds` is the obvious reading of the key list at file offset `0x34080`,
  but the code that reads those two keys is untraced.
- Where `DAT_11d8_6883` is set, the flag that chooses `SPACE256.BMP` over `THRON256.BMP`.
- `DAT_11d8_2870`, set to 1 when any frame has a sound. Nothing reads it.
- The message that reaches the `DAT_11d8_6787` check. Ghidra stopped disassembling the
  window procedure at `1008:4653`, so the suppression logic above came from hand-decoding
  the bytes at file offset `0x9217` plus the segment 2 relocation chains.
- `FUN_1010_1928` decodes the escape-byte runs and pads rows to a 4-byte stride, matching
  the decoder in `docs/plan.md` section 2. Ghidra's pointer typing inside it is
  unreliable, so the format was not re-derived from the code.
- Who plays `BRTH1.WAV`, the name at `DS:0x2126`. No call site turned up in the
  decompiled code, so it may be data nothing reads.
