# Screens, menus and settings

This note describes every screen `XCHESS.EXE` draws, every button on the menu
row, and every setting the game keeps on disk. It comes from reading the New
Executable resource table with `tools/extract/ne.py` and from decompiling
`original/win3x/cd/XCHESS.EXE` in Ghidra 12.1.3. Addresses use Ghidra's
`segment:offset` form. The data segment is segment 60, mapped at `11d8:0000`,
starting at file offset `0x33bc0`.

Read `docs/research/languages.md` first for the four resource DLLs, the 528
string ids and the two bitmap fonts. This note uses those ids without repeating
how they are stored.

## Where the code lives

| What it does | Address |
| --- | --- |
| Startup, class registration, message loop | `FUN_1008_3ef6` @ `1008:3ef6` |
| Main window procedure and its 32-entry message table | `WNDPROC` @ `1008:45d9`, table at `1008:5558` |
| Builds the menu buttons and the status bar | `SUB_1060_01be` @ `1060:01be` |
| Pushes and pops a menu page | `FUN_1060_040d` @ `1060:040d`, `FUN_1060_04e6` @ `1060:04e6` |
| Writes the hovered button's label into the status bar | `FUN_1060_00fe` @ `1060:00fe` |
| Command dispatch, 47-entry table at `1040:0917` | `FUN_1040_00dd` @ `1040:00dd` |
| Title window procedure and its 11-entry table at `1070:129d` | `TITLEWNDPROC` @ `1070:0daa` |
| Loads one title screen and starts its timer | `FUN_1070_030a` @ `1070:030a` |

## 1. What the executable's resources hold

`XCHESS.EXE` carries 166 resources: 155 bitmaps, five cursors and five cursor
groups (`XCUR`, `WCUR`, `BCUR`, `WCURX`, `BCURX`), one icon and one icon group
(`BOARD`), and one dialog (`USERENTRY`). It holds no resource of type 4, so
nothing here uses a Windows menu bar. Every control is a bitmap button drawn by
`CC.DLL`.

Of the 155 bitmaps, 153 are button faces measuring 41 by 25 pixels at 8 bits per
pixel, 2176 bytes each including a 1024-byte palette; only `BLANK_D` differs, at
41 by 26. The other two are `GRAYBRUSH`, an 8 by 8 pattern brush, and `BENGLOW`,
77 by 161.

A base name such as `GAME` gets one suffix per drawing state, so the file holds
`GAME_U` for the released button, `GAME_D` for the pressed one and `GAME_I` for
the disabled one. `CC.DLL` knows six suffixes, stored back to back at file offset
`0x1d8b` in that DLL: `_U`, `_D`, `_F`, `_I`, `_M` and `_H`. Only the first,
second and fourth exist in `XCHESS.EXE`. Nine names appear twice: `SWITCH_D`,
`SWITCH_U`, `SWITCH_I`, `SPLAY_D`, `SPLAY_I`, `SOUND_D`, `SOUND_U`, `SOUND_I`
and `RSTR_U`. `FindResource` returns the first match, so the second copy of each
pair never loads.

### The one dialog template

`USERENTRY` is 128 bytes at file offset `0x91f80`, a captioned modal-frame popup
that names its own font. Coordinates below are dialog units, not pixels.

| Element | Class | id | x, y | width, height | Text |
| --- | --- | --- | --- | --- | --- |
| The dialog itself | none | | 10, 35 | 156, 75 | `ChessMaster`, System 10pt |
| Prompt | STATIC | 501 | 5, 7 | 146, 20 | `Enter a number:` |
| Number field | EDIT | 502 | 12, 29 | 130, 12 | empty |
| Cancel | BUTTON | 2 | 96, 53 | 46, 13 | `Cancel` |
| OK | BUTTON | 1 | 12, 53 | 47, 12 | `OK` |

No resource DLL supplies any of those five strings, so the dialog stays in
English in all four languages.

