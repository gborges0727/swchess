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
| `assets/catalog.json` | Source file hashes, extractor version, every resource name and offset, output hashes, and the three unresolved sound cues |
| `assets/captures/<NAME>/` | One RGBA PNG per distinct ANX record, plus `timeline.json` with the frames in play order |
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
| Timeline entries across 72 captures | 5423 |
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
- `cues.py` matches a `wav=` line to a sound file.
- `verify.py` reads a written PNG back for `--check-alpha`.
- `../reference/anx.py` decodes the escape-byte RLE that both the ANX files and
  the piece DLL bitmaps use.

## Things the run reports rather than hides

Three sound cues do not match a file name directly, and `catalog.json` lists all
three under `unresolved`:

- `WN.INI [WNBR_019]` writes `atftstep.awv`, and the resource is `ATFTSTEP.WAV`.
- `WP.INI [WPBB_002]` writes `r2alarm\.wav` with a stray backslash, and the
  resource is `R2ALARM.WAV`.
- `BB.INI [BBWQ_007]` asks for `LEIA2.WAV`, which the disc does not contain.
  Leave that cue silent until the code says what replaces it.

`WN.INI` lists only 71 frame keys under `[WNBR]` while `WNBR.ANX` holds 80
frames. The file still carries sections `[WNBR_072]` through `[WNBR_080]`, so
the extractor uses those and marks each recovered entry with
`"ini_key_source": "recovered from the frame section name"`.

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
