// Places poses on the shared canvas and moves between RGBA and RIFE's inputs.
//
// This is the C++ port of tools/interp/images.py. RIFE reads three colour
// channels and throws alpha away, so each pose leaves here as two pictures.
// One holds the colour over black, which is the colour multiplied by alpha.
// The other holds the alpha channel on its own as grey. Recombining divides
// the colour back out by the interpolated alpha and clears any pixel whose
// alpha falls under the cutoff.
#pragma once

#include <cstdint>
#include <vector>

#include "export/json_write.h"

namespace swchess::interp {

using swchess::exporter::Json;

// A pixel this faint reads as a smear rather than as part of the sprite.
constexpr int kAlphaCutoff = 16;

// The rectangle every pose sits inside, in game canvas coordinates.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The smallest rectangle covering every pose rectangle in `poses`.
Rect unionRect(const Json& poses);

// Reads one pose PNG and places it on the padded canvas. Returns straight
// RGBA. Throws when the file is not RGBA or is not the size the timeline
// claims.
std::vector<std::uint8_t> compose(const std::string& path, const Json& pose, const Rect& rect);

// Splits straight RGBA into the colour over black and the alpha channel.
void split(const std::vector<std::uint8_t>& rgba, std::vector<std::uint8_t>* rgb,
           std::vector<std::uint8_t>* alpha);

// Rebuilds straight RGBA from an interpolated colour and alpha pair.
std::vector<std::uint8_t> combine(const std::vector<std::uint8_t>& rgb,
                                  const std::vector<std::uint8_t>& alpha, int cutoff);

}  // namespace swchess::interp
