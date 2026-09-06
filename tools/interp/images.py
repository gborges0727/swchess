"""Compose poses onto the padded canvas and move between RGBA and RIFE inputs.

RIFE reads three colour channels and drops alpha, so each pose leaves here as
two pictures: the colour over black, which is the colour premultiplied by alpha,
and the alpha channel on its own as grey. Recombining divides the colour back by
the interpolated alpha and drops any pixel whose alpha falls under a cutoff.
"""

from . import png

ALPHA_CUTOFF = 16

_OPAQUE = bytes(0 if i == 0 else 255 for i in range(256))


def union_rect(poses):
    """Return (x, y, w, h) covering every pose rectangle."""
    left = min(p["x"] for p in poses)
    top = min(p["y"] for p in poses)
    right = max(p["x"] + p["w"] for p in poses)
    bottom = max(p["y"] + p["h"] for p in poses)
    return left, top, right - left, bottom - top


def compose(path, pose, rect):
    """Place one pose PNG on the padded canvas and return straight RGBA bytes."""
    x0, y0, width, height = rect
    src_w, src_h, channels, pixels = png.read(path)
    if channels != 4:
        raise ValueError("pose %s is not RGBA" % path)
    if (src_w, src_h) != (pose["w"], pose["h"]):
        raise ValueError("pose %s is %dx%d, the timeline says %dx%d"
                         % (path, src_w, src_h, pose["w"], pose["h"]))
    out = bytearray(width * height * 4)
    dst_x = pose["x"] - x0
    dst_y = pose["y"] - y0
    for row in range(src_h):
        start = ((dst_y + row) * width + dst_x) * 4
        out[start:start + src_w * 4] = pixels[row * src_w * 4:(row + 1) * src_w * 4]
    return out


def split(rgba):
    """Return (colour over black, alpha) as two byte strings.

    The colour is the RGB channels with every transparent pixel forced to black,
    which is what compositing straight-alpha colour over black produces. Pose
    alpha is either 0 or 255, so an AND with the alpha mask does the whole job.
    """
    data = bytes(rgba)
    count = len(data) // 4
    alpha = data[3::4]
    rgb = bytearray(count * 3)
    rgb[0::3] = data[0::4]
    rgb[1::3] = data[1::4]
    rgb[2::3] = data[2::4]
    mask = bytearray(count * 3)
    opaque = alpha.translate(_OPAQUE)
    mask[0::3] = opaque
    mask[1::3] = opaque
    mask[2::3] = opaque
    masked = int.from_bytes(bytes(rgb), "big") & int.from_bytes(bytes(mask), "big")
    return masked.to_bytes(count * 3, "big"), alpha


def combine(rgb, alpha, cutoff=ALPHA_CUTOFF):
    """Rebuild straight RGBA from an interpolated colour and alpha pair.

    Pixels at alpha 255 keep their colour untouched. Pixels under `cutoff`
    become fully transparent black. The partly transparent edge pixels, the only
    ones that need a division, are found with bytes.find so the Python loop runs
    over the edge alone.
    """
    count = len(alpha)
    solid = bytes(255 if v >= 255 else 0 for v in range(256))
    edge = bytes(255 if cutoff <= v < 255 else 0 for v in range(256))
    keep = alpha.translate(solid)
    mask = bytearray(count * 3)
    mask[0::3] = keep
    mask[1::3] = keep
    mask[2::3] = keep
    kept = int.from_bytes(bytes(rgb), "big") & int.from_bytes(bytes(mask), "big")
    colour = bytearray(kept.to_bytes(count * 3, "big"))

    edges = alpha.translate(edge)
    pos = edges.find(255)
    while pos >= 0:
        a = alpha[pos]
        base = pos * 3
        for c in range(3):
            value = (rgb[base + c] * 255 + a // 2) // a
            colour[base + c] = 255 if value > 255 else value
        pos = edges.find(255, pos + 1)

    out = bytearray(count * 4)
    out[0::4] = colour[0::3]
    out[1::4] = colour[1::3]
    out[2::4] = colour[2::3]
    out[3::4] = alpha.translate(bytes(0 if v < cutoff else v for v in range(256)))
    return out


def rows(buffer, width, channels):
    """Slice a flat pixel buffer into rows for the PNG writer."""
    stride = width * channels
    return [buffer[i:i + stride] for i in range(0, len(buffer), stride)]
