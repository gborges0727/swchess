#include "ui/draw.h"

#include <algorithm>

namespace swchess::ui {

void blitClipped(Image& canvas, const std::uint8_t* rgba, int width, int height, int x, int y,
                 const Rect& clip) {
    const int left = std::max({x, clip.x, 0});
    const int top = std::max({y, clip.y, 0});
    const int right = std::min({x + width, clip.x + clip.width, canvas.width});
    const int bottom = std::min({y + height, clip.y + clip.height, canvas.height});
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            const std::size_t source =
                (static_cast<std::size_t>(py - y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(px - x)) *
                4u;
            const std::uint8_t alpha = rgba[source + 3];
            if (alpha == 0) {
                continue;
            }
            std::uint8_t* out =
                &canvas.rgba[(static_cast<std::size_t>(py) * static_cast<std::size_t>(canvas.width) +
                              static_cast<std::size_t>(px)) *
                             4u];
            if (alpha == 255) {
                out[0] = rgba[source + 0];
                out[1] = rgba[source + 1];
                out[2] = rgba[source + 2];
            } else {
                for (int channel = 0; channel < 3; ++channel) {
                    const int src = rgba[source + static_cast<std::size_t>(channel)];
                    const int dst = out[channel];
                    out[channel] = static_cast<std::uint8_t>((src * alpha + dst * (255 - alpha)) / 255);
                }
            }
            out[3] = 255;
        }
    }
}

void fillRect(Image& canvas, const Rect& rect, std::uint8_t red, std::uint8_t green,
              std::uint8_t blue) {
    const int left = std::max(rect.x, 0);
    const int top = std::max(rect.y, 0);
    const int right = std::min(rect.x + rect.width, canvas.width);
    const int bottom = std::min(rect.y + rect.height, canvas.height);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            std::uint8_t* out =
                &canvas.rgba[(static_cast<std::size_t>(py) * static_cast<std::size_t>(canvas.width) +
                              static_cast<std::size_t>(px)) *
                             4u];
            out[0] = red;
            out[1] = green;
            out[2] = blue;
            out[3] = 255;
        }
    }
}

void strokeRect(Image& canvas, const Rect& rect, std::uint8_t red, std::uint8_t green,
                std::uint8_t blue) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    fillRect(canvas, Rect{rect.x, rect.y, rect.width, 1}, red, green, blue);
    fillRect(canvas, Rect{rect.x, rect.y + rect.height - 1, rect.width, 1}, red, green, blue);
    fillRect(canvas, Rect{rect.x, rect.y, 1, rect.height}, red, green, blue);
    fillRect(canvas, Rect{rect.x + rect.width - 1, rect.y, 1, rect.height}, red, green, blue);
}

Image makeCanvas(int width, int height) {
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    for (std::size_t at = 3; at < image.rgba.size(); at += 4) {
        image.rgba[at] = 255;
    }
    return image;
}

}  // namespace swchess::ui
