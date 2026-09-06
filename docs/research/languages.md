# Languages and fonts

This note answers how Star Wars Chess stores its four translations and how it
draws them. Everything below comes from reading the files on the CD with Python,
`strings` and `xxd`. No decompiler ran.

Two scripts in `tools/fonts/` reproduce every number here:

- `tools/fonts/legfont.py` cuts both bitmap fonts into 96 per-glyph PNGs each
  under `preview/legfont/` and `preview/guitext/`, then draws `preview/sample.png`,
  `preview/sample_guitext.png` and `preview/sample_real_strings.png`.
- `tools/fonts/strings_csv.py` writes `tools/fonts/strings.csv`, which lines the
  four languages up by string id and turns the stored bytes into readable text.

## 1. The four resource DLLs

`RESENG.DLL`, `RESFRN.DLL`, `RESGER.DLL` and `RESSPN.DLL` are each 19,968 bytes.
Each one holds 33 resources and nothing else. Every resource has type 6, which is
the 16-bit Windows string table. There is no bitmap, no dialog, no menu and no
private data type in any of the four files.

The 33 resources sit back to back from file offset 0xc00 to 0x4dff, one every
0x200 bytes, in the same order in all four files. A string table resource holds
sixteen strings, each written as a length byte followed by that many bytes. A
length byte of zero leaves that id empty on purpose, so the extractor keeps it.
The string id is `(resource id - 1) * 16 + slot`, which puts ids 14000 to 14015
in resource 876 and ids 0 to 15 in resource 1.

That gives 33 * 16 = 528 string slots per language, and the four files use the
same 33 resource ids, so all 528 ids line up across the four languages. How many
of the 528 carry visible text differs a little:

| Language | File | Slots with bytes | Slots with visible text |
| --- | --- | --- | --- |
| English | RESENG.DLL | 301 | 295 |
| French | RESFRN.DLL | 300 | 295 |
| German | RESGER.DLL | 299 | 299 |
| Spanish | RESSPN.DLL | 296 | 296 |

Six English slots and five French slots hold nothing but a space character,
which the crawl uses to leave a blank line. German and Spanish hold no such
slot.

Outside the string tables the four DLLs are byte for byte identical except in six
places. Three bytes at 0x408 hold the NE build timestamp, and three bytes at
0x5dc hold the module name inside the file. The files carry no code that differs
per language.

`XCHESS.EXE` names the four DLLs in one block starting at file offset 0x33e3a, in
the order RESENG, RESGER, RESFRN, RESSPN. `SWCMPC.INI` picks one with
`[language] language=0`. The order in the EXE suggests 0 selects English, 1
German, 2 French and 3 Spanish, but only the code that indexes that block can
confirm it, so treat the numbers 1 to 3 as unconfirmed.

## 2. The character encoding

Every byte of every string in the four files falls between 0x20 and 0x7e.
Nothing is stored as a high byte, so no Windows code page describes the text.
The game reuses printable ASCII codes for accented letters and symbols, and each
of the two bitmap fonts fills those codes differently.

Both fonts hold exactly 96 glyph cells, one per byte from 0x20 to 0x7f. A byte
selects cell number `byte - 0x20`. So the same string can only be read once you
know which font draws it.

LEGFONT has no digits at all. Its ten digit cells hold accented lowercase
letters, and four more cells hold two digit shapes and two legal symbols:

| Byte | Character | Byte | Character |
| --- | --- | --- | --- |
| `0` | a with dieresis | `5` | o with dieresis |
| `1` | a with grave | `6` | u with dieresis |
| `2` | e with grave | `7` | a with acute |
| `3` | e with acute | `8` | i with acute |
| `4` | e with circumflex | `9` | sharp s |
| `[` | the digit 1 | `]` | the copyright sign |
| `\` | the digit 0 | `^` | the registered sign |

Every other byte with a glyph in LEGFONT means itself: space, `&`, `'`, `,`, `-`,
`.`, A to Z and a to z. LEGFONT has no glyph for any other punctuation.

GUITEXT keeps the digits as digits and instead puts the three German capitals in
the bracket cells:

