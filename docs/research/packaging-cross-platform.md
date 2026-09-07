# Windows and Linux packaging plan

The game appears portable to Windows 10/11, Ubuntu 24.04, and Fedora 44, but none of those builds has been verified here.
The source audit found no confirmed compiler error in the `swchess` target.
The repository currently produces an application bundle only for macOS.
Windows and Linux need dependency setup, portable startup and file handling, and installation rules.

The main inventory records committed source at `bc87237` on September 7, 2026.
Its line references use that commit so concurrent edits cannot silently change what they identify.
It includes `README.md`, the top-level CMake file, every `src/*/CMakeLists.txt`, and all source files in the requested packaging and tool directories.
It also checks `tests/CMakeLists.txt` and the RIFE build script because they affect clean builds.
The engine section below records additional working-tree files that appeared during the audit.
This work changes only this plan.

## Data and runtime requirements

Players must supply their own CD folder.
The copyrighted files under `original/` remain gitignored and must never enter an application package or CI upload.
The decoded cache remains local to the player too.
Allow for the supplied estimate of about 900 MB, plus temporary interpolation files and space for a replacement cache during regeneration.

The Python extractor writes the decoded cache through `tools/extract/__main__.py:95`.
It uses the standard library and also imports the decoder in `tools/reference/anx.py`.
The interpolation tool in `tools/interp/pipeline.py:187` calls `rife-ncnn-vulkan` to generate additional frames.
RIFE needs its model and a working Vulkan implementation on the player's machine.

The game itself does not link Python, RIFE, or Vulkan.
It reads the CD directly in `src/game/session.cpp:132`.
It optionally loads generated frames in `src/game/session.cpp:232` and `src/anim/interp.cpp:110`.
Missing or unreadable interpolation files already fall back to the original animation cadence in `src/game/session.cpp:246`.
Keep that behavior when packaging the game.

The launcher currently runs extraction only, at `packaging/launcher.sh.in:101`.
It never runs interpolation.
Installing Python and running extraction therefore does not produce the enhanced animations by itself.

## Linux probe result

The Docker client exists at `/opt/homebrew/bin/docker`.
The availability check used the requested Colima socket.

```sh
DOCKER_HOST=unix:///Users/segrob/.colima/default/docker.sock \
  docker info --format '{{json .}}'
```

The command exited with status 1 and emitted this exact diagnostic.

```text
permission denied while trying to connect to the docker API at unix:///Users/segrob/.colima/default/docker.sock
```

This session cannot access that socket under its filesystem restrictions.
The check did not establish whether the daemon is running.
No container started, no SDL build ran, and no Linux compiler or linker diagnostic was produced.
There were zero source fixes and zero build retries.
Linux compilation remains unverified.

The following sequence describes the deferred probe.
These commands were not executed.
Run an `ubuntu:24.04` container with the repository mounted read-only at `/src`.
Keep dependency installations and build output inside the disposable container.
Use `linux/arm64` for the native Colima probe on this Mac.
Use the x64 CI job below to verify the intended Linux download architecture.

