// Software compositing used by the headless `--dump-frame` path.
// The viewer draws through SDL, but this machine has no display to check, so
// the same placement math also runs on the CPU and writes a PPM file.
#pragma once

#include <cstdint>
#include <string>

#include "assets/bmp.h"

namespace swchess {

// Where a sprite of the given size lands when centered on `canvas`.
// The exact board placement in the original game is not recovered yet.
struct Placement {
    int x = 0;
    int y = 0;
};

Placement centerOn(const Image& canvas, int spriteWidth, int spriteHeight);

// Draws straight-alpha RGBA pixels over `canvas` with source-over blending.
// Pixels outside the canvas are skipped.
void blitRGBA(Image& canvas, const std::uint8_t* rgba, int width, int height, int x, int y);

// Writes `canvas` as a binary PPM (P6). Alpha is dropped, and every background
// is opaque, so nothing is lost.
void writePPM(const Image& canvas, const std::string& path);

}  // namespace swchess
