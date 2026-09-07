#!/bin/bash
# Build rife-ncnn-vulkan for arm64 macOS against MoltenVK.
# Clones a pinned commit into .cache/rife/src, builds, and installs the
# binary plus the rife-v4.6 model into .cache/rife/bin. Safe to re-run.
#
# The finished binary has to be relocatable, which here means it loads
# nothing outside /System/Library and /usr/lib and carries no runpath. That
# is what lets the packaging lane copy it into the application bundle
# without rewriting any load command. MoltenVK links in statically, so the
# player needs no installed Vulkan loader. The script audits both of those
# at the end and stops if either one fails.
#
# Set MACOS_MIN to change the oldest macOS the binary runs on. The default
# matches the candidate minimum in docs/research/packaging-macos.md.
set -euo pipefail

RIFE_REPO="https://github.com/nihui/rife-ncnn-vulkan"
RIFE_COMMIT="a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CACHE="$ROOT/.cache/rife"
SRC="$CACHE/src"
BUILD="$CACHE/build"
BIN="$CACHE/bin"

MODEL="rife-v4.6"
MACOS_MIN="${MACOS_MIN:-11.0}"

BREW_PREFIX="${BREW_PREFIX:-/opt/homebrew}"
MOLTENVK="$BREW_PREFIX/opt/molten-vk"
VK_HEADERS="$BREW_PREFIX/opt/vulkan-headers"

for pkg in molten-vk vulkan-headers; do
  if [ ! -d "$BREW_PREFIX/opt/$pkg" ]; then
    echo "installing $pkg"
    brew install "$pkg"
  fi
done

if [ ! -d "$SRC/.git" ]; then
  mkdir -p "$SRC"
  git -C "$SRC" init -q
  git -C "$SRC" remote add origin "$RIFE_REPO"
fi
if [ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo none)" != "$RIFE_COMMIT" ]; then
  git -C "$SRC" fetch --depth 1 origin "$RIFE_COMMIT"
  git -C "$SRC" checkout -q "$RIFE_COMMIT"
fi
git -C "$SRC" submodule update --init --recursive --depth 1

# USE_STATIC_MOLTENVK only adds the Metal and Cocoa frameworks to the link
# line. The two Vulkan_ paths are what point the build at the static
# MoltenVK archive instead of a dylib.
cmake -S "$SRC/src" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_INSTALL_RPATH= \
  -DUSE_STATIC_MOLTENVK=ON \
  -DVulkan_INCLUDE_DIR="$VK_HEADERS/include" \
  -DVulkan_LIBRARY="$MOLTENVK/lib/libMoltenVK.a"

cmake --build "$BUILD" --parallel

mkdir -p "$BIN"
# Delete the old binary before copying rather than writing over it. macOS
# caches a signed executable's code signature against the inode, and writing
# new bytes into that same inode makes the kernel kill the next run with
# SIGKILL even though the file is a valid signed binary. Removing it first
# gives the copy a new inode and no stale cache entry.
rm -f "$BIN/rife-ncnn-vulkan"
cp "$BUILD/rife-ncnn-vulkan" "$BIN/rife-ncnn-vulkan"
rm -rf "$BIN/$MODEL"
cp -R "$SRC/models/$MODEL" "$BIN/$MODEL"
# The weights carry no license file of their own, so keep the one license
# the package does have next to the binary that uses them.
cp -f "$SRC/LICENSE" "$BIN/rife-ncnn-vulkan.LICENSE"

for weight in flownet.bin flownet.param; do
  if [ ! -f "$BIN/$MODEL/$weight" ]; then
    echo "FAIL the model folder $BIN/$MODEL has no $weight" >&2
    exit 1
  fi
done

# Anything the binary loads from outside these two system directories would
# have to be copied into the bundle and have its load command rewritten, so
# treat it as a build failure here instead.
outside=$(otool -L "$BIN/rife-ncnn-vulkan" | tail -n +2 | awk '{print $1}' \
  | grep -v '^/System/Library/' | grep -v '^/usr/lib/' || true)
if [ -n "$outside" ]; then
  echo "FAIL the binary loads libraries from outside the system:" >&2
  echo "$outside" >&2
  exit 1
fi

runpath=$(otool -l "$BIN/rife-ncnn-vulkan" | grep -c LC_RPATH || true)
if [ "$runpath" != "0" ]; then
  echo "FAIL the binary carries $runpath runpath entries and should carry none" >&2
  otool -l "$BIN/rife-ncnn-vulkan" | grep -A2 LC_RPATH >&2
  exit 1
fi

if otool -L "$BIN/rife-ncnn-vulkan" | grep -q MoltenVK; then
  echo "FAIL MoltenVK is linked as a library instead of statically" >&2
  exit 1
fi

# Prove the installed copy runs, not just the one in the build directory.
if ! "$BIN/rife-ncnn-vulkan" -m "$BIN/$MODEL" \
     -0 "$SRC/images/0.png" -1 "$SRC/images/1.png" -s 0.5 \
     -o "$CACHE/install-check.png" > /dev/null 2>&1; then
  echo "FAIL the installed binary cannot interpolate the sample images" >&2
  exit 1
fi
rm -f "$CACHE/install-check.png"

echo "built $BIN/rife-ncnn-vulkan at commit $RIFE_COMMIT"
echo "the $MODEL weights are in $BIN/$MODEL, which is what --model wants"
echo "the license is $BIN/rife-ncnn-vulkan.LICENSE"
echo "it loads only system libraries, carries no runpath, and holds MoltenVK inside it"
file "$BIN/rife-ncnn-vulkan"
xcrun vtool -show-build "$BIN/rife-ncnn-vulkan" | grep -E 'platform|minos'
