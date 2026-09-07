# macOS application packaging plan

Ship a signed and notarized DMG that contains a self-contained application.
Build SDL3 from pinned source and link it statically.
Move CD selection into the native application.
Use the existing C++ decoders for original playback and finish the native exporter for optional local cache generation.
Keep decoded artwork outside the application bundle.

Exclude raw CD files under `original/` from every release artifact.
Never publish the decoded `assets/` tree or its interpolated frames.
Build the payload from an explicit file list so ignored local files cannot enter it accidentally.

This plan reflects the repository and documentation reviewed on September 7, 2026.
The commands below describe proposed builds and release operations.
This research does not implement or execute them.

## Current repository

| File | Current behavior | Proposed change |
| --- | --- | --- |
| `README.md` | It requires Apple silicon, macOS 15, Homebrew, Python, and the player's CD. | Separate instructions for installing a release from instructions for building the source. |
| `CMakeLists.txt` | It finds installed SDL3 and requires Python 3.10 even when tests are disabled. | Fetch pinned dependencies and require Python only for development tests. |
| `scripts/build-app.sh` | It forces `arm64` and writes an application into `build/`. | Accept a build directory, architecture list, and explicit minimum macOS version. |
| `packaging/Info.plist.in` | It names the shell launcher as `CFBundleExecutable` and declares macOS 15.0. | Name the native executable and generate the minimum version from the build configuration. |
| `packaging/launcher.sh.in` | It saves CD and cache locations in `~/Library/Application Support/Star Wars Chess/config`. It requires `catalog.json` and runs Python from `SWCHESS_REPO` when that file is absent. | Preserve existing configuration while moving setup into C++. Remove the source directory and Python fallback from release bundles. |
| `packaging/make-icns.sh` | It uses decoded `assets/ui/STLGO16.png` when available. It uses Python to generate a fallback image otherwise. | Always use an original icon checked into `packaging/`. |
| `cmake/BundleDylibs.cmake` | It copies libraries with absolute locations and rewrites their load commands. It skips dependencies beginning with `@`. | Remove the SDL copying step for static builds. Audit every executable and library in the finished bundle. |
| `scripts/check-fresh-checkout.sh` | It installs the Brewfile and borrows the developer's CD and cache. It invokes the game executable directly. | Retain the developer check and add a separate test of the downloaded application. |
| `Brewfile` | It installs CMake, Ninja, SDL3, and nlohmann-json. | Keep build tools optional for contributors. Remove SDL3 and nlohmann-json when CMake supplies them. |
| `tools/extract/` | It writes PNG, WAV, and JSON files with Python's standard library. It also imports `tools/reference/anx.py`. | Keep Python as the reference implementation and finish a C++ exporter. |

The library helper already runs `codesign --force --sign -` after changing load commands.
These are ad-hoc signatures on individual binaries.
The repository has no Developer ID signing, notarization, signed distribution container, or final bundle verification.
The helper also suppresses errors from several signing and load-command operations.

The game already supports playback without decoded files on disk.
The entry point, `src/app/game_main.cpp`, documents the optional `--assets` argument.
The animation loader, `src/anim/interp.cpp`, returns no enhanced sequence when its manifest is absent.
The launcher imposes the current mandatory extraction step.
Removing that requirement can let a player start at the original 120 ms cadence after selecting the CD.

## Dependencies inside the application

### SDL3

FetchContent and static linking answer different questions.
CMake's FetchContent downloads dependency source during configuration.
Static linking incorporates compiled SDL code into each executable.
Use both for the release build. [CMake FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)

Add `cmake/Dependencies.cmake` and include it before defining game targets.
Enable C and C++ in the top-level `project()` declaration.
Pin SDL 3.4.16 at commit `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c`.
That release is also the version installed on this development machine. [SDL 3.4.16 release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)

The dependency declaration should use the following settings.
The SSH URL follows this machine's GitHub configuration.

```cmake
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY git@github.com:libsdl-org/SDL.git
  GIT_TAG fa2c02bb6e21974a89ea9824bc53c9932abe5f9c
)
FetchContent_MakeAvailable(SDL3)
```

