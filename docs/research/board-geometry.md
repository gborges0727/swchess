# Board geometry and piece placement in XCHESS.EXE

This records how the Windows front end turns a chess square into a screen pixel, which sheet
cell holds which piece, how a click becomes a square, and how a walking piece moves between
squares. Every claim names the function and instruction addresses behind it.

## How the code was read

Ghidra 12.1.3 headless imported `original/win3x/cd/XCHESS.EXE` with its NE loader and the
`x86:LE:16:Protected Mode` language, and all 1,183 functions decompiled. The decompiler
handles control flow but loses the arguments of Borland's 32-bit helper calls `LXMUL@` (at
`1000:0294`) and `LDIV@` (at `1000:034c`), so every arithmetic formula below comes from the
disassembly instead.

Ghidra names code segment N `CodeN` at selector `0x1000 + 8*(N-1)`, and data segment 60 starts
at file offset `0x33bc0`, so a data address `11d8:XXXX` is file offset `0x33bc0 + XXXX`.

## Where the live numbers come from

`CM.INI [board]` is not the source of the numbers the game runs on. `FUN_1028_14a8` at
`1028:14a8` calls `GetPrivateProfileInt` 26 times for every key in that section, and Ghidra's
reference listing, with NE relocations applied, finds no caller for it, so the whole section
is dead code in this build.

The live values arrive from `CMWIN.DAT`, a 144-byte file. `FUN_1008_37bc` at `1008:37bc` opens
it, checks that its length is exactly `0x90`, and reads it straight over the data block at
`11d8:67ff`, which covers every board variable. The shipped `original/win3x/cd/CMWIN.DAT` and
`CMWIND.DAT` are byte-identical and decode like this.

| Data address | Byte in CMWIN.DAT | Meaning | Shipped value |
| --- | --- | --- | --- |
| `11d8:680f` | `+0x10` | `bitmap_style` | 2 |
| `11d8:6811` | `+0x12` | `vanishpt_3D` | 335 |
| `11d8:6813` | `+0x14` | `size_3D` | 535 |
| `11d8:6815` | `+0x16` | `turn_3D` | 360 |
| `11d8:6817` | `+0x18` | `tilt_3D` | 307 |
| `11d8:6819` / `681b` | `+0x1a` / `+0x1c` | 3D wood board center | 319, 226 |
| `11d8:681d` / `681f` | `+0x1e` / `+0x20` | 2D wood board center | 275, 219 |
| `11d8:6821` / `6823` | `+0x22` / `+0x24` | 3D marble board center | 318, 256 |
| `11d8:6825` / `6827` | `+0x26` / `+0x28` | 2D marble board center | 275, 217 |
| `11d8:6829` / `682b` | `+0x2a` / `+0x2c` | `size_2D` and `turn_2D` | 394, 360 |
| `11d8:682d` | `+0x2e` | live vanishing point | 335 |
| `11d8:682f` | `+0x30` | live board size | 535 |
| `11d8:6831` | `+0x32` | live turn angle | 360 |
| `11d8:6833` | `+0x34` | live tilt angle | 307 |
| `11d8:6841` | `+0x42` | selected chess set | 0 |
| `11d8:6883` | `+0x84` | background choice | 1 |

Neither `3dmarblebitmap_xcenter` nor `use_piece_scaling` appears anywhere in the executable,
so those two `CM.INI` keys change nothing, and `CMWIN.DAT` supplies the 3D marble center
instead. After loading the file, `FUN_1008_37bc` reads six keys from `SWCMPC.INI [look_feel]`:
`turn` overwrites the live turn angle at `11d8:6831`, and `board` fills `11d8:6883`, which
picks the background bitmap.

## The projection

`FUN_1028_0d1e` at `1028:0d1e` builds the nine model coordinates of the grid lines:

```
step = size / 4;
for (i = 0; i < 9; i++) grid[i] = step * (i - 4);     // grid[] lives at 11d8:6f14
```