Segment 1048 makes eight `DialogBox` calls naming eight templates:
`OpponentSelect`, `UserEntry` twice, `EditAnnotation`, `BoardColors`,
`SelectPlayers`, `SelectOpponentColor` and `GameCopy`. Only `UserEntry` matches a
resource in the file, and no DLL on the CD supplies the rest, so the other seven
calls fail and those dialogs never appear. The port can skip them. `CC.DLL` holds
one more dialog, id 100, captioned `Bit Button Style`, but its controls belong to
Borland Resource Workshop, so it is the design-time property sheet for the custom
control rather than a screen a player sees.

## 2. The screen flow from launch

`FUN_1008_3ef6` runs first. It registers the `TitleWindow` class through
`FUN_1070_12c9`, then calls `FUN_1008_3e10` at `1008:3fc7`, which runs
`CHESSAPP.EXE` with `WinExec` and waits for its window. That separate 16-bit
program shows the Software Toolworks logo from `TWORK256.DAT` and
`START256.DAT`. This note did not decompile it, so which of those two files it
draws when is unresolved.

`FUN_1008_3ef6` then checks for a 256-colour driver with `Color256Available`,
showing a message box with the string at `11d8:0663`
(`-- install your 256 color driver --`) when the check fails. It reads settings,
registers the main window class, creates the main window, and calls
`FUN_1070_01f8`, which loads `TITLERES.DLL`, pulls `LEGFONT` out of it, and
creates a `TitleWindow` at 0, 0 sized 674 by 512 pixels over a black stock brush.

The word at `11d8:93d8` says which screen is showing, and `FUN_1070_030a` loads
it. `WM_TIMER` at `1070:1093` adds 1000 to an elapsed counter, compares it
against the screen's duration, and on expiry increments `11d8:93d8` at
`1070:117e`. At 5 the handler posts `WM_SYSCOMMAND` with `SC_CLOSE` to the title
window and the sequence ends.

| State | Screen | Bitmap or strings | Font | Duration | Sound |
| --- | --- | --- | --- | --- | --- |
| 0 | Toolworks logo | `STLGO16`, 640 by 480, 8bpp | none | 3000 ms | none |
| 1 | Legal notice | `LEGAL`, 640 by 480, 8bpp | none | 3000 ms | none |
| 2 | Opening crawl | string ids 14001 upward | LEGFONT | 60000 ms | `STWPRES.WAV` then `SWTHEME.WAV` (the port moves `STWPRES.WAV` to state 0) |
| 3 | Title | `STARTITL`, 640 by 480, 8bpp | none | 5000 ms | none |
| 4 | Credit roll | string ids 15001 upward | LEGFONT | 140000 ms | `SWTHEME.WAV` |

The durations are 32-bit values in a table at `11d8:32e4`, indexed by state times
four. States 0, 1 and 3 call `SetTimer` with a 1000 ms period at `1070:03f1`, and
state 4 uses 500 ms. State 2 sets no timer; the private message 0x84c drives it
instead, and its handler at `1070:11f2` runs `FUN_1070_0866`.

The crawl and the credit roll share their layout code. `FUN_1070_030a` reads the
line count with `LoadString(14000)` for the crawl and `LoadString(15000)` for the
credits, then renders each following id into its own DIB through
`FUN_1070_0000`, which sums the LEGFONT advance widths, allocates a DIB that
wide, and copies each glyph cell out of the 640 by 253 LEGFONT sheet. It keeps
the widest line and stores `(640 - widest) / 2` at `11d8:922a` as one left margin
for the whole block, so the block is centred but the individual lines are not.
Total scroll distance is `42 * lines + 240` pixels at `11d8:93da`, where the
42-pixel line pitch is the LEGFONT cell height.

