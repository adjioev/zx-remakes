#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>

// Paddle geometry decoded from the original sprite mask.
inline constexpr float BB_BALL_RADIUS = 6;
inline constexpr int BB_ROOM_WIDTH = 92;
inline int bb_even_paddle_inset(int width) { return std::min(3, std::max(0, width / 2)); }
// BB1's painted paddle omits three units at its left sprite edge. Constrain
// the physical body, not the sprite anchor, to the room's playable rail.
inline int bb_even_paddle_centre(int centre, int width) {
    width = std::max(1, width);
    const int inset = bb_even_paddle_inset(width);
    // Native projection subtracts half-width in an unsigned byte. Underflow
    // makes its screen blitter overwrite RAM outside the display (the tune).
    return std::clamp(centre, width / 2, BB_ROOM_WIDTH - width + width / 2 + inset);
}
inline float bb_even_paddle_left(int centre, int width) {
    return float(centre - width / 2 - bb_even_paddle_inset(width));
}
struct BBPaddleShape { float dx = 12, dz = 7, h = 12; int left = 0; };
inline BBPaddleShape bb_paddle_shape(const uint8_t* mask) {
    BBPaddleShape b;
    if (!mask) return b;
    auto solid = [&](int x, int y) { return !(mask[y*6 + 3 + x/8] & (0x80 >> (x%8))); };
    int left = 24, right = -1, bottom = -1;
    for (int y = 0; y < 20; ++y) for (int x = 0; x < 24; ++x)
        if (solid(x, y)) left = std::min(left, x), right = std::max(right, x), bottom = std::max(bottom, y);
    if (right < 0) return b;
    int run = 0;
    while (left + run <= right && solid(left + run, bottom)) ++run;
    int top = bottom;
    while (top > 0 && solid(left, top - 1)) --top;
    return {float(right-left+1-run), float(run), float(bottom-top), left};
}
struct BBPaddleBody { float x0, x1, z0, z1, height; };
inline BBPaddleBody bb_paddle_body(float left, float width, float z, BBPaddleShape shape) {
    // BAT_Z positions the original sprite's play-side edge. Subtracting the
    // ball radius moves most of the paddle outside the floor (Z < 0).
    const float face = z;
    return {left, left + width, face - shape.dz, face, shape.h};
}

inline BBPaddleBody bb_visible_paddle_body(float left, float width, float z,
                                          float inset, BBPaddleShape shape = {}) {
    width = width > 0 ? width : shape.dx;
    inset = std::clamp(inset, 0.0f, std::max(0.0f, width - 1));
    return bb_paddle_body(left + inset, width - inset, z, shape);
}

// Even's five native X increments, selected by position on the visible face.
// A half-unit minimum centre tolerance handles integer ball coordinates on a
// very narrow paddle. Grazing hits outside the visible face use the edge angle.
inline int bb_paddle_aim(float ball_x, float visible_left, float visible_width) {
    const float offset = ball_x - (visible_left + visible_width / 2);
    const float distance = std::abs(offset);
    if (distance <= std::max(0.5f, visible_width / 8)) return 0;
    const int magnitude = distance <= visible_width * 3 / 8 ? 1 : 2;
    return offset < 0 ? -magnitude : magnitude;
}
