#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace eth::prompts {
// Decode the uncompressed indexed/truecolour TGA formats used by PC font
// descriptors. Output has the same top-left origin as D3DX's texture upload.
inline bool DecodeTga(const uint8_t* p, size_t size, unsigned& w, unsigned& h,
                      std::vector<uint32_t>& pixels)
{
    if (!p || size < 18) return false;
    auto word = [p](size_t n) { return unsigned(p[n]) | unsigned(p[n + 1]) << 8; };
    w = word(12); h = word(14);
    if (!w || !h || w > 2048 || h > 2048 || (p[17] & 0xc0)) return false;
    const bool indexed = p[2] == 1 && p[1] == 1 && p[16] == 8;
    const bool rgb = p[2] == 2 && p[1] == 0 && (p[16] == 24 || p[16] == 32);
    if (!indexed && !rgb) return false;
    unsigned first = word(3), count = word(5), depth = p[7] / 8;
    if (indexed && (!count || first + count > 256 || (p[7] != 24 && p[7] != 32))) return false;
    size_t pos = 18 + p[0], palette = pos;
    if (indexed) pos += count * depth;
    unsigned stride = indexed ? 1 : p[16] / 8;
    if (pos > size || size - pos < size_t(w) * h * stride) return false;
    pixels.resize(size_t(w) * h);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const uint8_t* q = p + pos + (size_t(y) * w + x) * stride;
        unsigned bytes = stride;
        if (indexed) {
            if (*q < first || *q >= first + count) return false;
            q = p + palette + (*q - first) * depth;
            bytes = depth;
        }
        const unsigned dx = (p[17] & 0x10) ? w - x - 1 : x;
        const unsigned dy = (p[17] & 0x20) ? y : h - y - 1;
        pixels[size_t(dy) * w + dx] = uint32_t(q[0]) | uint32_t(q[1]) << 8 |
            uint32_t(q[2]) << 16 | uint32_t(bytes == 4 ? q[3] : 255) << 24;
    }
    return true;
}

// Native control codes, not DirectInput button numbers. 0x1f stays newline.
inline int IconForCode(unsigned code)
{
    if (code >= 1 && code <= 12) return int(code - 1);
    switch (code) {
    case 14: return 12; // directional controls
    case 15: return 0;  // confirm
    case 16: return 2;  // cancel
    case 17: return 13; // Berserka
    case 18: return 15; // Shuriken rage
    case 19: return 16; // I-Ninja rage
    case 20: return 14; // Revive
    default: return -1;
    }
}
}
