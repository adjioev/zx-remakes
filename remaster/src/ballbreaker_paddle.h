#pragma once

#include <algorithm>
#include <cstdint>

// Paddle geometry decoded from the original sprite mask.
inline constexpr float BB_BALL_RADIUS = 6;
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