Link the game and viewer to `SDL3::SDL3-static`.
An explicit target avoids changing the linkage accidentally when both variants are enabled.
SDL documents both the static target and the build options. [SDL CMake options](https://wiki.libsdl.org/SDL3/README-cmake)

Cache the pinned source for release builds.
Use `FETCHCONTENT_SOURCE_DIR_SDL3` to supply an existing source checkout when building offline.
An HTTPS source archive with a recorded SHA-256 is another option for builders without GitHub SSH access.
Neither mechanism downloads SDL on the player's machine. [CMake FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)

| Linkage | Benefit | Cost |
| --- | --- | --- |
| Static SDL | The bundle needs no SDL dylib or SDL load-command rewrites. | The game and viewer each include SDL code. Updating SDL requires rebuilding them. |
| Bundled shared SDL | The game and viewer share one SDL library. | Packaging must copy, locate, sign, and verify that library for every executable. |

A correctly bundled shared SDL already removes the player's Homebrew requirement.
Vendoring controls the SDL version, supported architectures, and minimum macOS version.
Recommend static SDL because this application has few executables and no SDL plugin requirement.
Measure the bundle size before making that choice permanent.

If shared SDL remains an option, copy `$<TARGET_FILE:SDL3::SDL3-shared>` explicitly into `Contents/Frameworks`.
Give each consumer an `@executable_path/../Frameworks` runpath.
Resolve dependencies expressed with `@rpath` and `@loader_path` during the audit.
Make every failed `otool`, `install_name_tool`, and `codesign` operation fail the packaging command.
Complete all load-command changes before release signing.

### Other build dependencies

Pin nlohmann-json, the JSON library, in `cmake/Dependencies.cmake` too.
Its headers become part of the build and require no runtime installation.
Guard its existing `find_package` calls in `src/anim/CMakeLists.txt` and `src/save/CMakeLists.txt` when the target already exists.
Apply the same target check to SDL discovery in `src/audio/CMakeLists.txt`.
Keep standalone module builds working.

Continue using macOS's zlib through `ZLIB::ZLIB`.
Reject release artifacts that load a Homebrew zlib instead.
Include third-party license notices under `Contents/Resources/Licenses`.
SDL's zlib license permits static redistribution. [SDL license](https://github.com/libsdl-org/SDL/blob/release-3.4.0/LICENSE.txt)

Move required Python discovery behind an option such as `SWCHESS_ORACLE_TESTS`.
Keep that option separate from ordinary C++ tests.
Guard test executables and Python discovery in the module CMake files with `BUILD_TESTING`.
Today several modules create tests regardless of that flag.
The application must configure with `-DBUILD_TESTING=OFF -DSWCHESS_ORACLE_TESTS=OFF` without finding Python or CD files.

## Native setup and local artwork

### CD selection

Add setup handling in `src/app/startup.cpp` before constructing the game session.
Use SDL's native folder dialog, `SDL_ShowOpenFolderDialog`, for the CD selection.
Invoke it on the main thread and forward its callback result into the event loop. [SDL folder dialog](https://wiki.libsdl.org/SDL3/SDL_ShowOpenFolderDialog)

Set `CFBundleExecutable` to `Star Wars Chess` in `packaging/Info.plist.in`.
Remove the launcher copy and `SWCHESS_SOURCE_DIR` substitution from `CMakeLists.txt`.
Retire `packaging/launcher.sh.in` after the native startup code reads its existing two-line configuration.
Preserve `SWCHESS_CD`, `SWCHESS_ASSETS`, `--cd`, and `--assets` for development and scripted checks.
Remove `SWCHESS_REPO` from the release workflow.

Validate the files required by the native loaders before starting the game.
Checking only `XCHESS.EXE` misses incomplete CD copies.
Check the INI, ANX, BMP, audio, piece, title, and language resources as well.
Include `XCHESS.EXE` and the font resources in the source fingerprint.
The Python extractor's present source list does not cover every file the native UI reads.

Use a shared filename lookup that ignores case across validation and the loaders.
The launcher currently recognizes only three spellings of `XCHESS.EXE`.
Several loaders request uppercase filenames directly.
Test a lowercase CD copy on a filesystem that distinguishes letter case.

Keep the CD folder read-only.
Parse Windows EXE and DLL files as data without executing them.
Keep the folder available for later launches because the game still reads it during playback.
Offer another folder selection if the saved location disappears or access fails.
Treat cancellation as a normal exit from setup.
Use native dialogs for errors that a Finder launch cannot show in a terminal.

### C++ extraction

Recommend finishing the C++ exporter rather than including Python in the standard release.
Add a reusable export library and a command-line executable, `src/app/extract_main.cpp`, that builds as `swchess-extract`.
Implement export orchestration in `src/extract/export.cpp` as the separate `swchess_export` CMake target.
The application should call the export library from a worker thread when a player requests decoded files or enhanced captures.
Ordinary original playback should not wait for this export.

| Python responsibility | Existing native code | Work still needed |
| --- | --- | --- |
| `ne.py`, `dib.py`, `ini.py`, and `pieces.py` decode resources. | `src/assets/ne.cpp`, `bmp.cpp`, `ini.cpp`, `anx.cpp`, and `piece_dll.cpp` decode the same formats. | Add output traversal, manifests, progress, cancellation, and complete input validation. |
| `sheets.py` and `audio.py` export cells and sounds. | `src/assets/sheet.cpp` slices cells and `wav.cpp` reads the WAVE records. | Write sheet manifests and preserve RIFF bytes, including duplicate sound records. |
| `captures.py` and `cues.py` resolve animation timing. | `src/anim/capture.cpp` already resolves the native timeline. | Serialize `timeline.json` and `resolved.json` from the same timing rules. |
| `locales.py` and `ui.py` export strings and title bitmaps. | `src/text/strings.cpp`, `src/text/font.cpp`, and `src/ui/res_bitmap.cpp` read these resources. | Export stored string bytes and bitmap metadata without substituting a text encoding. |
| `png.py` writes images and `__main__.py` writes `catalog.json`. | `src/anim/png_read.cpp` reads RGBA PNGs. | Add PNG writing, SHA-256 hashes, catalog generation, and cache version checks. |

Keep the decoders in `swchess_assets` independent of SDL.
Add a small zlib PNG writer under `src/assets/png_write.cpp` to preserve the existing RGB and RGBA output contract.
SDL 3.4 also offers `SDL_SavePNG` if the exporter can depend on SDL.
Either choice needs no Python or separate SDL_image installation. [SDL PNG writer](https://wiki.libsdl.org/SDL3/SDL_SavePNG)

Compare native output against Python using decoded pixels, sound bytes, and normalized JSON.
Different PNG compressors need not produce identical compressed files.
Recompute each output hash from the file actually written.
Exclude generation timestamps and absolute directory names from cross-run comparisons.

For the known CD, verify 4,799 distinct capture records, 5,414 used timeline entries, and 5,342 displayed poses across 72 captures.
Verify 1,344 piece bitmaps, 110 sound resource records under 109 names, eight BMP files, and four locale files.
Keep the existing decoder and timing comparisons in `tests/CMakeLists.txt` and `tests/timeline_parity.py`.
The raw ANX total of 5,423 includes nine records that the game never plays.
Do not confuse that total with the exporter's 5,414 used entries.

The proposed standalone command should work without the application UI.

```sh
cmake --build build-release --target swchess-extract
./build-release/swchess-extract \
  --cd '/path/to/player/CD' \
  --out '/path/to/private/cache' \
  --verify
```

### Bundled Python alternative

A private Python runtime can remove the requirement for an installed Python.
It preserves the reference exporter and reduces the amount of decoder work.
It also adds a runtime that must receive security updates and support every advertised CPU and macOS version.

If this alternative is selected, build a relocatable CPython distribution for the chosen minimum version.
Put its framework under `Contents/Frameworks/Python.framework`.
Put `tools/extract`, `tools/reference/anx.py`, and their package files under `Contents/Resources/python`.
Include the standard library, encodings, compression, hashing modules, and all their native dependencies.
Copying a Homebrew `python3` executable alone cannot provide that runtime.

Use a native helper that initializes CPython with `PyConfig_InitIsolatedConfig`.
Set explicit module search directories relative to the bundle and disable bytecode writes.
Do not use the user's Python environment or import from the current directory. [CPython isolated configuration](https://docs.python.org/3/c-api/init_config.html)

Sign native extension modules and the Python framework before signing the helper and application.
Preserve framework symlinks when packaging.
Use the existing extractor arguments, `--cd` and `--out`, through that helper.
Do not install pip packages or fetch Python on first launch.
Including Python for extraction still leaves the separate RIFE pipeline to package.

### Cache storage and generation

Use `~/Library/Application Support/Star Wars Chess/assets` as the managed cache root.
Keep the existing `config` file and saved settings outside that directory.
Keep generated artwork outside `Star Wars Chess.app`, `/Applications`, and the CD folder.
Replacing the application must preserve the player's configuration and cache.

Application Support is a deliberate choice for files that can take substantial GPU work to reproduce.
Apple also provides `~/Library/Caches` for discardable performance data.
That location is suitable if automatic deletion and regeneration are acceptable.
Provide a visible command to remove enhanced frames and report their size. [Apple filesystem directories](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/FileSystemOverview/FileSystemOverview.html)

| Source of artwork | Recommendation | Consequence |
| --- | --- | --- |
| The app decodes the player's CD locally. | Use this for original artwork and optional cache generation. | Playback works offline and the release contains no CD-derived images or sounds. |
| A server supplies a prebuilt 900 MB cache. | Reject this under the repository's distribution rule. | Decoding or interpolation does not make CD-derived artwork generic. Asking for a CD first does not satisfy that rule. |
| The app includes generic resources. | Include only original setup graphics, an original icon, and third-party resources whose licenses permit distribution. | Generic graphics do not recreate the game's original artwork or capture films. |

The supplied 900 MB figure includes interpolated 60 fps frames.
Python extraction alone does not generate those frames.
The existing RIFE process takes about an hour for all captures on the recorded development hardware.
That measurement is not a performance promise for other Macs.

Offer local enhancement as an explicit action after CD selection.
Let the player use original captures while enhanced frames are being generated.
Show progress by completed capture and allow cancellation and resumption.
The game should use a completed enhanced capture only after its manifest passes validation.
Keep the application in original mode for missing, corrupt, or incompatible enhanced sequences.

Add `src/interp/` and a `swchess-interpolate` helper to replace the orchestration in `tools/interp/`.
Port timeline sampling, canvas placement, alpha processing, output manifests, and cue preservation.
A C++ extractor alone does not remove Python from this part of the application.
In `CMakeLists.txt`, make `StarWarsChess` depend on each enabled helper target before copying its executable into `Contents/MacOS`.

Package the pinned `rife-ncnn-vulkan` executable under `Contents/MacOS`.
Package the `rife-v4.6` model under `Contents/Resources/models` after checking its redistribution terms.
Build ncnn, the inference library, and MoltenVK, the Vulkan implementation for macOS, as pinned dependencies.
Use static MoltenVK to avoid requiring a separately installed Vulkan loader. [RIFE build instructions](https://github.com/nihui/rife-ncnn-vulkan/blob/master/README.md)

Replace the Homebrew and arm64 assumptions in `tools/rife/build.sh` for release builds.
Invoke the packaged helper with argument arrays and explicit model locations.
Never invoke `brew`, CMake, Python, or a compiler on the player's machine.
Check GPU availability before starting enhancement.
If the packaged backend cannot run, preserve original playback and explain that enhancement is unavailable.

Use a cache generation directory identified by the CD fingerprint and cache format version.
Keep the existing relative layout beneath each generation so `--assets` can select it.
Track the extractor version, interpolation version, model hash, input hashes, and completed captures in its metadata.
Import an existing configured cache only after validating its contents.
Do not treat the presence of `catalog.json` as proof of completion.
Today Python writes that file before the caller checks every expected count.

Write each capture into a temporary sibling directory.
Validate its frames and timing before renaming it into place.
Switch the active generation only after validating required output.
Preserve the previous generation when extraction fails.
Use a lock to prevent two application instances from generating the same files.

Check available disk space before generation and during long writes.
Two complete 900 MB generations require about 1.8 GB before intermediate images and the CD folder are counted.
Measure temporary RIFE output to determine the additional allowance.
Delete temporary files after successful work and offer recovery after interrupted work.
Never place CD files or generated cache files in public build artifacts or test logs.

## Distribution container

| Format | Installation behavior | Fit for this application |
| --- | --- | --- |
| `.dmg` | Finder mounts a disk image. The player copies the app beside an Applications shortcut. | Recommend it. It explains installation and can include concise CD requirements. Extraction still happens in the player's account. |
| `.pkg` | Apple Installer copies files to declared destinations and records installation receipts. System installation commonly requires administrator authorization. | Use only if managed deployment or additional installed components become requirements. An installer cannot choose the CD for every user. |
| `.zip` | Archive Utility expands the app into the download directory. The player moves it manually. | Offer it as an optional download for experienced users. It provides fewer installation cues. |

No container can supply the player's CD.
No package installation script should extract artwork or write a particular user's configuration.
Keep CD selection in the application for all formats.
Support `~/Applications` when the player cannot write to `/Applications`.

Build the DMG with Apple's `hdiutil` utility.
Use a clean staging directory containing the app, an `/Applications` symlink, and installation instructions.
Explain that the player must keep their CD folder available.
Show a copy-to-Applications instruction if the app runs from the mounted DMG.
Avoid relying on the working directory or files beside the app.

Build an optional ZIP with `ditto -c -k --sequesterRsrc --keepParent` after stapling the app.
A ZIP itself cannot receive a notarization ticket.
For an optional PKG, build from the signed app with `productbuild --component` and sign it with Developer ID Installer.
The application inside still needs its own Developer ID Application signature. [Apple signing guidance](https://developer.apple.com/library/archive/technotes/tn2206/_index.html)

## Signing and notarization

### Developer account and certificates

The Apple Developer Program costs US$99 per membership year, or the local currency price where available.
Developer ID certificates and notarization are included in that membership.
There is no separate Developer ID certificate purchase for each application. [Apple enrollment](https://developer.apple.com/programs/enroll/)

Enroll through Apple's website on a device with a visible browser.
Create a Developer ID Application certificate for the release signer.
Install that certificate and its matching private key in the release machine's Keychain.
Create a Developer ID Installer certificate only if producing a PKG. [Apple Developer ID certificates](https://developer.apple.com/help/account/certificates/create-developer-id-certificates/)

Use the installed signing identity and create a notarization credential profile.
Replace the identity, account, and team placeholders with the release account's values.
The credential command prompts for the app-specific password and stores it in Keychain.
It does not need a browser redirect to this SSH session.

```sh
security find-identity -v -p codesigning
xcrun notarytool store-credentials 'swchess-notary' \
  --apple-id 'release-account@example.com' \
  --team-id 'TEAMID'
```

Keep signing credentials outside the repository.
Use an App Store Connect API key instead if release automation needs it.
The signer also needs Xcode command-line utilities with `notarytool` and `stapler`.
Do not use the retired notarization interface in `altool`. [Apple notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)

### Release commands

Finish compilation, architecture merging, icon generation, copying, and load-command changes before signing.
Set the release version in `CMakeLists.txt` before generating `Info.plist`.
Use the same bundle identifier across releases.
Keep `CFBundleVersion` increasing.

Sign nested executable code before the enclosing application.
The example assumes static SDL and native helpers.
If Python, shared SDL, or Sparkle is included, sign their nested code in the required order before this application step.
Do not use `codesign --deep` to sign everything indiscriminately.

```sh
set -eu
release_app='build-release/Star Wars Chess.app'
release_identity='Developer ID Application: NAME (TEAMID)'
release_version='0.7.0'

for helper in swchess-viewer swchess-extract swchess-interpolate rife-ncnn-vulkan; do
  if [ -f "$release_app/Contents/MacOS/$helper" ]; then
    codesign --force --sign "$release_identity" --timestamp --options runtime \
      "$release_app/Contents/MacOS/$helper"
  fi
done

codesign --force --sign "$release_identity" --timestamp --options runtime \
  "$release_app"
codesign --verify --deep --strict --verbose=2 "$release_app"
codesign --display --verbose=4 "$release_app"
```

The hardened runtime adds execution protections and is required for notarized applications.
Keep its default protections for the native build.
Do not request App Sandbox, JIT, unsigned executable memory, or disabled library validation without a demonstrated requirement.
Ensure that release entitlements do not enable `com.apple.security.get-task-allow`.
Reading the player's CD as data does not require running unsigned Windows code. [Apple notarization preparation](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)

Create and sign the DMG from the signed app.
The example uses a new staging directory on each run.
The proposed `packaging/Install.txt` supplies the installation instructions.

```sh
mkdir -p dist
release_stage=$(mktemp -d /tmp/swchess-dmg.XXXXXX)
ditto "$release_app" "$release_stage/Star Wars Chess.app"
ln -s /Applications "$release_stage/Applications"
cp packaging/Install.txt "$release_stage/Install.txt"
release_dmg="dist/StarWarsChess-$release_version.dmg"
hdiutil create -volname 'Star Wars Chess' -srcfolder "$release_stage" \
  -format UDZO -ov "$release_dmg"
codesign --force --sign "$release_identity" --timestamp "$release_dmg"
xcrun notarytool submit "$release_dmg" \
  --keychain-profile 'swchess-notary' --wait --output-format json \
  > "dist/StarWarsChess-$release_version-dmg-notary.json"
```

Require the service to report `Accepted` before attaching the DMG ticket.
Record the submission ID and retain the JSON response with the release records.
Apple recommends notarizing the outermost container, which covers the nested signed app.
This DMG workflow needs one submission. [Apple distribution packaging](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution)

If the service rejects the submission, retrieve its log and correct the reported files before rebuilding and resubmitting.

```sh
xcrun notarytool log 'SUBMISSION-ID' \
  --keychain-profile 'swchess-notary' 'dist/notary-log.json'
```

After acceptance, attach the ticket and check the final DMG.

```sh
xcrun stapler staple "$release_dmg"
xcrun stapler validate "$release_dmg"
codesign --verify --verbose=2 "$release_dmg"
spctl --assess --type open --context context:primary-signature \
  --verbose=4 "$release_dmg"
shasum -a 256 "$release_dmg" > "$release_dmg.sha256"
```

Add explicit checks of the notarization JSON status to the future release script.
A completed submission is not necessarily an accepted submission.
Do not modify signed bundle contents afterward.
Do not re-sign after notarization.
Generate release hashes after stapling changes the final downloadable file.

For a ZIP-only release, submit the signed app in a ZIP.
After acceptance, staple the app and recreate the ZIP from that app.
The following commands describe that alternative to the DMG submission. [Apple ZIP notarization](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)

```sh
ditto -c -k --sequesterRsrc --keepParent "$release_app" \
  "dist/StarWarsChess-$release_version-submit.zip"
xcrun notarytool submit "dist/StarWarsChess-$release_version-submit.zip" \
  --keychain-profile 'swchess-notary' --wait
```

Continue only after `Accepted`.

```sh
xcrun stapler staple "$release_app"
xcrun stapler validate "$release_app"
ditto -c -k --sequesterRsrc --keepParent "$release_app" \
  "dist/StarWarsChess-$release_version.zip"
shasum -a 256 "dist/StarWarsChess-$release_version.zip"
```

If the identical app was already accepted inside the DMG, its ticket can be stapled without another submission.
For a PKG, use the following command after the app has passed its checks.
Submit the resulting PKG with `notarytool`, require `Accepted`, and staple and validate the PKG.

```sh
productbuild --component "$release_app" /Applications \
  --sign 'Developer ID Installer: NAME (TEAMID)' \
  "dist/StarWarsChess-$release_version.pkg"
pkgutil --check-signature "dist/StarWarsChess-$release_version.pkg"
```

Run `syspolicy_check distribution` against the final app on macOS 14 or later.
Use `spctl --assess --type execute --verbose=4` on older macOS versions.
Run these checks after notarization because a valid Developer ID signature alone may still be rejected.
Assess a final PKG with `spctl --assess --type install --verbose=4`.
Test the distributed artifact in a fresh environment as well. [Apple Gatekeeper diagnostics](https://developer.apple.com/forums/thread/706379)

### Gatekeeper and builds without Developer ID

Gatekeeper checks downloaded applications before normal launch.
An ad-hoc signature verifies code integrity without identifying a trusted developer.
It costs nothing and can make locally built Apple silicon executables runnable.
It cannot substitute for Developer ID or obtain notarization. [Apple code signing](https://developer.apple.com/library/archive/technotes/tn2206/_index.html)

For a development artifact, sign each nested executable with `codesign --force --sign -`.
Sign the enclosing app last with the same command.
Then run `codesign --verify --deep --strict` on the app.
This verifies the signature structure, not Gatekeeper acceptance.

| Artifact | Expected behavior on a normally configured Mac |
| --- | --- |
| An unsigned or ad-hoc signed download | Gatekeeper normally blocks routine launch because it cannot establish trusted distribution. The player may be able to grant an individual exception. |
| A Developer ID signed download without notarization | The signature identifies the developer, but current Gatekeeper policy normally still requires notarization. |
| A Developer ID signed and notarized download | Gatekeeper can accept it after checking integrity and the notarization ticket. A normal downloaded-app confirmation may still appear. |
| A signed and notarized download with a stapled ticket | The ticket travels with the artifact and supports validation when Apple's service cannot be reached. Other local policies and malware checks still apply. |

Do not promise that right-click Open works on every supported macOS version.
macOS Sequoia removed that override for software that is not correctly signed or notarized.
Apple directs users to System Settings, Privacy & Security, and Open Anyway after a blocked attempt.
Offer that explanation only for clearly marked development downloads.
Do not instruct players to disable Gatekeeper or clear quarantine recursively. [Apple Gatekeeper change](https://developer.apple.com/news/?id=saqachfa), [Apple opening blocked apps](https://support.apple.com/en-us/102445)

## Architectures and macOS versions

SDL 3.4's documented deployment minimum is macOS 10.13 High Sierra for Intel.
SDL requires Xcode 12.2 and the macOS 11 SDK or newer to build.
The build SDK and deployment minimum are different requirements.
The maintainer's deployment change and macOS build guide both specify 10.13. [SDL deployment change](https://discourse.libsdl.org/t/sdl-bumped-deployment-requirements-for-apple-platforms/56913), [SDL macOS guide](https://wiki.libsdl.org/SDL3/README-macos)

The broad platform overview lists 10.14 instead.
This plan follows the explicit macOS deployment guidance for the technical minimum.
Verify the pinned release's build settings before advertising support for either old Intel version. [SDL platform overview](https://wiki.libsdl.org/SDL3/README-platforms)

Recommend macOS 11.0 as the application's candidate minimum for both architectures.
Apple silicon itself requires macOS 11 or later.
This choice avoids extending the entire C++ application to SDL's oldest Intel target without evidence.
It remains a proposal until the application, C++ standard library, SDL configuration, and optional inference backend pass tests there.
If a bundled dependency requires a newer system, rebuild it for the chosen minimum or raise the documented requirement.

Set `CMAKE_OSX_DEPLOYMENT_TARGET` explicitly when configuring a fresh build directory.
Generate `LSMinimumSystemVersion` from that same value in `packaging/Info.plist.in`.
The plist alone cannot lower the minimum version recorded in an executable.
CMake requires architecture and deployment settings before its initial `project()` call. [CMake deployment target](https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_DEPLOYMENT_TARGET.html), [CMake architectures](https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_ARCHITECTURES.html)

Replace the arm64 constant in `scripts/build-app.sh` with a configurable architecture list.
Build SDL and every shipped executable for both architectures.
Use a separate build directory to avoid reusing Homebrew discovery or arm64 objects.

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DBUILD_TESTING=OFF -DSWCHESS_ORACLE_TESTS=OFF
cmake --build build-release --target StarWarsChess

file 'build-release/Star Wars Chess.app/Contents/MacOS/Star Wars Chess'
lipo -archs 'build-release/Star Wars Chess.app/Contents/MacOS/Star Wars Chess'
otool -arch all -L \
  'build-release/Star Wars Chess.app/Contents/MacOS/Star Wars Chess'
xcrun vtool -show-build \
  'build-release/Star Wars Chess.app/Contents/MacOS/Star Wars Chess'
```

Repeat the architecture, dependency, and minimum-version checks for every Mach-O file in the app.
That includes `swchess-viewer`, extraction helpers, inference helpers, and any frameworks or Python extension modules.
System libraries under `/usr/lib` and `/System/Library` remain external.
Every other required library must resolve inside the bundle.
Inspect dynamically loaded inference libraries too because `otool -L` does not list arbitrary runtime loads.

If a dependency cannot build both architectures in one CMake invocation, build it twice and merge corresponding binaries with `lipo -create`.
Merge before signing and verify both architectures afterward.
The model and artwork data can be shared between architectures.

Run the x86_64 executable on an Intel Mac with the promised minimum macOS version.
An `arch -x86_64` run under Rosetta on Apple silicon can supplement that check.
It cannot establish compatibility with Intel graphics or an older macOS installation.
Keep enhancement availability separate from original playback support when GPU capabilities differ.

## Updates

Recommend manual application replacement for the initial distribution design.
Add a release-page link to the app's About or Help interface.
The player downloads a new signed DMG and replaces the app while retaining Application Support.
The repository currently has no configured Git remote, so choose a public release location before publishing that link.

Sparkle, the macOS update framework, becomes worthwhile if releases become frequent enough to justify maintaining automatic updates.
Prefer Sparkle 2 over a custom downloader that replaces executable code.
The CD requirement does not prevent Sparkle updates because CD files and caches remain outside the app.

If adopted, add an Objective-C++ adapter in `src/app/macos_updates.mm` and enable `OBJCXX` in CMake.
Compile the adapter with automatic reference counting and link `Sparkle.framework`.
Create `SPUStandardUpdaterController` programmatically after native application startup.
Copy the pinned `Sparkle.framework` with its required helpers and symlinks into `Contents/Frameworks`.
Follow Sparkle's signing instructions for its helper applications and XPC services. [Sparkle setup](https://sparkle-project.org/documentation/), [Sparkle programmatic setup](https://sparkle-project.org/documentation/programmatic-setup/)

Set `SUFeedURL` and `SUPublicEDKey` in `packaging/Info.plist.in`.
Host the update feed and downloads over HTTPS.
Generate a private signing key once with Sparkle's `bin/generate_keys`.
Generate signed update metadata with `bin/generate_appcast` after producing final notarized downloads.
Protect the private update key separately from the public download server. [Sparkle update signing](https://sparkle-project.org/documentation/)

Test an upgrade between two signed versions with a populated cache.
Check that replacement preserves the CD selection, saves, and generated frames.
Handle incompatible cache versions through the cache generation mechanism described above.
Do not download copyrighted artwork through the update feed.

## Work breakdown

Each phase produces a buildable artifact and a specific check.
Optional components must not prevent the base application from building.

1. **Dependency and resource packaging.** Add `cmake/Dependencies.cmake` and configure static SDL and pinned JSON headers. Update module dependency discovery and test options. Replace the CD-derived icon input with an original resource. Build `StarWarsChess` from a checkout without `original/` or `assets/`. Verify that the bundle contains only declared distributable resources.

2. **Native first launch.** Add `src/app/startup.cpp` and migrate the launcher configuration. Update `CFBundleExecutable` and remove the release shell launcher. Keep this component independent of the exporter by supporting original playback immediately. Check folder selection, cancellation, incomplete CDs, moved folders, and operation without a cache.

3. **Native export.** Add the export library, PNG writer, and `swchess-extract` executable. Keep the Python implementation available for development comparisons. Add output, cancellation, corruption, and atomic completion checks. Run the helper independently against the private CD and compare its output with Python. Use synthetic resources in public tests.

4. **Local enhancement.** Add `src/interp/`, `swchess-interpolate`, and the packaged RIFE backend. Extend `tools/rife/build.sh` to use pinned release dependencies. Keep this component optional at configure time. Verify timing, alpha, resumption, disk exhaustion, and failed GPU initialization. Confirm that the application continues using original captures when enhancement is unavailable.

5. **Universal application.** Update `scripts/build-app.sh` and `packaging/Info.plist.in` to use explicit architecture and minimum-version settings. Build every included helper for both CPUs. Audit every Mach-O file with `lipo`, `otool`, and `vtool`. Run original playback and supported enhancement tests on Apple silicon and Intel hardware.

6. **Release containers and trust.** Add `scripts/package-macos.sh`, `scripts/notarize-macos.sh`, and `packaging/Install.txt`. Make the package command accept a completed app so it can be tested independently of compilation. Test assembly with an ad-hoc signed fixture app. Package the Developer ID signed app in a notarized and stapled DMG when the release identity is available. Preserve the submission records and final hashes.

7. **Installation acceptance and documentation.** Add `scripts/check-installed-app.sh` and revise `scripts/check-fresh-checkout.sh`. Document player installation separately in `README.md`. Make the installation check accept a DMG and an explicit private CD location. It must not discover the source tree or a developer cache. Record the manual update procedure and verify app replacement with existing user data.

The installation acceptance check must inspect every executable, including the viewer and helpers.
Reject references to `/opt/homebrew`, `/usr/local`, developer build directories, and unresolved non-system dependencies.
Audit release resources by an explicit permitted-file list.
An extension check alone cannot detect a CD logo converted into an `.icns` file.

Run installation checks in a fresh user account or disposable Mac environment without Homebrew, Python, Xcode tools, or the source tree.
Download the release in a browser so it receives normal quarantine metadata.
Copy the app, eject the DMG, disconnect the network, and launch through Finder.
Check original playback from the supplied CD and local enhancement from an empty cache.
Check signature validity again after generation to prove that setup did not change the bundle.
The visible Finder and Gatekeeper checks require a tester with a display.
Shell checks on this SSH machine cannot substitute for that acceptance test.

## Decisions that need evidence

- Static SDL is the recommended default. A measured size comparison may favor sharing SDL across the game, viewer, and enhancement helpers.
- macOS 11.0 is the proposed application minimum. The complete application has not been tested on macOS 11 or Intel hardware here.
- Bundled native RIFE is the proposed enhancement mechanism. Model redistribution terms, temporary disk usage, and Intel GPU behavior need verification before promising the feature on every supported Mac.

- The rife-v4.6 weights carry no license of their own. `models/rife-v4.6/` in the
  pinned rife-ncnn-vulkan checkout holds `flownet.bin` and `flownet.param` and
  nothing else. No license, readme, or attribution file sits beside them, and
  none sits anywhere else under `models/`.

  The one license in that package is the MIT license at the root of the
  repository, "The MIT License (MIT), Copyright (c) 2020 nihui". It grants the
  right to use, copy, publish, distribute and sell the software, on the
  condition that the copyright notice travels with every copy. It names no
  exception for the model folder, so on its own wording it covers the converted
  weights as well as the code.

  The README lists rife-v4.6 as upstream version 4.6 and names
  https://github.com/hzwer/arXiv2020-RIFE as the original project. It does not
  say what that project permits. So the only written permission to redistribute
  these weights comes from the person who converted them, not from the people
  who trained them. Read the upstream terms and write them here before the
  weights ship inside an application.

  `tools/rife/build.sh` now copies the MIT license to
  `.cache/rife/bin/rife-ncnn-vulkan.LICENSE`, so the packaging lane has the
  notice it must carry.

- The RIFE binary now declares macOS 11.0 as its minimum, but Homebrew's
  MoltenVK 1.4.2 was built for macOS 12.0. The linker reports the mismatch and
  links anyway. So the binary claims macOS 11 while carrying code compiled for
  macOS 12, and no one has run it on macOS 11. Build MoltenVK from source at
  the chosen minimum, or raise the minimum to 12.0, before promising macOS 11.

- The built binary is already relocatable on Apple silicon. It loads Metal,
  QuartzCore, CoreGraphics, Cocoa, IOKit, IOSurface, Foundation, CoreFoundation,
  AppKit, libc++ and libSystem, all of them from `/System/Library` or
  `/usr/lib`, and it carries no runpath. MoltenVK is compiled into it. So
  packaging copies one file and rewrites no load command.
  `tools/rife/build.sh` checks all three after every build. It stops the build
  if the binary loads a library from anywhere else, if it grows a runpath, or
  if MoltenVK turns up as a separate library. Intel graphics and temporary disk
  use are still unmeasured.