Ubuntu 24.04 does not list an SDL3 development package in its official archive.
Its SDL3 source package listing starts with later Ubuntu releases.
Build SDL3 from a pinned release there. [Ubuntu SDL3 package history](https://launchpad.net/ubuntu/%2Bsource/libsdl3)

Use SDL 3.4.16 for this probe to match the headers installed on this Mac.
That version has an upstream release. [SDL 3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)

```sh
set -eu
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config curl ca-certificates python3 \
  nlohmann-json3-dev zlib1g-dev libasound2-dev libpulse-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev libwayland-dev wayland-protocols \
  libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
  libudev-dev libdbus-1-dev libdecor-0-dev
curl -fL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz \
  -o /tmp/SDL3.tar.gz
tar -xzf /tmp/SDL3.tar.gz -C /tmp
cmake -S /tmp/SDL3-3.4.16 -B /tmp/sdl-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF
cmake --build /tmp/sdl-build --parallel 4
cmake --install /tmp/sdl-build
cmake -S /src -B /tmp/swchess-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/sdl3 \
  -DBUILD_TESTING=OFF
cmake --build /tmp/swchess-build --target swchess --parallel 4
```

The SDL dependency list enables desktop video and audio support. [SDL Linux build instructions](https://wiki.libsdl.org/SDL3/README-linux)
The probe must retain the SDL configure summary, compiler versions, and complete build output.
Permit at most one change to fix an observed build error, then report any remaining errors exactly.
A successful compile would not verify dialogs, rendering, audio, installation, or Vulkan inference.

## C++ portability inventory

The search covered C++ sources and headers under `src/`, including test programs.
It checked platform headers, preprocessor branches, process calls, file operations, numeric casts, packing directives, and SDL calls.
The locations below refer to the audited source lines.

### Platform calls and entry points

| Locations | Finding and required work |
| --- | --- |
| `src/app/game_main.cpp:251,253,256,265,270` | `askForPath()` runs `osascript` through `popen()` only on Apple. Other platforms return an empty filename. Replace this function with SDL open/save dialogs. |
| `src/text/text_test.cpp:76,80,81,90,188` | The font test builds a shell command with `2>/dev/null` and unguarded `popen()`/`pclose()`. Windows command processing differs. Move the Python comparison into a test driver that passes an argument array. |
| `src/app/game_main.cpp:28,389` and `src/app/viewer.cpp:15,554` | Both programs include `SDL.h` and define ordinary `main()`. Neither includes `SDL_main.h`. Add SDL's entry-point header when building a Windows GUI executable. |
| `src/app/game_main.cpp:83,159` and `src/app/viewer.cpp:84,163` | Both parsers require `--cd`. They do not load the launcher's saved CD selection or its environment variables. Add a shared startup configuration reader. |
| `src/game/shell.cpp:117` | `defaultConfigDir()` uses `HOME` and a macOS `Library` directory on every platform. Without `HOME`, it returns the working directory. Replace this policy with the locations below. |
| `src/game/shell.h:83` and `src/game/shell.cpp:541,563,572` | `chooseFile` returns a filename synchronously. SDL dialogs return through callbacks. Change the shell to request a dialog and accept its result later. |

The include search found no POSIX-only headers such as `unistd.h`, `dirent.h`, `sys/mman.h`, or `pthread.h`.
It found no direct Cocoa, Metal, Win32, or X11 calls in game C++.
The only platform conditional in that C++ is `src/app/game_main.cpp:251`.

The process calls above are portability concerns, not observed compiler failures.
Microsoft documents `_popen()` for console applications and warns against using it in a Windows GUI application.
Compatibility spellings may let the existing test compile, but they do not repair its shell command. [Microsoft process-pipe documentation](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/popen-wpopen?view=msvc-170)

The current CMake executable targets are console targets on Windows.
Missing `SDL_main.h` therefore does not prove that today's `swchess` target will fail to link.
A future `WIN32_EXECUTABLE` target needs the Windows startup wrapper or an equivalent entry point. [SDL entry-point guidance](https://github.com/libsdl-org/SDL/blob/main/docs/README-main-functions.md)

### Filenames and directories

| Locations | Finding and required work |
| --- | --- |
| `src/assets/anx.cpp:29`, `src/assets/ne.cpp:60`, `src/anim/capture.cpp:36`, `src/anim/png_read.cpp:154`, `src/board/geometry.cpp:71` | These readers pass narrow strings to `fopen()`. UTF-8 folder selections can fail on Windows when the active code page cannot represent them. Use native filesystem paths for streams or convert UTF-8 before opening files. |
| `src/render/compositor.cpp:44` and `src/ui/settings.cpp:63` | PPM output and settings use the same narrow `fopen()` interface. Apply the same filename conversion when writing. |
| `src/anim/interp.cpp:59,120` and `src/game/shell.cpp:727,793,816` | Manifest and save streams receive `std::string` filenames. Pass a `std::filesystem::path` constructed through an explicit UTF-8 conversion instead. |
| `src/game/shell.cpp:132,139,276,714,716,776,778` and `src/audio/wav_load.cpp:107,108` | The game uses standard filesystem existence checks, directory creation, and file streams. These operations are portable. The narrow-string construction of their input paths still needs the Windows encoding policy. |
| `src/text/font.cpp:32`, `src/text/strings.cpp:14`, `src/game/shell.cpp:49`, `src/ui/title.cpp:16`, `src/ui/buttons.cpp:118` | The repeated `joinPath()` helpers recognize only a trailing `/`. Replace them with filesystem joining to handle drive roots, trailing backslashes, and network shares consistently. |
| `src/assets/piece_dll.cpp:49`, `src/assets/sheet.cpp:52,75`, `src/assets/wav.cpp:91`, `src/anim/capture.cpp:164,166,170`, `src/anim/walk.cpp:107,120,121,123` | These functions concatenate `/` into CD filenames. They need the same joining policy. |
| `src/board/geometry.cpp:83`, `src/board/board_view.cpp:28`, `src/game/session.cpp:162`, `src/app/viewer.cpp:176`, `src/app/review.cpp:91` | Board and background readers also concatenate `/`. Apply the shared filename handling. |
| `src/anim/interp.cpp:68,112,113,215` and `src/app/viewer.cpp:65` | Interpolation filenames depend on a supplied asset directory. The viewer defaults to `assets` relative to its working directory. Resolve installed resources and user cache directories independently of the working directory. |
| `src/assets/anx.cpp:34`, `src/assets/ne.cpp:65`, `src/anim/capture.cpp:41` | File sizing uses `long` and `ftell()`. Windows uses a 32-bit `long` even in x64 programs. Use stream sizing or a 64-bit file API if individual files can exceed 2 GiB. The combined 900 MB cache does not itself trigger this limit. |
| `src/assets/ini.cpp:137` | `iniAsInt()` parses through `long` before returning `int`. Values outside the supported integer range can behave differently between Windows and Unix. Check the intended range explicitly. |
| `src/text/text_test.cpp:28,49`, `src/ui/ui_test.cpp:30,38,301`, `src/board/board_test.cpp:69` | Test helpers repeat the string joining and narrow file operations. Include them in filename coverage. |
| `src/anim/anim_test.cpp:378,383,385,386,390,392,408,411,428` | The animation test uses a fixed temporary directory and creates hard links to cached frames. Links can fail across volumes or on unsupported filesystems. Use a unique temporary directory and copy when linking fails. Avoid `.string()` conversions that lose Unicode. |
| `src/audio/audio_test.cpp:16,69,75,76,243` and `src/game/game_test.cpp:364,454,484,508,509,579,581` | These tests use standard filesystem operations. Keep the operations, but make their asset inputs configurable and use unique output directories. |

Forward slashes alone are not a Windows build blocker.
Microsoft's `fopen()` accepts both separator styles.
Its narrow filename argument uses a Windows code page by default. [Microsoft filename handling](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-wfopen?view=msvc-170)

Keep decoder libraries independent of SDL by using filesystem paths and streams there.
SDL's file API is another option for application code because it accepts UTF-8 filenames on every platform. [SDL file opening](https://wiki.libsdl.org/SDL3/SDL_IOFromFile)

### Case-sensitive CD lookup

| Locations | Filename assumption |
| --- | --- |
| `src/assets/sheet.cpp:48,52,75` | Sheet loading expects uppercase `CM.INI` and names such as `WHTBTM_P.BMP`. |
| `src/assets/piece_dll.cpp:48,49` and `src/anim/walk.cpp:120,121,123` | Piece loading expects uppercase `.DLL` and `.INI` names. |
| `src/assets/wav.cpp:34,91` and `src/game/shell.cpp:276` | Sound loading expects `SWCAUDIO.DLL` and uppercase loose WAV filenames. |
| `src/anim/capture.cpp:164,166,170` | Capture loading constructs `.INI` and `.ANX` names from the supplied capture identifier. A lowercase `--capture bbwb` can fail against uppercase CD files on Linux. |
| `src/board/geometry.cpp:83` and `src/game/shell.cpp:138` | Board and settings loading expect `CMWIN.DAT` and `SWC.INI`. |
| `src/board/board_view.cpp:27,28`, `src/game/session.cpp:161,162`, `src/app/viewer.cpp:176`, `src/app/review.cpp:91` | Background loading expects the selected stem followed by uppercase `.BMP`. |
| `src/text/font.cpp:227,230,260` | Font loading expects `TITLERES.DLL`, `XCHESS.EXE`, and `CC256.DLL`. |
| `src/text/strings.cpp:29,31,33,35,77` | Language loading expects `RESENG.DLL`, `RESFRN.DLL`, `RESGER.DLL`, and `RESSPN.DLL`. |
| `src/ui/title.cpp:85` and `src/ui/buttons.cpp:161` | Title and button loading expect `TITLERES.DLL` and `XCHESS.EXE`. |
| `src/anim/interp.cpp:112` | Cache lookup uses the capture identifier verbatim in `captures/<name>/interp60`. Normalize identifiers to the extractor's uppercase convention. |

The uppercase names match an unchanged CD copy.
Linux case sensitivity does not make those names wrong.
The gap concerns lowercase or mixed-case copies, which the launcher partially accepts at `packaging/launcher.sh.in:40`.
It checks three spellings of `XCHESS.EXE` and does not validate the remaining files.

Create one CD-directory index that matches ASCII filenames without case sensitivity.
Reject ambiguous duplicate names instead of choosing one arbitrarily.
Use that index in both the C++ readers and Python extractor.
Do not rename anything on the player's CD.

The extractor currently has the same uppercase assumptions at `tools/extract/__main__.py:68,82,100,213,261` and `tools/extract/captures.py:187`.
INI section and key matching already ignores case in `src/assets/ini.cpp` and `tools/extract/ini.py`.
That behavior does not change filesystem lookup.
Preserve literal sound resource names, including the intentionally silent `R2ALARM\.WAV` case in `tools/extract/cues.py`.
That backslash belongs to a resource identifier, not a directory separator to repair.

### Numeric representation and SDL behavior

| Locations | Finding |
| --- | --- |
| `src/assets/anx.cpp:14`, `src/assets/bmp.cpp:10,17`, `src/assets/ne.cpp:9,16`, `src/assets/wav.cpp:11,18` | The original-file decoders assemble little-endian integers from bytes. They do not reinterpret file buffers as native structs. |
| `src/anim/capture.cpp:56`, `src/board/geometry.cpp:19`, `src/text/font.cpp:42,52`, `src/ui/res_bitmap.cpp:11,18`, `src/save/cmg.cpp:53,58` | Position, font, bitmap, and save readers also decode explicit byte order. The save writer emits individual bytes. |
| `src/anim/png_read.cpp:12` and `src/audio/wav_load.cpp:12,17` | PNG headers use explicit big-endian reads. WAV headers use explicit little-endian reads. |
| `src/assets/ini.cpp:69`, `src/assets/ne.cpp:44`, `src/text/strings.cpp:106`, `src/save/cmg.cpp:282`, `src/anim/png_read.cpp:69`, `src/audio/wav_load.cpp:114`, `src/game/shell.cpp:798` | These casts expose byte buffers through character or byte pointers. None performs an unaligned numeric load. |
| `src/game/session.cpp:142,144`, `src/app/viewer.cpp:323,325`, `src/app/review.cpp:298,300`, `src/audio/wav_load.cpp:95` | Audio copies bytes and explicitly declares `SDL_AUDIO_S16LE`. It does not assume native sample byte order. |
| `src/app/game_main.cpp:240`, `src/app/viewer.cpp:332`, `src/app/review.cpp:309` | Textures use `SDL_PIXELFORMAT_RGBA32` with RGBA byte arrays. Keep this byte-order-aware SDL format. |
| `src/app/game_main.cpp:291,295,302,309,333,338,343`, `src/app/viewer.cpp:351,355,362,369`, `src/app/review.cpp:458,463,470,477` | The application uses SDL's default renderer and portable window/event APIs. Verify scaling, mouse coordinates, and drawing on Windows, X11, and Wayland. No renderer is forced to Metal. |
| `src/audio/audio.cpp:37,50,68,92,123,151` | The mixer lets SDL choose the audio device and falls back to the dummy driver. Its callback already uses `SDLCALL`. Dummy audio verifies timing but cannot prove that speakers work. |
| `src/anim/interp.cpp:190,196,200` and `src/game/session.cpp:240` | Loading uses standard C++ threads and futures. CMake does not explicitly link `Threads::Threads`. Declare that dependency for portable compiler and linker flags. |

The search found no `#pragma pack`, compiler-specific assembly, or pointer casts that read packed numeric fields.
The headers use `#pragma once`, which the proposed compilers support.
Windows x64, Linux x64, and the current macOS arm64 target need no byte-order rewrite.
Retain binary file modes when changing file handling.

## CMake and packaging inventory

| Locations | Finding and required work |
| --- | --- |
| `CMakeLists.txt:1,4,17,18`, `src/anim/CMakeLists.txt:34,35`, `src/save/CMakeLists.txt:24` | Builds need CMake 3.28, C++20, SDL3, nlohmann-json, zlib, and Python 3.10 or newer. Only Homebrew dependency setup is documented. Python is required even with `BUILD_TESTING=OFF`. |
| `CMakeLists.txt:31,39,44,76,115` | Compiler warnings use `-Wall -Wextra` unconditionally. Select `/W4` for MSVC-style drivers and the existing flags for GCC-style drivers. MSVC can ignore an unknown option with a warning, so these flags alone do not prove a failed build. |
| `src/anim/CMakeLists.txt:50,64,68,75`, `src/audio/CMakeLists.txt:34,40`, `src/board/CMakeLists.txt:37,43,58,62` | These modules repeat the same unconditional warning flags. Apply the shared compiler policy to standalone builds too. |
| `src/game/CMakeLists.txt:41,45`, `src/save/CMakeLists.txt:38,42`, `src/text/CMakeLists.txt:36,49,53`, `src/ui/CMakeLists.txt:44,48` | These modules also repeat the warning flags. The chess module already uses standard C++20 target features. |
| `src/anim/CMakeLists.txt:66,73`, `src/audio/CMakeLists.txt:36`, `src/board/CMakeLists.txt:60`, `src/chess/CMakeLists.txt:17` | Test executables are created regardless of `BUILD_TESTING`. Guard their creation while retaining standalone test support. |
| `src/game/CMakeLists.txt:43`, `src/save/CMakeLists.txt:40`, `src/text/CMakeLists.txt:51`, `src/ui/CMakeLists.txt:46` | The remaining modules also create tests unconditionally. Turning off only the top-level test directory does not remove them from an all-target build. |
| `tests/CMakeLists.txt:20,66` | The default build generates reference outputs from private CD files through `ALL` targets. A fresh checkout cannot complete that work. Make CD-dependent tests opt-in. Building only `swchess` avoids these generators. |
| `tests/CMakeLists.txt:100` and `src/audio/CMakeLists.txt:42` | Timeline comparison hardcodes `python3` and source-tree data directories. Audio tests also assume source-tree assets. Use `Python3_EXECUTABLE` and explicit CD/cache options consistently. |
| `CMakeLists.txt:85` | All bundle logic sits inside `if(APPLE)`. Other platforms get executable targets but no installer, desktop entry, or runtime-library installation rules. |
| `CMakeLists.txt:94,98,107,109,113,116` | The Apple block records the source directory, builds an icon, configures a shell launcher, and creates a second game executable inside a bundle. Replace only the launcher's responsibilities with common startup code. |
| `CMakeLists.txt:124,127,129,132,133,134,141,144,146,147,150` | The Apple post-build steps copy the launcher and viewer, run `chmod`, and invoke the dylib bundler. Keep Apple-specific binary handling conditional. |
| `cmake/BundleDylibs.cmake:24,31,44,68,69,75,83,86` | Dependency bundling uses `otool`, `install_name_tool`, `chmod`, macOS system directories, and `codesign`. Windows needs DLL installation. Linux needs ELF shared-library installation and relative runtime search directories. |
| `packaging/launcher.sh.in:17,22,32,67,93,96,101` | Startup assumes `~/Library`, `osascript`, a source checkout, and `python3` on `PATH`. The configured checkout directory will not exist on another player's machine. |
| `packaging/launcher.sh.in:48` and `tools/extract/__main__.py:191,303,304` | Cache acceptance checks only for `catalog.json`. The extractor writes that file before returning failure for wrong counts. Require a successful extraction result and validate the catalog before reusing a cache. |
| `packaging/Info.plist.in:6,26`, `packaging/make-icns.sh:20,22,57` | The plist launches the script and declares macOS 15. Icon generation uses Apple utilities. Release icons must use independently authored artwork. |
| `CMakeLists.txt:99` and `packaging/make-icns.sh:6,20` | A populated local cache makes the icon builder read decoded CD artwork. Release staging must select the independent icon explicitly so local assets cannot enter the distributed icon. |
| `scripts/build-app.sh:13,18,21` and `scripts/check-fresh-checkout.sh:18,26,47,56,69` | The scripts assume an arm64 `.app`, Homebrew, `/tmp`, `otool`, and local private data. Use CMake presets and CI commands for platform builds. Keep the existing private-data check as a local integration check. |
| `Brewfile:2,3,4,5` | Homebrew installs CMake, Ninja, SDL3, and nlohmann-json. It does not define equivalent Windows/Linux dependencies or package Python and RIFE for players. |

Add explicit `install()` rules with relative destinations for executables, helpers, models, and permitted runtime libraries.
Use CMake's runtime dependency support where appropriate.
Libraries loaded dynamically by SDL or Vulkan still need separate packaging checks. [CMake installation rules](https://cmake.org/cmake/help/latest/command/install.html)

### Concurrent engine work

Other work added engine integration while this plan was being written.
The new files were read separately and were not changed by this audit.
The following references describe those uncommitted files as observed during the final check.

| Locations | Effect on this plan |
| --- | --- |
| `src/engine/CMakeLists.txt:40,46,51,53` | The random engine adds another library and test executable. Its warning flags and unconditional test creation need the same CMake changes listed above. |
| `src/engine/CMakeLists.txt:48,49` | The engine already declares `Threads::Threads`. That dependency can reach the game through engine linking. The standalone animation library still needs to declare its own thread dependency. |
| `src/engine/random_engine.cpp:9,10,11,18,90` | The implementation uses standard C++ clocks and random-number generation. It names uppercase `.CMP` files but does not read them yet. Add those names to CD lookup coverage when the original engine begins reading them. |
| `src/engine/engine.h:29,56` | The proposed original engine expects `BOOK.DAT`, `XBOOK.DAT`, and level files from the CD. Include its eventual readers in the portability audit before claiming support for that engine. |

The observed additions introduced no POSIX-only headers, packed structs, or unaligned numeric reads.
Repeat clean builds against the completed engine integration because it changes the game target's dependencies.

## Windows build and installer

Target Windows 10 and Windows 11 on x64.
Keep Windows arm64 and 32-bit builds outside the initial support claim.
Use MSVC with Ninja as the maintained Windows configuration.

| Compiler | Evaluation |
| --- | --- |
| MSVC | Visual Studio Build Tools provide the compiler, Windows SDK, and runtime libraries. Use a developer command environment with Ninja. Set C++20, `/W4`, `/utf-8`, and a consistent C runtime choice. |
| clang-cl | Clang's MSVC-compatible driver uses Visual Studio headers and libraries. It can check the same Windows code with different diagnostics. Keep its dependencies compatible with the MSVC build. [Clang documentation](https://clang.llvm.org/docs/UsersManual.html#clang-cl) |
| MinGW-w64 | SDL supports MinGW/MSYS builds. Use one MSYS2 UCRT64 compiler and dependency set throughout. Do not reuse MSVC-built C++ libraries or assume import libraries are interchangeable. Account for any GCC runtime DLLs in the package. [SDL Windows instructions](https://wiki.libsdl.org/SDL3/README-windows) |

| SDL3 source | Evaluation |
| --- | --- |
| vcpkg | Recommend a project manifest for `sdl3`, `nlohmann-json`, and `zlib`, with a pinned baseline and `x64-windows` triplet. Use its CMake toolchain file during configure. Python remains a separate build tool and packaged runtime decision. [SDL3 port](https://vcpkg.io/en/package/sdl3.html), [vcpkg manifests](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode) |
| FetchContent | A pinned SDL source archive can provide the same target on all platforms. It adds a dependency build to configure/build work. Also pin JSON and zlib if this configuration owns those dependencies. Make SDL discovery conditional when FetchContent already defines `SDL3::SDL3`. [SDL CMake instructions](https://wiki.libsdl.org/SDL3/README-cmake) |
| Prebuilt SDL3 | Official development archives can avoid compiling SDL. Match architecture and compiler format, point CMake at the package, and install `SDL3.dll` beside the game. JSON and zlib still need their own setup. [SDL Windows instructions](https://wiki.libsdl.org/SDL3/README-windows) |

Each build preset should choose one dependency source explicitly.
Require SDL3 3.2.0 or newer for the dialog and process APIs.
Pin the selected release or vcpkg baseline so a later dependency update cannot silently change a release build.
For a shared MSVC build, install SDL3 and zlib DLLs plus the required Microsoft runtime.
Verify the installer on a machine without Visual Studio, vcpkg, Python, or repository files.

| Installer | Evaluation |
| --- | --- |
| Inno Setup | Recommend a per-user installer with shortcuts, uninstall support, and signed release output. It can install the game and its private helpers without a separate system Python installation. [Inno Setup capabilities](https://jrsoftware.org/isinfo.php) |
| WiX | WiX produces MSI packages and suits deployments that require Windows Installer management. It requires more package authoring for this small desktop app. Choose it if MSI distribution is a requirement. [WiX build tools](https://docs.firegiant.com/wix/tools/wixexe/) |
| MSIX | MSIX supports Windows 10/11 desktop packages. Direct distribution requires package signing and certificate trust. Package identity, filesystem behavior, and Windows 10 feature differences need validation with the Python/RIFE helpers. Choose it if Store or MSIX distribution is required. [Microsoft packaging](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/packaging/), [Windows version differences](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/msix-windows10-windows11) |

Keep settings under `%APPDATA%\FairLine\Star Wars Chess`.
Keep generated assets under `%LOCALAPPDATA%\FairLine\Star Wars Chess\cache` so the large cache does not roam.
Keep installed executables separate from both locations.
Preserve user saves and CD files during uninstall.

## Linux build and package

Support Ubuntu 24.04 and Fedora 44 on x64.
Fedora 44 is the current Workstation release at the audit date.
Fedora provides `SDL3-devel`, including CMake package files. [Fedora Workstation](https://www.fedoraproject.org/workstation/download/), [Fedora SDL3 development package](https://packages.fedoraproject.org/pkgs/SDL3/SDL3-devel/index.html)

Use GCC and Ninja for the required Linux builds.
Ubuntu needs the SDL source build described above.
Fedora can use `gcc-c++`, `cmake`, `ninja-build`, `SDL3-devel`, `json-devel`, `zlib-devel`, and `python3`.
Record package versions in CI output.

| Package | Evaluation |
| --- | --- |
| AppImage | Recommend this format for a direct download that serves both distributions. Bundle SDL3, Python, and any optional RIFE helper dependencies. Keep the CD and generated cache outside the image. Build against the oldest supported system, Ubuntu 24.04, and check the same artifact on Fedora. [AppImage concepts](https://docs.appimage.org/introduction/concepts.html) |
| Flatpak | Flatpak provides a controlled runtime and desktop installation across distributions. Its sandbox adds work for selected CD-folder access, Python/RIFE helpers, and GPU access. Package helpers inside the application and use the file chooser portal. Verify that folder access still works after restarting. [Flatpak permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html), [File chooser portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html) |
| `.deb` | A Debian package integrates with Ubuntu's package manager and can depend on system Python and libraries. Ubuntu 24.04 still needs an SDL3 distribution strategy. A `.deb` does not cover Fedora, which would need an RPM or one of the formats above. [Debian dependency policy](https://www.debian.org/doc/debian-policy/ch-binary.html) |

AppImage needs compatible host graphics drivers and system libraries.
Do not bundle the host's glibc or GPU driver as if either were a portable app dependency.
Set runtime search directories relative to the installed executable and inspect the packaged ELF dependencies.
Test extracted execution as well as normal AppImage execution.
Ubuntu 24.04 names the FUSE 2 compatibility package `libfuse2t64`. [AppImage FUSE guidance](https://docs.appimage.org/user-guide/troubleshooting/fuse.html)

Install a `.desktop` entry and an independently authored icon.
Launch the game directly from that entry.
An AppImage may need a minimal `AppRun` entry to locate its executable and libraries.
That entry should not repeat CD selection, configuration, or extraction logic.

Use `$XDG_CONFIG_HOME/swchess`, defaulting to `~/.config/swchess`, for configuration and `SWC.INI`.
Use `$XDG_CACHE_HOME/swchess`, defaulting to `~/.cache/swchess`, for generated assets.
Use `$XDG_DATA_HOME/swchess`, defaulting to `~/.local/share/swchess`, for default saves.
Ignore relative XDG directory values as the specification requires. [XDG directory specification](https://specifications.freedesktop.org/basedir/0.8/)

## Shared startup and dialogs

Fold launcher logic into `swchess` and share the configuration code with `swchess-viewer`.
This replaces the existing macOS shell launcher and avoids writing Windows and Linux setup scripts.
A separate C++ launcher could reuse SDL's dialogs, but it would still need to pass configuration and manage the game process.
The game already has SDL and can perform setup before constructing `GameShell`.

Implement startup without requiring CD artwork for the setup screen.
The current `GameShell` constructor immediately reads game data at `src/game/shell.cpp:186`.
Create the SDL window and show setup before calling that constructor.

1. Parse command-line options before choosing directories. Preserve `--cd`, `--assets`, `--config`, and headless commands. Apply command-line values before environment overrides, saved configuration, and platform defaults. Read `SWCHESS_CD` and `SWCHESS_ASSETS` directly instead of relying on shell exports.
2. Read a versioned UTF-8 configuration file. Import the existing macOS two-line `config` file without changing its saved CD directory. Keep the current macOS support directory available during migration.
3. Ask for the CD folder with `SDL_ShowOpenFolderDialog` on the main thread. Copy the selected filename and any error during the callback. Deliver the result to the event loop before changing game state. Distinguish cancellation from an error. Keep processing events while waiting. Keep callback state alive until the callback returns. [SDL folder dialogs](https://wiki.libsdl.org/SDL3/SDL_ShowOpenFolderDialog)
4. Validate the CD through the shared filename index. Check the files that the game and extractor actually read, including `CC256.DLL` and `CMWIN.DAT`. Report missing files without modifying the CD. Reopen selection if a previously saved folder has moved.
5. Offer extraction into the selected writable cache. Run the packaged Python interpreter with argument arrays through SDL's process API. Drain its output while the UI remains responsive. Capture its exit code and show errors in the setup screen. [SDL process creation](https://wiki.libsdl.org/SDL3/SDL_CreateProcess), [SDL process properties](https://wiki.libsdl.org/SDL3/SDL_CreateProcessWithProperties)
6. Write extraction output into a temporary sibling directory. Validate the catalog, counts, and expected files after the process succeeds. Publish the completed directory and update configuration only after validation. Prevent simultaneous extraction into the same destination. Preserve an existing usable cache if extraction fails.
7. Offer enhanced animations separately from ordinary play. Check the RIFE executable, model, and Vulkan device before generating frames. Let cancellation or missing GPU support return the player to original-cadence play. Report progress per capture without blocking the event loop.
8. Replace save/load `osascript` calls with SDL open/save dialogs. Let `GameShell` request a dialog and accept its later result. Do not block the SDL event loop while waiting for a callback.

Headless commands must never open a dialog.
They should report missing required inputs and return a failure code.
Allow setup cancellation to close normally, and make `--help` return success.

SDL's preference-directory helper returns Roaming AppData on Windows and a data directory on Linux.
It does not implement the separate XDG configuration and cache policy above. [SDL preference directories](https://wiki.libsdl.org/SDL3/SDL_GetPrefPath)
Use a small platform directory function for those distinctions.
Resolve installed helpers through executable or package-relative locations.
SDL's base-directory helper returns the bundle's Resources directory on macOS by default. [SDL installed resource lookup](https://wiki.libsdl.org/SDL3/SDL_GetBasePath)

## Extractor and interpolation changes

Package `tools/extract`, `tools/interp`, and the `tools/reference/anx.py` module they import.
Preserve their Python package layout, including the namespace-package directories.
Do not require `SWCHESS_REPO`, a checkout, or Git on a player's machine.
The optional Git lookup in `tools/interp/pipeline.py:44` already tolerates Git being absent.

Bundle a private Python runtime on Windows and in AppImage.
The Windows embeddable distribution is intended for application bundling. [Python Windows embedding](https://docs.python.org/3/using/windows.html#the-embeddable-package)
Configure its import directories and include the standard-library modules needed by extraction and multiprocessing.
A Flatpak must provide Python inside its runtime or application.
A `.deb` can declare a system Python dependency.

| Locations | Required work |
| --- | --- |
| `tools/extract/locales.py:95,96` | JSON output disables ASCII escaping without declaring a file encoding. Open JSON files with `encoding='utf-8'` and use the same encoding when reading them. A Windows locale can otherwise produce different bytes or an encoding error. [Python text encoding](https://docs.python.org/3/library/io.html#text-encoding) |
| `tools/rife/build.sh:16,20,38,41,42,43,44,49` | The build assumes Homebrew, arm64, MoltenVK, and an executable without `.exe`. Add native Windows and Linux build configurations. Keep MoltenVK flags inside the Apple configuration. |
| `tools/interp/pipeline.py:30,31,60` | RIFE and model locations are relative to the working directory. Missing input silently falls back to `original/win3x/cd`. Use explicit installed helper locations and the selected CD/cache directories. Reject missing resolved input in packaged runs instead of reading the development fixture. |
| `tools/interp/pipeline.py:95,128` | RIFE receives an argument array already. Preserve that behavior. Add a copy fallback when hard links are unavailable, including caches on removable filesystems. |
| `tools/interp/pipeline.py:231` and `tools/interp/__main__.py:58` | Worker processes use `multiprocessing.Pool`. The module entry point already has a main guard. Verify Windows process spawning with the packaged interpreter. Use `freeze_support()` if choosing a frozen Python executable. |
| `tools/interp/pipeline.py:278,280,310` and `tools/interp/check.py:68,112` | Manifests record input filenames that checks and cue refresh later reopen. Store references relative to the cache where possible. Define how an existing cache is relocated between directories or operating systems. |

RIFE upstream supports native Windows and Linux builds and publishes binaries for both.
Those targets use Vulkan directly instead of MoltenVK.
Preserve the repository's pinned RIFE commit and model unless compatibility testing justifies an update. [RIFE upstream instructions](https://github.com/nihui/rife-ncnn-vulkan/blob/master/README.md)
Build or select a matching helper for each architecture and install its runtime dependencies.
Check the redistribution terms for the selected helper, model, and Python runtime before including them.
Use independently generated pictures for public inference checks.
Keep CD-derived frames out of release archives and CI artifacts.

## Work breakdown

Each phase leaves a buildable program and defines its own acceptance checks.
The packaging phase consumes the completed startup and asset-tool work.
The phases form one portability change and do not imply separate public releases.

| Phase | Work | Acceptance |
| --- | --- | --- |
| 1. Build configuration | Add dependency manifests or pinned source presets. Select compiler flags by compiler driver. Declare thread dependencies. Guard all tests with `BUILD_TESTING` and make CD tests opt-in. Replace hardcoded Python commands. | Windows, Ubuntu, and Fedora compile `swchess` and `swchess-viewer` from a checkout without CD files. The chess tests run without private data. The macOS targets still build. |
| 2. Files and directories | Add UTF-8/native filename conversion, shared CD lookup, and platform config/cache locations. Update game readers and Python lookup. Keep explicit CLI operation working throughout. | Synthetic inputs verify uppercase and mixed-case names, Unicode folders, spaces, trailing separators, Windows drive roots, and network shares. Settings and saves work without writing into the install directory or CD folder. |
| 3. Startup and file dialogs | Move CD selection and saved configuration into the game. Change save/load requests to accept asynchronous results. Add setup before `GameShell` construction and preserve headless operation. | A desktop launch selects and remembers a valid CD folder on each OS. Save/load dialogs work after restart. Cancellation, invalid folders, and moved folders produce usable outcomes. Headless commands open no dialogs. |
| 4. Packaged asset tools | Package Python and the required modules. Add native RIFE builds, explicit helper locations, UTF-8 JSON, hard-link fallback, and validated cache publication. Connect extraction and optional interpolation to startup. | A clean machine can extract from its own CD without a checkout. A synthetic interpolation check runs on supported GPU machines. Interrupted extraction preserves the previous cache. Ordinary play works without RIFE or a Vulkan device. |
| 5. Installation and release checks | Add common CMake installation rules, Windows Inno Setup output, Linux AppImage output, independent icons, and desktop metadata. Update the Apple bundle to launch the game directly. Test the selected distribution alternatives if their requirements are adopted. | Install, launch, upgrade, and uninstall on Windows 10/11, Ubuntu 24.04, Fedora 44, and macOS arm64. Check packages without development tools installed. Verify that every package excludes CD files and decoded cache content. |

## GitHub Actions matrix

Use explicit runner labels instead of `*-latest`.
GitHub lists `windows-2022`, `ubuntu-24.04`, and `macos-15` runners.
The standard `macos-15` runner uses arm64. [GitHub hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)

| Job | Runner and environment | Checks |
| --- | --- | --- |
| Windows x64 | Use `windows-2022`, MSVC, Ninja, and the pinned vcpkg dependencies. | Compile both executables, run synthetic tests, stage runtime DLLs, and build the installer. |
| Windows alternate compiler | Use `windows-2022` with clang-cl and compatible dependencies. | Compile both executables and run the same synthetic tests. |
| Ubuntu x64 | Use `ubuntu-24.04` with GCC, Ninja, and pinned SDL3 source. | Compile, run synthetic tests, stage the Linux installation, and build the AppImage. |
| Fedora x64 | Use a `fedora:44` container on `ubuntu-24.04` with Fedora development packages. | Compile against Fedora SDL3 and run synthetic tests. Check the Ubuntu-built AppImage through extracted execution. |
| macOS arm64 | Use `macos-15` with Apple Clang and Ninja. | Compile both executables and the app bundle. Run synthetic tests and verify copied dylibs. |
| MinGW compatibility | Use `windows-2022` with a consistent MSYS2 UCRT64 environment if MinGW support is adopted. | Compile and test against MinGW-built dependencies. Check the separate runtime DLL list. |

Run these builds without `original/` or the generated `assets/` directory.
Use newly authored byte fixtures for decoder, filesystem, dialog-result, and setup tests.
The existing chess rules tests can run without the CD now.
Add the other synthetic tests during the relevant implementation phase.

Run CD-based oracle and gameplay checks locally on machines whose operators provide their own CD files.
Do not upload their decoded pictures, sound data, caches, or original binaries through Actions.
Keep dependency caches separate from game-data directories.

Hosted build runners do not prove Windows 10 or Windows 11 desktop behavior.
Use clean Windows 10 and Windows 11 machines for installer and dialog checks.
Use Ubuntu and Fedora desktop sessions for Wayland, X11 where available, audio, and folder-portal checks.
Use a Vulkan-capable machine for each claimed RIFE platform.
The Colima compile probe cannot establish those results.

## Decisions that change implementation

The plan recommends x64 Windows/Linux releases, MSVC with vcpkg, Inno Setup, AppImage, and optional RIFE generation.
Confirm a requirement for MSI, MSIX, Flatpak publication, Windows arm64, or 32-bit Windows before treating it as release scope.
Those choices change build dependencies, installer authoring, or runtime access tests.

A downloadable RIFE component would need download verification and offline error handling.
A bundled RIFE component would increase package size and require model distribution review.
This plan assumes a bundled optional helper where redistribution permits it.
The game must remain usable with original-cadence animation when that helper is unavailable.