With size 535 the step is 133 and the grid runs -532, -399, -266, -133, 0, 133, 266, 399,
532. One square is 133 model units wide. `11d8:6f24` is `grid[8]`, and `11d8:6f1e` is
`grid[5]`, which equals one square width.

`FUN_1028_0107` at `1028:0107` projects one model point in six steps:

1. `1028:0119` picks the vanishing point. When `bitmap_style` is non-zero and the board is not
  flat it takes `vanishpt_3D`, otherwise the live value at `11d8:682d`. Both hold 335, so the
  choice does not matter here.
2. `1028:0160` rotates about Z by the live turn angle, through `FUN_1038_04c9`.
3. `1028:0178` rotates about X by the live tilt angle, through `FUN_1038_0407`.
4. `1028:0180` computes `d = grid[8] * vanishpt / 100` and translates by `(0, 0, -d)` through
  `FUN_1038_052a`. With grid[8] = 532 and vanishpt = 335, `d` is 1782.
5. `1028:01bc` writes `abs(z)` of the translated point to the caller's depth output.
6. `1028:01f3` divides by perspective through `FUN_1038_0626`, then stores x and y.

The matrix library works in signed 32-bit fixed point where 1.0 is 32767. `FUN_1038_0038` at
`1038:0038` multiplies a 4-vector by a 4x4 matrix and divides each product by 32767 at
`1038:0067`. The sine table sits at `11d8:15a4` (file `0x35164`) and the cosine table at
`11d8:1b44` (file `0x35704`). Each holds 360 signed longs equal to the function times 32767,
indexed by whole degrees. `FUN_1038_03c1` sets the w component to the integer 1, so a
translation row holding `32767 * t` contributes exactly `t`.

`FUN_1038_0626` at `1038:0626` does the perspective divide. At `1038:0679` it forms `t = 32767
+ (-32767 * z) / d`, at `1038:06a6` it multiplies every component by 32767, and at `1038:06b7`
it divides every component by `t`. The result is `out = v * d / (d - z)`.

All divisions are C integer divisions that truncate toward zero.

## Square centers and square corners

`FUN_1028_020f` at `1028:020f` is `GetSquareOrigin(row, col, POINT *out, int *depth, BOOL
addCenter)`. It projects the square corner `(grid[col], grid[row], 0)` and adds the board
center offset when `addCenter` is set. Row 0 is rank 8 and column 0 is file a, which
`FUN_1008_1197` at `1008:1197` confirms by converting an algebraic string with
`out[0] = s[0] - 'a'` and `out[1] = '8' - s[1]`.

`FUN_1028_0f6d` at `1028:0f6d` is `GetPieceOrigin(row, col, POINT *out, int *depth)`. It adds
half a square to both model coordinates before projecting, so it returns the square center,
then adds the board center offset and rewrites the depth through `FUN_1028_095d` mode 4.

The board center offset comes from `FUN_1028_095d` mode 2 at `1028:0a94`. With `bitmap_style`
equal to 2 and the board tilted it returns `11d8:6821` and `11d8:6823`, which `CMWIN.DAT` sets
to (318, 256). With `bitmap_style` equal to 1 it returns the wood centers, and with 0 it
centers the board inside the window.

`FUN_1028_00e8` at `1028:00e8` tests whether the live tilt angle equals 360, which means the
board is flat. `CMWIN.DAT` sets 307, so the board is tilted and the sets marked `3` in `CM.INI
[chesssets]` are the ones in play. Running the arithmetic with the shipped numbers gives the
square centers below, each the pixel a sprite anchors to on the 640x480 screen in the default
orientation with white at the bottom.

