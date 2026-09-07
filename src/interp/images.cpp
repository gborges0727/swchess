#include "interp/images.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "interp/png_io.h"

namespace swchess::interp {

Rect unionRect(const Json& poses) {
    if (poses.empty()) {
        throw std::runtime_error("a capture with no poses has no canvas");
    }
    int left = poses[0].at("x").get<int>();
    int top = poses[0].at("y").get<int>();
    int right = left + poses[0].at("w").get<int>();
    int bottom = top + poses[0].at("h").get<int>();
    for (const Json& pose : poses) {
        const int x = pose.at("x").get<int>();
        const int y = pose.at("y").get<int>();
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x + pose.at("w").get<int>());
        bottom = std::max(bottom, y + pose.at("h").get<int>());
    }
    Rect rect;
    rect.x = left;
    rect.y = top;
    rect.width = right - left;
    rect.height = bottom - top;
    return rect;
}

std::vector<std::uint8_t> compose(const std::string& path, const Json& pose, const Rect& rect) {
    Picture picture = readPng(path);
    if (picture.channels != 4) {
        throw std::runtime_error("pose " + path + " is not RGBA");
    }
    const int poseW = pose.at("w").get<int>();
    const int poseH = pose.at("h").get<int>();
    if (picture.width != poseW || picture.height != poseH) {
        throw std::runtime_error("pose " + path + " does not match the size the timeline gives it");
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(rect.width) * rect.height * 4, 0);
    const int dstX = pose.at("x").get<int>() - rect.x;
    const int dstY = pose.at("y").get<int>() - rect.y;
    for (int row = 0; row < poseH; ++row) {
        const std::size_t start =
            (static_cast<std::size_t>(dstY + row) * rect.width + dstX) * 4;
        std::copy(picture.pixels.begin() + static_cast<std::ptrdiff_t>(row) * poseW * 4,
                  picture.pixels.begin() + static_cast<std::ptrdiff_t>(row + 1) * poseW * 4,
                  out.begin() + static_cast<std::ptrdiff_t>(start));
    }
    return out;
}

void split(const std::vector<std::uint8_t>& rgba, std::vector<std::uint8_t>* rgb,
           std::vector<std::uint8_t>* alpha) {
    const std::size_t count = rgba.size() / 4;
    rgb->assign(count * 3, 0);
    alpha->assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t a = rgba[i * 4 + 3];
        (*alpha)[i] = a;
        // Pose alpha is only ever 0 or 255, so a clear pixel goes to black.
        const std::uint8_t mask = a == 0 ? 0 : 0xFF;
        (*rgb)[i * 3] = rgba[i * 4] & mask;
        (*rgb)[i * 3 + 1] = rgba[i * 4 + 1] & mask;
        (*rgb)[i * 3 + 2] = rgba[i * 4 + 2] & mask;
    }
}

std::vector<std::uint8_t> combine(const std::vector<std::uint8_t>& rgb,
                                  const std::vector<std::uint8_t>& alpha, int cutoff) {
    const std::size_t count = alpha.size();
    std::vector<std::uint8_t> out(count * 4, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const int a = alpha[i];
        std::uint8_t colour[3] = {0, 0, 0};
        if (a >= 255) {
            colour[0] = rgb[i * 3];
            colour[1] = rgb[i * 3 + 1];
            colour[2] = rgb[i * 3 + 2];
        } else if (a >= cutoff) {
            // The colour arrived multiplied by alpha, so divide it back out.
            for (int c = 0; c < 3; ++c) {
                const int value = (rgb[i * 3 + c] * 255 + a / 2) / a;
                colour[c] = static_cast<std::uint8_t>(value > 255 ? 255 : value);
            }
        }
        out[i * 4] = colour[0];
        out[i * 4 + 1] = colour[1];
        out[i * 4 + 2] = colour[2];
        out[i * 4 + 3] = a < cutoff ? 0 : static_cast<std::uint8_t>(a);
    }
    return out;
}

}  // namespace swchess::interp