`FUN_1070_0866` scrolls with `ScrollWindow` one pixel at a time, pumping messages
between steps. A key press, a left click and a right click all reach `1070:0f82`,
which ends the current screen. `WM_DESTROY` at `1070:0e9c` plays `ENERGIZE.WAV`,
a WAVE resource inside `SWCAUDIO.DLL`, frees `TITLERES.DLL`, and sets
`11d8:93d8` to 4 so the next title window shows the credit roll. Then
`FUN_1008_4509` calls `RealizeGUI` in `CC.DLL`, moves the main window to the
rectangle the title window used, and shows it. The game screen appears there.

## 3. The game screen

The main window's `WM_CREATE` arm at `1008:4feb` builds everything. It captures
the mouse, sets up the board through `FUN_1028_0d1e` and `FUN_1028_0d58`, picks
the background bitmap through `FUN_1008_1311`, and at `1008:5144` calls
`SUB_1060_01be`.

`SUB_1060_01be` loops 13 pages by 6 slots. For each slot whose command id is not
-1 it calls `CreateWindow` at `1060:02cb` with class `BitButton` from
`11d8:301a`, window text taken from a 20-byte-per-slot name table at `11d8:2966`,
control id taken from a table of 78 words at `11d8:2f7e`, and geometry
x = `15 + 41 * slot`, y = 445, 41 by 25 pixels. So the six buttons of a page sit
in a single row from x = 15 to x = 261 at y = 445 to y = 470, and the page-switch
code redraws left 15, top 445, right 278, bottom 470. Every button gets
`WS_CHILD`; only page 0 also gets `WS_VISIBLE`, and `FUN_1060_0000` shows and
hides the rest.

Three control styles appear in the low bits. Most buttons take 0, a plain push
button. The four look-and-feel toggles in slots 0 to 3 of page 7 take 2, a check
button that keeps its pressed state. The three player-combination buttons on
page 6, the two side-to-move buttons on page 8 and the five difficulty buttons on
page 9 take 9, an auto radio button, so exactly one button of each group stays
pressed.

Right after creating a button, `SUB_1060_01be` sends it message 0x465 at
`1060:030f` carrying `page * 10 + slot`, the string id, which the button
remembers. Message 0x401 sets a toggle button's pressed state, and
`SUB_1060_01be` uses it from `1060:034d` onward to seed the four toggles, the
difficulty and the player combination from the saved settings. It finishes by
creating the status bar at `1060:03f0`: class `BorderStatic` from `11d8:3024`, no
text, x = 278, y = 453, 342 by 12 pixels, handle stored at `11d8:8484`.

`FUN_1060_00fe` fills that bar. The button under the mouse reports its remembered
id through message 0x466, `FUN_1060_061f` passes the id here, and the routine
loads that string from the active resource DLL and sends `WM_SETTEXT` to the bar.
For the toggle ids 70 to 73 it adds 6 when the matching flag is zero, so a
switched-off setting shows the id 76 to 79 wording offering to switch it on. It
saves the old text with `WM_GETTEXT` into `11d8:83aa` first, and
`FUN_1060_00cc` puts that text back when the mouse leaves.

The board's file and rank labels are the only text the game draws with a Windows
font. `FUN_1028_10eb` calls `GetTextMetrics`, then `TextOut` twice, once for the
file letter and once for the rank digit, at positions computed from the board
projection rather than from fixed coordinates.

This screen has no move list, no clock and no player-name area. The whole program
calls `CreateWindow` from two places and `TextOut` from one, so everything else
the player reads goes through that one `BorderStatic` bar. `FUN_1008_2368` pumps
messages from the chess engine, and a handler that leaves text in its buffer
calls `FUN_1008_03d0`, which sends `WM_SETTEXT` to the same bar. Check,
checkmate, draw and illegal-move notices all arrive that way.

## 4. The menu tree

