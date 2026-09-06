# Review of the first plan (Claude Fable 5.1, xhigh)

**Verified**

- Twelve piece DLLs have zero NE segments. Each has `e_lfanew=0x400`, so the segment count word sits at `0x41c` and reads 0 in AT, BF, C3, CB, DV, EM, LO, LS, R2, SP, ST, YO.
- `CC.DLL` is a control library. It has 8 code segments, exports `INITCC` at `0x18e` and `PAINTBUTTON`, `SPINWNDPROC`, `BMCOMBOWNDPROC`, and its description "Custom Control Library" starts at `0x41d`. `CC16.DLL` and `CC256.DLL` have zero segments and hold 26 uncompressed control bitmaps each. None contains `treecn`, `iterct`, or `PLYMAX`.
- The search engine lives in `CHESSAPP.EXE`. It holds `treecn` at `0xb63d`, `ePersonalitySet` at `0xb675`, `treef: m[tply] > INF - 30`, `iterct >= (PLYMAX - 2)`, `CHESS.LIB Debug Message`, and `Freeing Hash memory`. `XCHESS.EXE` only holds the DDE item names plus "xSearchUpdate: move not in Kittinger's ply 1 list" at `0x38bd6`. CHESSAPP is one 41,250-byte code segment and one data segment importing only KERNEL, USER, GDI, TWRXDDE.
- ANX layout holds for all 72 files. Frame count at `0x00` (`BBWB.ANX` = 80), 450 offset slots, first `BITMAPINFOHEADER` at `0x70c` (112x171, 8 bpp, 256 colors), compression dword at `0x71c` = `0x00020001`.
- The three `BOOK.DAT`/`XBOOK.DAT` copies share SHA-256 `1ed09451...526e` and begin "Chessmaster 3000 Opening Book\r\nCopyright 1991".
- `AT.DLL` reads "AT-AT walking frames" at `0xd51`.
- `EXPERT.CMP` begins "Chessmaster" at `0x00`, and the five files carry names at `0x44`: Newcomer, Novice, Woodpusher, Kamikaze, Chessmaster.
- `SCHESS.EXE` reads "Microsoft Windows 3.x Setup Application" at `0x16ca`. `SWCAUDIO.DLL` has 110 `WAVE` resources, first RIFF at `0x1400`, mono 8-bit 22,050 Hz. `AT.INI:[W]` lists 16 frames with `count=15`. `XCHESS.EXE` imports no WinG module and blits with GDI `StretchDIBits` (GDI.439).

**ANX decoder confirmed**

The escape-byte RLE is correct across every record. Escape = high word of the compression dword (1, 2, or 3 per file), `escape, value, count` for runs, other bytes literal. All 4,799 ANX records and all 1,344 piece-DLL bitmaps decode to exactly `width*height` pixels with only zero padding left before the next 32-byte-aligned record. Rows are tight (no 4-byte DIB stride). `biSizeImage` holds the padded DIB size. Transparency key is palette index 0; entry 0 is the same teal `1b 63 73` in all 4,799 records. 624 of 5,423 timeline entries reuse an earlier record.

**Disagreements with the first plan**

1. Windows install media is not needed; the owner's image is a pre-installed drive. (Now moot: emulation is out of scope.)
2. Smooth 120 Hz presentation changes nothing during a capture. Offline interpolation should be a real milestone with an acceptance test.
3. `CHESSAPP.EXE` is the easiest decompilation target in the package; its DDE items name engine globals (`hashtb`, `sqarrays`, `ply1list`, `nodect`, `bestvariation`). (Now deferred: engines are out of scope for now.)
4. Start decompiling the capture player in `XCHESS.EXE`, not `CWDIB.DLL`. The bitmap format is done; what is unknown is how `frame_delay`, `pause`, x/y offsets, and wav cues are interpreted.
5. `CF_CHESS_MEM_BLOCK` and `CF_CHESS_BOARD` ship raw engine memory to the UI. Design the native engine interface from UCI, not from DDE.

**Gaps**

- The 3D board is three pre-rendered bitmaps, not a projection. `CM.INI` `[chesssets]` names `WHTBTM_`, `WHTTOP_` (414x286) and `FACING_` (640x480) as 3D sets and `2DSET_` as 2D, and `XCHESS.EXE` reads `vanishpt_3D`, `tilt_3D`, `turn_3D`, `size_3D`. The square-to-pixel mapping for piece placement must be recovered from those numbers and the walk INIs.
- The `[demo]` cheat in `CM.INI` (`attack`, `defend`, `attack_loop`, `loop_delay`) is implemented in `XCHESS.EXE` (strings at `0x3436a`).
- Saved games (`STARWARS.CMG`, `%d.CMG`, `.CMO` tournaments) have no format entry.
- Frame timing uses `timeGetTime` (MMSYSTEM.607) and `sndPlaySound` (MMSYSTEM.2).
