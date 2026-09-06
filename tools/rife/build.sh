#!/bin/bash
# Build rife-ncnn-vulkan for arm64 macOS against MoltenVK.
# Clones a pinned commit into .cache/rife/src, builds, and installs the
# binary plus the rife-v4.6 model into .cache/rife/bin. Safe to re-run.
set -euo pipefail

RIFE_REPO="https://github.com/nihui/rife-ncnn-vulkan"
RIFE_COMMIT="a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CACHE="$ROOT/.cache/rife"
SRC="$CACHE/src"
BUILD="$CACHE/build"
BIN="$CACHE/bin"

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

cmake -S "$SRC/src" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DUSE_STATIC_MOLTENVK=ON \
  -DVulkan_INCLUDE_DIR="$VK_HEADERS/include" \
  -DVulkan_LIBRARY="$MOLTENVK/lib/libMoltenVK.a"

cmake --build "$BUILD" --parallel

mkdir -p "$BIN"
cp -f "$BUILD/rife-ncnn-vulkan" "$BIN/rife-ncnn-vulkan"
rm -rf "$BIN/rife-v4.6"
cp -R "$SRC/models/rife-v4.6" "$BIN/rife-v4.6"

echo "built $BIN/rife-ncnn-vulkan at commit $RIFE_COMMIT"
file "$BIN/rife-ncnn-vulkan"