Page times ten plus slot gives the string id. The command id is both the button's
Windows control id and the number `WM_COMMAND` carries. Commands 1000 to 1199
push a page: `FUN_1060_040d` subtracts 1000 and divides by ten to get the page
number. Command 1200 pops, through `FUN_1060_04e6`. The menu stack at
`11d8:83e0` holds at most three levels.

| Page | String id | English label | Bitmap base | Command | Handler |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | GAME MENU | GAME | 1010 | push page 1 |
| 0 | 1 | PLAY MENU | PLAY | 1020 | push page 2 |
| 0 | 2 | ACTIONS MENU | ACTION | 1030 | push page 3 |
| 0 | 3 | MENTOR MENU | MENTOR | 1040 | push page 4 |
| 0 | 4 | MINIMIZE | MIN | 3010 | `1040:03cc` |
| 0 | 5 | EXIT | BACK | 1100 | push page 10 |
| 1 | 10 | DEMO MODE | DEMO | 2000 | `1040:02d5` |
| 1 | 11 | NEW GAME | NGAME | 100 | `1040:07d4` |
| 1 | 12 | LOAD GAME | LGAME | 101 | `1040:0710` |
| 1 | 13 | SAVE GAME | SGAME | 103 | `1040:0757` |
| 1 | 14 | SETTINGS | SET | 1050 | push page 5 |
| 2 | 20 | SELECT PLAYERS | SPLAY | 1060 | push page 6 |
| 2 | 21 | LOOK & FEEL MENU | LKFL | 1070 | push page 7 |
| 2 | 22 | SETUP GAME | SETUP | 1080 | push page 8, plus command 263 |
| 2 | 23 | SHOW CAPTURED PIECES | CAPTUR | 324 | `1040:0232` |
| 3 | 30 | SWITCH SIDES | SWITCH | 200 | `1040:0447` |
| 3 | 31 | FORCE MOVE | FORCE | 201 | `1040:0470` |
| 3 | 32 | TAKE BACK MOVE | TBACK | 202 | `1040:0780` |
| 3 | 33 | RE-PLAY A MOVE | REPLAY | 205 | `1040:0788` |
| 3 | 34 | OFFER DRAW | DRAW | 209 | `1040:01ce` |
| 4 | 40 | HINT | HINT | 250 | `1040:06f9` |
| 4 | 41 | PLAY LEVEL | PLAYLV | 1090 | push page 9 |
| 5 | 50 | LOAD SETTINGS | LOAD | 113 | `1040:0823` |
| 5 | 51 | SAVE SETTINGS | SAVE | 112 | `1040:08da` |
| 5 | 52 | RESTORE SETTINGS | RSTR | 115 | `1040:0823` |
| 6 | 60 | HUMAN VS. COMPUTER | HC | 3000 | `1040:036a` |
| 6 | 61 | HUMAN VS. HUMAN | HH | 3001 | `1040:036a` |
| 6 | 62 | COMPUTER VS. COMPUTER | CC | 3002 | `1040:036a` |
| 7 | 70 | WHITE ON BOTTOM | WT | 296 | `1040:0790` |
| 7 | 71 | MUSIC OFF | SOUND | 157 | `1040:029c` |
| 7 | 72 | CAPTURES OFF | CAP | 158 | `1040:0254` |
| 7 | 73 | WALKING OFF | WALK | 159 | `1040:0278` |
| 7 | 74 | CHANGE BOARD | CHGBRD | 271 | `1040:035c` |
| 8 | 80 | CLEAR | CLEAR | 450 | `1040:06bc` |
| 8 | 81 | NEW | NEW | 451 | `1040:06c5` |
| 8 | 82 | DONE | DONE | 453 | `1040:0607` |
| 8 | 83 | WHITE TO MOVE | WTM | 454 | `1040:06e7` |
| 8 | 84 | BLACK TO MOVE | BTM | 455 | `1040:06f0` |
| 8 | 85 | BACK | BACK | 452 | `1040:0594` |
| 9 | 90 | NEWCOMER | NEWCMR | 460 | `1040:0248` |
| 9 | 91 | EASY | EASY | 461 | `1040:0248` |
| 9 | 92 | MODERATE | MODERT | 462 | `1040:0248` |
| 9 | 93 | HARD | HARD | 463 | `1040:0248` |
| 9 | 94 | EXPERT | EXPERT | 464 | `1040:0248` |
| 10 | 100 | QUIT OK | OK | 119 | `1040:03e5` |
| 10 | 101 | QUIT CANCEL | CANCEL | 1200 | pop |