| rank | a | b | c | d | e | f | g | h | z |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8 | 108,130 | 168,130 | 228,130 | 288,130 | 347,130 | 408,130 | 468,130 | 528,130 | 2154 |
| 7 | 102,163 | 164,163 | 225,163 | 287,163 | 348,163 | 410,163 | 472,163 | 534,163 | 2047 |
| 6 | 95,199 | 159,199 | 223,199 | 286,199 | 349,199 | 413,199 | 476,199 | 540,199 | 1941 |
| 5 | 89,237 | 154,237 | 220,237 | 285,237 | 350,237 | 416,237 | 481,237 | 547,237 | 1835 |
| 4 | 82,275 | 150,275 | 217,275 | 285,275 | 351,275 | 418,275 | 486,275 | 553,275 | 1730 |
| 3 | 75,318 | 144,318 | 214,318 | 283,318 | 352,318 | 422,318 | 491,318 | 561,318 | 1624 |
| 2 | 67,363 | 139,363 | 210,363 | 282,363 | 353,363 | 425,363 | 497,363 | 569,363 | 1517 |
| 1 | 58,411 | 133,411 | 207,411 | 281,411 | 354,411 | 429,411 | 503,411 | 577,411 | 1411 |

The nine by nine grid corners, which `FUN_1028_0d58` turns into the 64 hit regions:

| corner row | c0 | c1 | c2 | c3 | c4 | c5 | c6 | c7 | c8 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| r0 | 81,114 | 140,114 | 200,114 | 259,114 | 318,114 | 377,114 | 436,114 | 496,114 | 555,114 |
| r1 | 74,146 | 135,146 | 196,146 | 257,146 | 318,146 | 379,146 | 440,146 | 501,146 | 562,146 |
| r2 | 67,181 | 130,181 | 193,181 | 256,181 | 318,181 | 380,181 | 443,181 | 506,181 | 569,181 |
| r3 | 60,218 | 125,218 | 189,218 | 254,218 | 318,218 | 382,218 | 447,218 | 511,218 | 576,218 |
| r4 | 52,256 | 119,256 | 185,256 | 252,256 | 318,256 | 384,256 | 451,256 | 517,256 | 584,256 |
| r5 | 44,297 | 113,297 | 181,297 | 250,297 | 318,297 | 386,297 | 455,297 | 523,297 | 592,297 |
| r6 | 36,341 | 106,341 | 177,341 | 248,341 | 318,341 | 388,341 | 459,341 | 530,341 | 600,341 |
| r7 | 26,387 | 99,387 | 172,387 | 245,387 | 318,387 | 391,387 | 464,387 | 537,387 | 610,387 |
| r8 | 17,437 | 92,437 | 168,437 | 243,437 | 318,437 | 393,437 | 468,437 | 544,437 | 619,437 |

The board painted into `original/win3x/cd/SPACE256.BMP` matches these numbers. On scanline
y=275, the middle of rank 4, the artwork's light-to-dark transitions sit at x = 51, 117, 185,
249, 321, 386, 454, 520 and 586, against 48, 116, 183, 251, 318, 385, 453, 520 and 588
predicted by interpolating corner rows r4 and r5. Scanlines y=163 and y=411 agree as closely,
and no edge is off by more than four pixels.

## Where the sprite lands relative to that pixel

`FUN_1018_09f3` at `1018:09f3` turns the anchor point into the sprite rectangle:

```
w = dib.width; h = dib.height;                       // 68 x 142 for a 3D set cell
anchorY = (bScale == 0) ? h / 2 : h - w / 4;
rect.left = x - w / 2;
rect.top  = y - anchorY;
rect.right  = rect.left + w + 1;
rect.bottom = rect.top  + h + 1;
```

`FUN_1020_0948` passes `bScale` as "the board is not flat" at its two `SetSpritePosition` calls
`1020:09b4` and `1020:0a53`. A 3D set therefore anchors the sprite at bottom center raised by a
quarter of the cell width: with a 68 by 142 cell the anchor sits 17 pixels above the bottom
edge and halfway across. The 2D set anchors the sprite at its exact center.

The sprites are never scaled by rank. `FUN_1018_09f3` sizes the rectangle from the bitmap
alone, and `FUN_1018_0b64` at `1018:0b64` calls `BitBlt` whenever the source and destination
extents match, which they always do, so its `StretchBlt` branch is unreachable for pieces. The
depth value only orders and picks sprites.

## Which sheet cell holds which piece

