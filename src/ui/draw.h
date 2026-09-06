// Small drawing helpers the two user interface screens share.
//
// Everything here works on the same top-down RGBA8 Image that assets/bmp.h
// defines, so the button bar and the title sequence draw onto whatever canvas
// the caller owns.
#pragma once

#include <cstdint>

#include "assets/bmp.h"

namespace swchess::ui {

// A rectangle in canvas pixels. `x`, `y` name the top-left corner.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

// Draws straight-alpha RGBA pixels over `canvas`, keeping every pixel inside
// `clip`. Pixels outside the canvas are dropped as well.
void blitClipped(Image& canvas, const std::uint8_t* rgba, int width, int height, int x, int y,
                 const Rect& clip);

// Paints one opaque rectangle. Pixels outside the canvas are dropped.
void fillRect(Image& canvas, const Rect& rect, std::uint8_t red, std::uint8_t green,
              std::uint8_t blue);

// Paints a one-pixel outline just inside `rect`.
void strokeRect(Image& canvas, const Rect& rect, std::uint8_t red, std::uint8_t green,
                std::uint8_t blue);

// A black canvas of the given size, alpha 255 everywhere.
Image makeCanvas(int width, int height);

}  // namespace swchess::ui