Slot 5 of pages 1 to 7 and of page 9 is left out of the table because all eight
are the same: string id `page * 10 + 5` reading BACK, bitmap `BACK`, command
1200, popping the menu stack. Pages 11 and 12 exist in the tables, but every slot
holds -1 except two, so they make no buttons, and string ids 110 to 125 are empty
in all four languages.

The five toggles work alike. Each arm flips its flag with the
`NEG AX; SBB AX,AX; INC AX` idiom, sends message 0x401 to its own button with the
new state, and calls `FUN_1060_00fe` so the status line switches wording.
Command 157 flips `11d8:6889` (music) and silences the current sound when it
turns off, 158 flips `11d8:6887` (captures), and 159 flips `11d8:6885` (walking).
Command 271 flips `11d8:6883`, choosing between `thron256.bmp` and
`space256.bmp` as `board-geometry.md` describes. Command 296 flips `11d8:6841`,
the chess-set index that puts white at the bottom or the top, then calls
`FUN_1028_0789` to rebuild the board.

The five difficulty buttons share the arm at `1040:0248`, which stores the
command id itself, 460 through 464, into `11d8:688b` and calls `FUN_1048_0a13`.
It finds each button through the base `11d8:80bc`, the handle array at
`11d8:83e8` biased by twice 460 so the raw command id indexes it directly.

Command 119, the QUIT OK button, does not exit straight away. Its arm at
`1040:03e5` kills the game timer, hides the main window and creates a fresh
`TitleWindow`; `WM_DESTROY` left `11d8:93d8` at 4, so that window runs the credit
roll, and closing it posts `SC_CLOSE` to the main window. Command 2000, DEMO
MODE, reads `GetPrivateProfileInt("demo", "enabled", 0, CM.INI)` at `1040:02e7`;
a set key runs the cheat demo through `FUN_1008_588a`, which `capture-player.md`
describes, and otherwise the ordinary computer-versus-computer demo starts.

## 5. Settings

`wsprintf` builds the path from the format `%s\%s` at `11d8:048a`, the program
directory at `11d8:67e5` and the name `SWC.INI` at `11d8:00e7`, so the settings
file sits beside `XCHESS.EXE` rather than in the Windows directory.
`FUN_1008_37bc` reads it, once from `FUN_1008_3ef6` at `1008:40c6` and again
whenever the player picks LOAD SETTINGS or RESTORE SETTINGS. It first opens
`CMWIN.DAT` and reads exactly 144 bytes into `11d8:67ff`, a binary blob holding
the chess engine's own configuration that this note did not decode. Then it
reads seven integers.

| Key | Variable | Default | Shipped in `SWC.INI` | Meaning |
| --- | --- | --- | --- | --- |
| `language` | `11d8:688d` | 0 | 0 | index into RESENG, RESGER, RESFRN, RESSPN |
| `turn` | `11d8:6831` | 0 | 0 | board rotation in degrees |
| `walking` | `11d8:6885` | 0 | 1 | animate pieces walking between squares |
| `captures` | `11d8:6887` | 0 | 1 | play the capture animation |
| `sounds` | `11d8:6889` | 0 | 1 | play music and effects |
| `play_level` | `11d8:688b` | 464 | 464 | command id 460 to 464, so 464 is EXPERT |
| `board` | `11d8:6883` | 0 | absent | 0 draws `thron256.bmp`, non-zero draws `space256.bmp` |