`FUN_1020_0304` at `1020:0304` computes a cell rectangle:

```
memcpy(colMap, DS:0x1256, 12);                       // six words
*outW = set[setIndex].cellWidth;                     // 11d8:68bb + setIndex * 0x31
*outH = set[setIndex].cellHeight;                    // 11d8:68b9 + setIndex * 0x31
*outX = (*outW + 1) * colMap[piece] + 1;
*outY = (*outH + 1) * colorRow + 1;
```

`DS:0x1256` is file offset `0x34e16` and holds the words 5, 4, 1, 3, 2, 0, sitting directly
after two unterminated character arrays at file `0x34e0e`, `KQRBNP` then `WB`. `FUN_1020_036f`
at `1020:036f` loops `colorRow` over 0 and 1 and `piece` over 0 to 5, loads each cell, and
stores the handle in `11d8:6e40[piece + colorRow * 6]`.

The piece index runs K=0, Q=1, R=2, B=3, N=4, P=5, matching the `CM.INI [demo]` comment "K=0
Q=1 R=2 B=3 N=4 P=5". Two other tables name the characters. The sound file names at file
`0x33dc7` run LUKE, LEIA, YODA, C3P0, CHEWIE, R2D2, EMPEROR, VADER, ATAT, BOBA, SAND, STORM.
The piece DLL prefixes at file `0x36d1e` run LS, LO, YO, C3, CB, R2, EM, DV, AT, BF, SP, ST,
and the walk player indexes them as `color * 6 + piece`. Both lists put the six white
characters first in K, Q, R, B, N, P order.

| Piece | Index | Sheet column | White cell | Black cell |
| --- | --- | --- | --- | --- |
| King | 0 | 5 | Luke Skywalker (`LS`) | Emperor (`EM`) |
| Queen | 1 | 4 | Leia Organa (`LO`) | Darth Vader (`DV`) |
| Rook | 2 | 1 | Yoda (`YO`) | AT-ST walker (`AT`) |
| Bishop | 3 | 3 | C-3PO (`C3`) | Boba Fett (`BF`) |
| Knight | 4 | 2 | Chewbacca (`CB`) | Tusken Raider (`SP`) |
| Pawn | 5 | 0 | R2-D2 (`R2`) | Stormtrooper (`ST`) |

Reading the columns left to right gives pawn, rook, knight, bishop, queen, king, which is
exactly the observed sheet order R2-D2, Yoda, Chewbacca, C-3PO, Leia, Luke on row 0 and
Stormtrooper, AT-ST, Tusken Raider, Boba Fett, Vader, Emperor on row 1. **Row 0 is white and
row 1 is black.**

Cell k starts at pixel `1 + k * (size + 1)` on both axes, which the extractor already assumes.
For `WHTBTM_P.BMP` and `WHTTOP_P.BMP` that gives `(68 + 1) * 6 = 414` wide and
`(142 + 1) * 2 = 286` tall, matching the file exactly. For `2DSET_P.BMP` it gives 228 by 92,
one pixel short of the 229 by 93 file, so that sheet ends with a trailing separator line.

## Which set is active

`11d8:6841` holds the set index into the record array parsed from `CM.INI [chesssets]`.
Records are `0x31` bytes each, in file order 0 `WHTBTM_`, 1 `WHTTOP_`, 2 `2DSET_`, 3 `FACING_`.
`FUN_1008_37bc` at `1008:3938` and `1008:3940`, and `FUN_1008_00a3` at `1008:00e7` and
`1008:00ef`, both store `turn % 360 != 0`, so the default turn of 360 selects `WHTBTM_` and any
other turn selects `WHTTOP_`. `FUN_1008_00a3` stores 3 at `1008:013d` on the piece setup
screen, the only use of `FACING_`.

`FUN_1008_0e73` at `1008:0e73` then rejects records that do not fit. A flat board demands a
dimension character other than `3` and a cell height under 100, which only `2DSET_` satisfies,
while a tilted board demands a dimension character other than `2` and a cell width under 96.
Both branches also demand palette character `P` in 256-color mode, `N` otherwise.

