# Asset extractor

This package reads the original Star Wars Chess CD files and writes a local
asset cache. It never changes anything under `original/`.

```sh
python3 -m tools.extract --cd original/win3x/cd --out assets
python3 -m tools.extract --selftest
python3 -m tools.extract --check-alpha --out assets
```

The first command fills `assets/`, which `.gitignore` keeps out of the
repository. The second decodes five random ANX records and five random piece
bitmaps again and checks that each one produces exactly width times height
pixels. The third reads sprite PNGs back off disk and checks that alpha is 0 in
exactly the places where the source palette index is 0.

The extractor needs Python 3 and nothing else. It writes PNGs through `zlib`.

## What a run produces

| Path | Contents |
| --- | --- |
| `assets/catalog.json` | Source file hashes, extractor version, every resource name and offset, output hashes, per capture timing, and the three cues that stay silent |
| `assets/captures/<NAME>/` | One RGBA PNG per distinct ANX record, `timeline.json` with every timeline entry and where each field came from, and `resolved.json` with the poses the original draws and the millisecond each one appears at |
| `assets/pieces/<PIECE>/` | Every bitmap in the piece DLL under its original resource name, plus `manifest.json` built from the piece INI |
| `assets/sets/<SET>/` | The four piece sheets as `sheet.png`, their twelve cells, and `manifest.json` |
| `assets/backgrounds/` | The four 640 by 480 backgrounds as opaque PNGs |
| `assets/audio/` | 109 sounds from SWCAUDIO.DLL plus the four loose WAV files |
| `assets/locales/<language>.json` | Every string table resource with its bytes as hex |
| `assets/ui/` | The four TITLERES.DLL bitmaps, including LEGFONT at 0xe2c00 |

A finished run prints these counts and stores them in `catalog.json`. It exits
non-zero if any of them changes.

| Count | Value |
| --- | --- |
| Distinct capture records | 4799 |
| Timeline entries across 72 captures | 5414 |
| Poses the player actually draws | 5342 |
| Piece bitmaps | 1344 |
| WAVE records (109 distinct names) | 110 |
| BMP files | 8 |
| Locale files | 4 |

## How the modules divide the work

- `ne.py` walks the resource table of a 16-bit NE file. It reads resources
  straight out of the bytes and never loads a DLL.
- `dib.py` reads the plain uncompressed bitmaps: the eight BMP files and the
  four TITLERES resources.
- `ini.py` parses the game INI files and keeps every value as the raw string.
  Section and key lookup ignores case, the way the Windows profile reader does.
- `png.py` writes 8-bit RGB and RGBA PNGs with `zlib`.
- `captures.py`, `pieces.py`, `sheets.py`, `audio.py`, `locales.py` and `ui.py`
  each own one output folder.
- `cues.py` matches a `wav=` line to a WAVE resource name.
- `verify.py` reads a written PNG back for `--check-alpha`.
- `../reference/anx.py` decodes the escape-byte RLE that both the ANX files and
  the piece DLL bitmaps use.

## How a capture becomes a timeline

`docs/research/capture-player.md` describes the player in `XCHESS.EXE`, and
`resolved.json` follows it in six ways.

- The key names in `[BBWB]` are the timeline, in file order. Timeline entry `i`
  uses ANX record `i`. The number in a key name such as `BBWB_005` names the
  per-frame INI section and nothing else.
- A frame's position is the x and y pair at index `i` in table 2 of the ANX,
  which starts at file offset `0x25c`, plus the `[BBWB_OFFSET]` x and y. The x
  defaults to 215 and the y defaults to 100 when the section leaves the key out,
  which is what `GetPrivateProfileInt` is told to return at `1058:11bf` and
  `1058:11d9`. `timeline.json` carries the raw table values as `table_x` and
  `table_y`, the sum as `x` and `y`, and the dead per-frame INI keys as `ini_x`
  and `ini_y`.
- The player decodes pose 0 and frees it without drawing it, so a capture shows
  one fewer pose than its timeline lists. `resolved.json` starts at index 1.
- Pose `i` appears at `(i - 1) * frame_delay`, where `frame_delay` is 120 from
  `CM.INI [defaults]`. A blocking sound earlier in the run pushes every later
  pose out by however long it stalled the loop.
- After the last pose the player waits `[BBWB_OFFSET] hold` milliseconds, or
  1000 when the key is absent, then plays `[BBWB_OFFSET] wav` and waits for it
  to finish. `end_ms` is the moment after all of that, when the last pose is
  erased. Only `BKWR` and `BQWN` set that final wav.
- The 640 by 480 backdrop is the canvas, so `canvas` is the same rectangle in
  every file. Twenty captures place at least one pose partly off it, and the
  original clips rather than failing.

A frame's `sound` says what `pause=` in its INI section does.

| `pause` | `mode` | What happens |
| --- | --- | --- |
| absent or 0 | `async` | The sound starts and the animation carries on |
| 1 | `sync` | The animation stops until the sound finishes |
| 2 | `wait_previous` | The sound starts, and the next frame that has a sound waits for it |

The player ignores a `pause=` that has no `wav=` beside it, so `[WBBR_001]` and
`[WKBK_003]` change nothing. Sounds stall the loop in 30 of the 72 captures.
`timeline.json` records how long the loop stalled as `blocking_stall_ms` and how
much later that pushed `end_ms` as `blocking_added_ms`.

`cuts` is an empty list in every file. A later step fills it.

## Things the run reports rather than hides

`FUN_1058_0c4d` uppercases a `wav=` value and looks it up as a `WAVE` resource in
`SWCAUDIO.DLL`. It repairs nothing and reads no file. Three cues miss by a
character or two, so the original plays nothing on those frames, and
`catalog.json` lists all three under `unresolved`:

- `WN.INI [WNBR_019]` writes `atftstep.awv`, and no resource is named
  `ATFTSTEP.AWV`. `ATFTSTEP.WAV` exists, and the player never asks for it.
- `WP.INI [WPBB_002]` writes `r2alarm\.wav`, and the stray backslash survives the
  uppercasing. `R2ALARM.WAV` exists, and the player never asks for it.
- `BB.INI [BBWQ_007]` asks for `LEIA2.WAV`, which `SWCAUDIO.DLL` does not hold.

The extractor gives those three frames no sound, the same as the original.

`WN.INI` lists 71 frame keys under `[WNBR]` while `WNBR.ANX` holds 80 records.
The loader stops when the key names run out, so the capture is 71 entries long
and the last nine records never play. `timeline.json` records the nine as
`unused_anx_records`. The file also carries sections `[WNBR_072]` through
`[WNBR_080]`, and the extractor ignores them the way the original does.

Seven walk sequences declare a `count` that disagrees with the INI or the DLL.
One of them is `AT.INI [W]`, which lists 16 steps and declares 15.
`catalog.json` lists all seven under `piece_count_mismatches`.

Both `GRUNT1.WAV` records in `SWCAUDIO.DLL` hold identical bytes, which is why
110 records carry 109 distinct names. The extractor writes the file once and
records both resource offsets.

The sheets slice into six columns and two rows with a one pixel separator in
front of each cell, so cell k starts at `1 + k * (cell size + 1)` on both axes.
The extractor checks that every separator line really is one flat color and
writes the answer into each set manifest. Which column holds which piece is not
established yet, so `piece_order_verified` is `false` and the assumed names come
from the `CM.INI [demo]` numbering.

`FACING_P.BMP` is a 640 by 480 canvas holding the same 414 by 286 grid in its
top-left corner. The manifest records the unused canvas to the right and below.