| Byte | Character |
| --- | --- |
| `[` | U with dieresis |
| `\` | O with dieresis |
| `]` | A with dieresis |

Every expected word confirms the tables. In `RESFRN.DLL`, id 14001 stores
`tr2s` for "très", id 14005 stores `3poque` for "époque", id 14012 stores
`tr4ve` for "trêve" and id 14014 stores `1 travers` for "à travers". In
`RESGER.DLL`, id 14004 stores `gro9er` for "großer", id 14006 stores
`geschw0chten` for "geschwächten" and id 14019 stores `gr59ten` for "größten".
In `RESSPN.DLL`, id 14001 stores `atr7s` for "atrás" and id 14013 stores
`per8odo` for "período". On the menu side `RESGER.DLL` id 21 stores
`]NDERUNGSMEN[` for "ÄNDERUNGSMENÜ", id 33052 stores `L]UFERS` for "LÄUFERS" and
id 33035 stores `K\NIG` for "KÖNIG".

The two mappings do not conflict in practice because no id is drawn by both
fonts. `MPC  version [.\` at id 15003 reads "MPC version 1.0" and
`Copyright ] MCMXCIII` at id 15182 reads "Copyright © MCMXCIII", both through
LEGFONT. `K\NIG GEHT 1 FELD WEITER` at id 33038 reads "KÖNIG GEHT 1 FELD WEITER"
through GUITEXT, where the `1` is a real digit.

Neither font holds an o with acute, an n with tilde, or any accented capital
beyond the three German ones. The translators worked around that: Spanish id
33062 spells the pawn "PEON" with no accent, and no Spanish string needs an n
with tilde.

`RESGER.DLL` id 74 stores `HINTERGRUND [NDERN`, which draws as "HINTERGRUND
ÜNDERN" where the German should read "ÄNDERN". That is a typo in the original
file, not a second meaning for the byte `[`.

Three ids ignore the substitution because they are numbers the game parses rather
than text it draws. Id 14000 holds the crawl's line count, which is 27 in
English, 26 in French, 29 in German and 28 in Spanish. Id 15000 holds 189 in all
four languages, which is probably the credit roll's line count, though nothing in
the data proves that.

No string in any of the four files contains a `%`. The message templates that
take arguments live in `XCHESS.EXE` itself, for example `%s_%s%03d` at file
offset 0x36e80 and `%d.CMG` near 0x33f14. None of them is translated.

## 3. The two fonts

### LEGFONT

`TITLERES.DLL` stores LEGFONT as an uncompressed 8-bit DIB at file offset
0xe2c00, 640 by 253 pixels. The extractor already writes it to
`assets/ui/LEGFONT.png`.

The sheet is a fixed grid, not a packed strip. White separator lines run across
the whole width at y = 0, 42, 84, 126, 168 and 210, and down the columns every 40
pixels. That makes 6 rows of 16 cells, and each cell measures 40 by 42 with the
separator included. Cell `n` starts at x = 40 * (n mod 16) + 1 and
y = 42 * (n div 16) + 1, and its drawable area is 39 by 41 pixels. The 96 cells
map straight onto bytes 0x20 to 0x7f in reading order.

Palette index 159 is the background and is pure black. The letters are yellow,
mostly index 100 at RGB 255, 251, 95, with index 255 white on the highlights and
darker yellows down the shaded edges. 39 of the 256 palette entries appear.

`XCHESS.EXE` carries the advance widths. A 16-bit word at file offset 0x36ebe
holds 40 and a word at 0x36ec0 holds 42, the cell width and cell height. At
0x36ec4 a run of 96 little-endian 16-bit words gives the advance width of each
cell, in the same 0x20 to 0x7f order. Space advances 10 pixels, `M` advances 28,
`W` advances 34 and `i` advances 9. Every entry sits one or two pixels wider than
the ink measured off the sheet, which proves the alignment. An entry of zero
marks a cell with no glyph, and those zeros match the cells with no ink, so the
width table decides which bytes LEGFONT can draw.

Five cells hold leftover artwork rather than glyphs: a stray pixel at `=`, and
partial copies of the copyright sign and other marks at `{`, `|`, `}` and `~` in
the last sheet row. All five have a width of zero, so the game never draws them.

### GUITEXT

The menus use a second font, and it is not a Windows font. `XCHESS.EXE` never
calls `CreateFont` or `CreateFontIndirect`. Walking the NE relocation records for
imported ordinals shows no GDI.56 and no GDI.57 anywhere in the executable. The
only face name in the file is "System", inside an unrelated block.

Instead the game links against a custom control library named CC and calls
`InitCC` from it. `CC16.DLL` and `CC256.DLL` each hold a bitmap resource named
GUITEXT at file offset 0x5800, both 1542 by 17 pixels at 8 bits per pixel, and
`CC.DLL` contains the strings `guitext` and `_GuiText`.

GUITEXT is one horizontal strip. A vertical separator line in palette index 25
sits every 16 pixels, and a horizontal one runs along y = 16. Cell `n` occupies
x = 16 * n to 16 * n + 14 and y = 0 to 15, so 96 cells fit with 6 unused pixels
at the right edge. The cells map onto bytes 0x20 to 0x7f as LEGFONT's do.

The letters are red, palette index 70 at RGB 227, 0, 0, on the same black index
159. The capitals are drawn as five separate one-pixel scanlines at y = 1, 3, 5,
7 and 9, with the rows between them left black. Descenders continue on y = 11 and
y = 13. Punctuation does not follow that spacing: the hyphen sits alone on y = 4,
the full stop on y = 8, and the colon on y = 3, 4, 6 and 7. So the gaps are a
deliberate scanline look, not a storage trick. Rendering the cell as it is stored produces
readable text, as `tools/fonts/preview/sample_real_strings.png` shows.

GUITEXT has no advance-width table that I could find. Searching `CC.DLL`,
`CC16.DLL`, `CC256.DLL` and `XCHESS.EXE` for 96 consecutive bytes or 96
consecutive 16-bit words close to the measured ink widths returned nothing, so
`legfont.py` advances by the measured ink width plus one pixel. Whether the
original library measures the cell at run time or uses a table I did not find is
unresolved.

The GUITEXT cells at bytes `^` and `_` hold shapes I cannot name. No string in
any of the four languages uses either byte, so the port can leave them out.

### Gaps in both fonts

Neither font covers every character its own strings use. LEGFONT has no `!`, no
`:` and no `/`. The credit line at id 15081,
`Effects/Touch-up Artists`, therefore draws with a hole where the slash belongs
in the original game.

GUITEXT has no comma. Seven Spanish strings use one anyway: ids 32773, 32774,
32786, 33031, 33035, 33036 and 33037, for example `TABLAS,JUEGO NO TIENE FIN`.
Those commas draw as blank space in the original. The port either reproduces
that blank or adds a comma glyph the original never had, and that is a choice to
make once rather than by accident.

## 4. Which ids are what

Every id in the crawl and the credit roll goes through LEGFONT. Every other id
goes through GUITEXT. Nothing in the string tables is a format template, because
no string contains a `%`.

| Id range | Resources | Slots | Visible in English | Font | What they are |
| --- | --- | --- | --- | --- | --- |
| 0 to 111 | 1 to 7 | 112 | 58 | GUITEXT | Menu button labels, seven screens of them |
| 2048 to 2095 | 129 to 131 | 48 | 15 | GUITEXT | Board setup refusals and move history notices |
| 2112 to 2127 | 133 | 16 | 2 | GUITEXT | Two startup notices |
| 2144 to 2159 | 135 | 16 | 2 | GUITEXT | The two answers to a draw offer |
| 14000 to 14031 | 876, 877 | 32 | 21 | LEGFONT | The opening crawl, one line per id, plus a line count at 14000 |
| 14992 to 15199 | 938 to 950 | 208 | 138 | LEGFONT | The credit roll, alternating job titles and names |
| 32768 to 32799 | 2049, 2050 | 32 | 8 | GUITEXT | End of game and check announcements |
| 33024 to 33087 | 2065 to 2068 | 64 | 51 | GUITEXT | Illegal move explanations |

The menu block gives each screen its own sixteen ids: the top level menus at 0,
player selection at 16, the action commands at 32, the settings and player
combinations at 48, the on and off toggles at 64, board setup and difficulty at
80, and the quit confirmation at 100.

The illegal-move block runs in piece order. Ids 33024 to 33039 hold general
refusals and the castling rules. Ids 33040 to 33046 belong to the king, 33047 to
33051 to the rook, 33052 to 33056 to the bishop, 33057 to 33060 to the knight,
and 33061 to 33076 to the pawn.

## 5. No LEGEND table exists here

The DOS build's `LEGEND_AMR`, `LEGEND_FRN`, `LEGEND_GER` and `LEGEND_SPN`
resources have no counterpart in the Windows release. Searching `XCHESS.EXE`,
`TITLERES.DLL` and all four RES DLLs for `LEGEND` returns nothing. `TITLERES.DLL`
holds four bitmaps and no other resource: STARTITL, LEGAL, STLGO16 and LEGFONT.
LEGFONT at file offset 0x36fd5, beside the string `TITLERES.DLL`, is the only
match for "LEG" in `XCHESS.EXE`.

There is no per-language layout table either. The four RES DLLs differ only in
their string bytes, their timestamp and their name, so no coordinate, width or
line count varies per language beyond the crawl line count at id 14000. Whether
`XCHESS.EXE` shifts any layout by language is a question for whoever reads the
drawing code.

## 6. What stays unresolved

- That `language=1` means German, `2` French and `3` Spanish follows only from
  the order of the DLL names at 0x33e3a.
- Id 15000 holds 189 in all four languages, and nothing checked here shows what
  reads it.
- GUITEXT's advance widths are measured off the bitmap. No table matching them
  turned up in `CC.DLL`, `CC16.DLL`, `CC256.DLL` or `XCHESS.EXE`.
- The GUITEXT cells at bytes `^` and `_` hold shapes I could not identify.
- Whether the game draws GUITEXT at its stored size or scales it is untested.
- LEGFONT's line spacing and the crawl's scroll speed live in the drawing code,
  which this note did not read.
