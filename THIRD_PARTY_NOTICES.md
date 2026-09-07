# Third party notices

The Star Wars Chess port is MIT licensed, and `LICENSE` holds that text. This
file lists the software written by other people that the port builds against,
ships, or was made with. Each license claim below was checked against the
license file in the checkout named beside it.

## SDL3

SDL supplies the window, the renderer, the keyboard and mouse events, the audio
output, and the folder and file dialogs. The game and `swchess-viewer` both
link it, and a release links it statically, so the binary you download carries
it. `cmake/Dependencies.cmake` pins release 3.4.16 by URL and SHA-256.

SDL is under the zlib license, copyright 1997 to 2026 Sam Lantinga. The text is
`build/<preset>/_deps/sdl3-src/LICENSE.txt` after a vendored configure.

## nlohmann-json

`src/anim` reads the capture manifests with it, `src/save` writes the JSON
saved game with it, and `src/export` and `src/interp` write their manifests
with it. It is a header-only library compiled into every binary.
`cmake/Dependencies.cmake` pins version 3.11.3.

nlohmann-json is under the MIT license, copyright 2013 to 2022 Niels Lohmann.
The text is `build/<preset>/_deps/nlohmann_json-src/LICENSE.MIT`.

## zlib

zlib compresses the PNG files `swchess-extract` writes and decompresses the ones
the game reads back. macOS and the Linux distributions ship it, so the build
uses the system copy there and compiles the pinned 1.3.1 source only on Windows
and anywhere the header is missing.

zlib is under the zlib license, copyright 1995 to 2022 Jean-loup Gailly and
Mark Adler. The text is `build/<preset>/_deps/zlib-src/LICENSE` on a build that
fetched it.

## rife-ncnn-vulkan

This is the frame interpolator that generates the pictures between the poses
the artists drew. It is a separate program that runs offline. `tools/rife/build.sh`
clones it at commit `a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7` and builds it
into `.cache/rife/bin/`, and `swchess-interpolate` and `tools/interp` start it
as a subprocess. The game itself never loads it and no release contains it.

rife-ncnn-vulkan is under the MIT license, copyright 2020 nihui. The text is
`.cache/rife/src/LICENSE`, and `tools/rife/build.sh` copies it next to the
binary as `rife-ncnn-vulkan.LICENSE`.

### What that binary contains

- **ncnn**, the neural network runtime that executes the model, is under the
  **BSD 3-Clause license**, copyright 2017 THL A29 Limited, a Tencent company.
  `.cache/rife/src/src/ncnn/LICENSE.txt` holds that text and then lists the
  further components ncnn vendors under their own terms, glslang among them.
- **libwebp** is under the BSD 3-Clause license, copyright 2010 Google Inc.
  The text is `.cache/rife/src/src/libwebp/COPYING`.
- **stb_image and stb_image_write**, by Sean Barrett, are dual licensed as MIT
  or public domain. The text sits at the bottom of
  `.cache/rife/src/src/stb_image.h`.

## The RIFE v4.6 model weights

`.cache/rife/bin/rife-v4.6/` holds `flownet.bin` and `flownet.param`, the
trained weights the interpolator runs. `tools/rife/build.sh` puts them there and
only the offline interpolation reads them. No release contains them.

**Their license is unresolved.** `docs/research/packaging-macos.md` records
what the checkout shows.

The model folder holds those two files and nothing else. No license, no readme
and no attribution file sits beside them, or anywhere else under `models/`. The
only license in that package is nihui's MIT license at the repository root. It
names no exception for the model folder, so on its own wording it covers the
converted weights as well as the code.

The project's README names https://github.com/hzwer/arXiv2020-RIFE as the
original work. It does not say what that project permits. So the only written
permission to redistribute these weights comes from the person who converted
them, not from the people who trained them. Read the upstream terms and write
them here before any package ships the weights.

## MoltenVK

MoltenVK implements Vulkan on top of Metal, which is how RIFE reaches the GPU
on a Mac. `tools/rife/build.sh` links Homebrew's static MoltenVK 1.4.2 into the
interpolator binary, so it exists on macOS only and only inside that offline
tool. The game does not use Vulkan.

MoltenVK is under the Apache License 2.0. The text is
`$(brew --prefix)/Cellar/molten-vk/1.4.2/LICENSE`, and `brew info molten-vk`
reports `Apache-2.0`.

## Ghidra

Ghidra 12.1.3 decompiled `XCHESS.EXE` and `CHESSAPP.EXE`. It is a development
tool. Nothing it produced is copied into this repository, and no release
contains any part of it.

Ghidra is under the Apache License 2.0. The text is
`$(brew --prefix)/Cellar/ghidra/12.1.3/LICENSE`, and `brew info ghidra` reports
`Apache-2.0`.

## The game data

The Star Wars Chess artwork, sound, text and executables are copyright
Lucasfilm Ltd. and The Software Toolworks. They are not third party components
of this project. They are not in this repository and not in any public release.
The player supplies their own CD. See the legal section of `README.md`.