## Backgrounds

`FUN_1008_1311` at `1008:1311` picks the background bitmap.

| Condition | Bitmap |
| --- | --- |
| `11d8:0127` is 2 and turn is a multiple of 360 | `2dbdbtop.bmp` |
| `11d8:0127` is 2 and turn is not a multiple of 360 | `2dbdwtop.bmp` |
| otherwise `11d8:6883` is 0 | `thron256.bmp` |
| otherwise `11d8:6883` is non-zero | `space256.bmp` |

`11d8:6883` comes from `SWCMPC.INI [look_feel] board`, and `CMWIN.DAT` ships it as 1, so the
game starts in space. The two 2D board bitmaps are unreachable in this build: the only
immediate stores to `11d8:0127` write 1 (at file `0x767a`) and 0 (at file `0x9f84`), and the
third write site sits inside `FUN_1008_06b9`, which has no cross reference.

## Draw order

`FUN_1018_0bd0` at `1018:0bd0` composites the frame. It blits the background into an offscreen
bitmap, then walks a sorted sprite list. `FUN_1018_0ae3` at `1018:0ae3` builds that list,
advancing past every entry whose depth is at or above the new sprite's depth before inserting,
so the list runs from largest depth to smallest. Each sprite is drawn twice, first its mask
with `SRCAND` and then its color with `SRCPAINT`.

Smaller depth means nearer. The depth column above shows rank 8 at 2154 and rank 1 at 1411, so
**the draw order is back to front by rank** and a nearer piece overdraws a farther one.

## Mouse hit test

`FUN_1028_0d58` at `1028:0d58` builds the hit shapes once per layout change. For each of the 64
squares it calls `GetSquareOrigin` on the four corners with the center offset applied, calls
`CreatePolygonRgn(pts, 4, ALTERNATE)`, and stores the handle in `11d8:6f26[col + row * 8]`.

`SUB_1028_08b2` at `1028:08b2` is the hit test itself, called from the `WM_LBUTTONDOWN` arm of
`WNDPROC` at `1008:490f` with the mouse x and y. It runs two steps:

1. When the caller asks for a piece first, try `FUN_1020_0f11` at `1020:0f11`. That calls
  `FUN_1018_1e0f` at `1018:1e0f`, which loops all 256 sprite slots, tests `PtInRect` against
  each sprite rectangle, selects that sprite's mask bitmap into a memory DC, and reads
  `GetPixel(x - rect.left, y - rect.top)`. A black mask pixel means the point is on the
  drawing. Among the hits it keeps the smallest depth, so the nearest piece wins, then scans
  the 8 by 8 board for the piece owning that sprite.
2. Failing that, loop rows 0 to 7 and columns 0 to 7 and call `PtInRegion` on each stored
  region at `1028:090e`. The first region containing the point gives the square.

The piece pick is pixel accurate against the sprite mask, the empty-square pick is a
point-in-polygon test against the projected quadrilateral, and no code inverts the projection
arithmetically.

## Walking between squares

`FUN_1008_148b` at `1008:148b` starts a move. It calls `GetPieceOrigin` for the origin square
and again for the destination square, then branches on the walking flag at `11d8:0136`.

With walking off it calls GDI `LineDDA` at `1008:150c` with the two square centers and
`LINEPROC` at `1008:55d8` as the callback. `LINEPROC` drops every other callback (`1008:5617`
tests the low bit of a counter), recomputes the depth from the current y through
`FUN_1028_095d` mode 4, and calls `SetSpritePosition`. The piece slides along the straight
screen line between the two square centers.