All seven live under `[look_feel]`. The code reads `language` twice, first as
`GetPrivateProfileInt("language", "language", 99, "SWCMPC.INI")` and then, only
when that returns the sentinel 99, from `[look_feel] language` in `SWC.INI`.
`SWCMPC.INI` carries no path, so Windows looks in the Windows directory, where
the installer put it. `FUN_1008_37bc` ends by storing `turn % 360 != 0` into
`11d8:6841`; the shipped `turn=0` makes that 0, which selects the `WHTBTM_` chess
set and puts white at the bottom.

`FUN_1008_36c7` writes the settings back. It calls `FUN_1008_3683` eight times,
each call formatting the value with `wsprintf` before passing it to
`WritePrivateProfileString`. The first call writes `[language] language` into
`SWCMPC.INI`; the other seven write `language`, `turn`, `walking`, `captures`,
`sounds`, `play_level` and `board` into `[look_feel]` of `SWC.INI`, in that
order.

Only one place calls `FUN_1008_36c7`, the arm at `1040:08da` for command 112, the
SAVE SETTINGS button. Nothing writes the file on exit, so an unsaved change is
lost when the game quits.

LOAD SETTINGS and RESTORE SETTINGS do the same thing as each other in this build.
The arm at `1040:0823` computes 0 for command 113 and 1 for command 115 and passes it as an
argument, but `FUN_1008_37bc` never reads that argument; its prologue at
`1008:37bc` goes straight to opening `CMWIN.DAT`. The defaults file `CMWIND.DAT`
ships on the CD and its name sits at `11d8:0534`, yet nothing opens it. Both
buttons re-read the same saved values, and the arm then re-sends message 0x401 to
every toggle, difficulty and player button so the pressed states match the file.

## 6. The custom controls

`XCHESS.EXE` imports three functions from `CC` and nothing else: `InitCC` at
`1008:4499`, `RealizeGUI` at `1018:0010` and `1018:02bb`, and
`ChessDefWindowProc`, the fallback for both window procedures.

`CC.DLL` exports 20 entry points, among them `PAINTBUTTON` at ordinal 6,
`BMCOMBOWNDPROC` at ordinal 11 and `SPINWNDPROC` at ordinal 18. It registers ten
window classes, each superclassing a stock Windows class: `BitButton` over
BUTTON, `BorderStatic` over STATIC, `BorderEdit` and `SpinEdit` over EDIT,
`BitListBox` over LISTBOX, `BitComboBox` and `BitComboEdit` over COMBOBOX and
COMBO, `ChessScrollBar` over SCROLLBAR, plus `CWBitScroll` and `CWBitParent`.
Star Wars Chess uses only `BitButton`, for every menu button, and `BorderStatic`,
for the status bar.

`InitCC` loads its bitmaps from a companion DLL, building the name with the
format `cc%d.dll` at file offset `0x1d5c`, so a 256-colour display loads
`CC256.DLL` and a 16-colour one loads `CC16.DLL`. Both hold the same 26 bitmaps
at the same offsets, and the eleven the port never needs are the eight
`SB_*ARROW*` and `SB_*BAR*` scrollbar pieces, `SB_BUTTONUP`, and the five drive
icons `FLOPPYDRIVE`, `HARDDRIVE`, `NETDRIVE`, `CDROMDRIVE` and `RAMDRIVE`.

| Bitmap | Size | Drawn by |
| --- | --- | --- |
| `GUITEXT` | 1542 by 17 | every class, as the 96-cell text font |
| `BB_CHKBXUP`, `BB_CHKBXDN` | 25 by 29 | `BitButton` check boxes |
| `BB_RADIOUP`, `BB_RADIODN`, `BB_RADIOD` | 17 by 13 | `BitButton` radio buttons |
| `COMBO_U`, `COMBO_D` | 15 by 28 | `BitComboBox` drop-down arrow |