With walking on it calls `FUN_1068_1457`, and `LINEPROC` routes each surviving LineDDA point
into `FUN_1068_0fe6` at `1068:0fe6` instead. That function recomputes the depth, biases it by
10 either way so the mover passes in front of or behind the stationary pieces, cross-fades to
the next walk frame through `FUN_1018_258a`, advances the frame with `FUN_1068_0f69`, and
busy-waits on `timeGetTime` until 100 milliseconds have passed. A walk frame lasts 100 ms and
takes its position from the LineDDA path, not from the INI offsets. `FUN_1068_1140` at
`1068:1140` is the other stepper, used for turning in place, and it does add the INI offsets
as `x = base.x + dx[frame]` and `y = base.y + dy[frame]` before biasing the depth by 15 and
waiting the same 100 ms.

`FUN_1068_01d5` at `1068:01d5` loads a walk. For each direction section it reads `count` with
`GetPrivateProfileInt`, then reads numbered keys `%03d` starting at 0 with
`GetPrivateProfileString` and the default string `255,-255`, parses each with `%d,%d`
(`1068:03e8`), and **skips any step that parses back as the default**. The accept counter and
the key counter are separate, and the loop runs until the accept counter reaches `count`.

That resolves `AT.INI [W]`, which lists keys 001 through 016 and declares `count=15`. Key
`000` is commented out, so the first read returns the default and is skipped without consuming
a slot. Keys 001 through 015 are then accepted, the counter reaches 15, and **key 016 is never
read**. The same rule explains the other six count mismatches the extractor reports.

For each accepted step the loader stores `dx` at `11d8:84ac`, `dy` at `11d8:84ae`, and a step
budget at `11d8:84a8`, all with a stride of 12 bytes. `FUN_1068_0156` at `1068:0156` computes
that budget as `max(|dx|, |dy|) - 1`, floored at zero, which is the Bresenham length the
artwork's feet travel in that frame.

`FUN_1068_0013` at `1068:0013` chooses the direction section. It takes the column and row
deltas of the move, negates both when the board is turned, and returns an index into the name
table at file `0x36d42`:

| Index | Section | Delta |
| --- | --- | --- |
| 0 | `[S]` | dx = 0, dy > 0 |
| 1 | `[SW]` | dx < 0, dy > 0 |
| 2 | `[W]` | dx < 0, dy = 0 |
| 3 | `[NW]` | dx < 0, dy < 0 |
| 4 | `[N]` | dx = 0, dy < 0 |
| 5 | `[NE]` | dx > 0, dy < 0 |
| 6 | `[E]` | dx > 0, dy = 0 |
| 7 | `[SE]` | dx > 0, dy > 0 |

Row indices grow downward on screen, so positive dy is toward rank 1 and is south.

`FUN_1068_019d` at `1068:019d` picks the shorter way round when the piece has to turn: it
subtracts the two direction indices, adds 8 if the result is negative, and returns +1 for a
difference under 5 and -1 otherwise. The rotation frame for each heading lives in a table at
`11d8:3086`, file `0x36c46`, laid out as `color * 0x6c + piece * 0x12 + direction * 2` bytes.
Its rows match the DOS release's `ROTATION.CFG` line for line: the first is the `LS_R001.IMG`
row 0, 4, 7, 12, 14, 18, 21, 25, 27, and the seventh begins the `EM_R001.IMG` row. Each row
holds nine entries for eight headings, and the ninth equals one less than the `count`.

## Unresolved

- The ninth entry of each rotation row equals one less than the `[R]` count in every row
  checked, but no instruction proves it is a wrap sentinel rather than a ninth heading.
- The `[US]`, `[UN]`, `[DN]` and `[DS]` sections, plus the `D%s` and `U%s` prefixes at file
  `0x36d8d` and `0x36e63`, belong to a later phase of the walk loader that remains untraced.
- `11d8:849c` counts frames and `11d8:84a0` receives the per-frame step budget, but no
  instruction that decrements the budget turned up, so the number of LineDDA pixels a single
  walk frame covers remains unknown.
- `11d8:0127` is compared against 1, 2 and 3 in several places, but only 0 and 1 are ever
  stored. What 3 would mean is unknown.
- The three-pixel drift between the computed grid and the painted board could be rounding in
  the original rendering tool or slightly different authoring numbers. Nothing in the code
  explains it.