Only `GUITEXT` matters for the port. The menu buttons carry their own art from
`XCHESS.EXE`, so `BitButton` never draws the check-box or radio faces here, and
the check and radio styles only decide whether a button keeps its pressed state.
`CC.DLL` also never turns the window text into a visible label: the window text
is the bitmap base name, and `PAINTBUTTON` uses it to find `<name>_U`,
`<name>_D` or `<name>_I` in the calling module. The wording the player reads
comes from the resource DLL and shows only in the status bar. Where `GUITEXT`
gets its per-glyph advance widths stays unresolved, as `languages.md` records.

## 7. Check, mate, draw and victory

Every end-of-game notice is a string id the chess engine picks and the status bar
shows. `FUN_1148_031e` at `1148:031e` maps the engine's result word at
`11d8:3c78` onto an id.

| Condition | String id | English text |
| --- | --- | --- |
| No progress | 32773 | `no progress DRAW` |
| Third repetition | 32774 | `Third repetition DRAW` |
| Insufficient material | 32775 | `Not enough pieces for game` |
| Check | 32776 | `Check!` |
| Black mated, white to move | 32777 | `Black was mated!` |
| White mated | 32778 | `White was mated!` |
| Stalemate | 32779 | `Stalemate!` |

That routine also reaches ids 32781 to 32785, which hold no English text, so
those conditions leave the status line blank. Ids 2148 and 2149,
`Draw accepted` and `No draw - play on`, answer the OFFER DRAW button, and ids
33024 to 33076 explain refused moves in piece order, all through the same bar.

Checkmate also starts an animation. `FUN_1008_1745` fills the four-character
capture code at `11d8:012b` with the winner's colour, the winning piece's letter,
the loser's colour and `K` for the king, as `capture-player.md` describes. It
then plays `WHTVIC.WAV`, named at `11d8:0340`, when white wins, or `BLKVIC.WAV`
at `11d8:034b` when black wins, both through `FUN_1008_1519` with flag 1 so they
play asynchronously. Last it calls `FUN_1008_1694`, then `FUN_1008_56c4`, which
runs the capture player on the mating piece against the losing king.

`FUN_1008_1694` handles one special case, a white king delivering mate. It checks
that the attacker letters are `W` and `K`, draws an extra sprite offset 20 pixels
from the king, and plays `BEN2.WAV` followed by `LUKE.WAV`.

No `White_Victory` resource exists. `XCHESS.EXE`, `TITLERES.DLL`, `CC.DLL`,
`CC256.DLL` and the four resource DLLs hold no resource by that name and no
string containing `Victory`, so the two WAV files and the checkmate capture
animation are the whole victory presentation.

Promotion has no user interface here. No promotion dialog template exists, no
resource DLL holds a promotion string, and no menu slot offers one. The word
appears once, as `promote` at `11d8:5379`, inside the list of sound-event names
the `TWRXDDE` remote-control interface accepts, beside `capture`, `illegal`,
`check`, `checkmate` and `draw`. Which piece a promoting pawn becomes is
unresolved, because this note did not read the engine segments.

## 8. What stays unresolved

- Which files `CHESSAPP.EXE` draws before `XCHESS.EXE` takes over, and in what
  order. That program was not decompiled.
- The fields of the 144-byte `CMWIN.DAT` blob read into `11d8:67ff`.
- Why nine button bitmap names appear twice, and which copy the original tools
  meant to reach.
- What the ignored argument to `FUN_1008_37bc` was meant to select, given that
  `CMWIND.DAT` ships but nothing opens it.
- Which piece a promoting pawn becomes.
- Where `BENGLOW`, 77 by 161, is drawn. It loads by name at `1018:152e`.
- Pages 11 and 12 of the button tables, which carry command ids 3020 and 324 in
  slot 5 with no string and no bitmap name.
