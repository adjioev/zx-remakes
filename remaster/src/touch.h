#pragma once

// A finger's gestures (Android): a tap (let go where it touched) and a swipe (a stroke one way). A
// tap counts on letting go, so a swipe that starts on a button does not press it.

#include <algorithm>
#include <cmath>

#include <raylib.h>

struct TouchGesture {
    enum Swipe { NONE, UP, DOWN, LEFT, RIGHT };
    bool tap = false;          // this frame: a tap, at
    Vector2 at{-1, -1};
    Swipe swipe = NONE;        // this frame: a swipe
    bool down = false;         // a finger on the screen now, at now
    Vector2 now{-1, -1};

    void update() {
        tap = false, swipe = NONE;
        const float unit = std::min(GetScreenWidth(), GetScreenHeight()) / 1080.0f;
        const bool touching = GetTouchPointCount() > 0;
        if (touching) {
            now = GetTouchPosition(0);
            if (!down) start_ = now, moved_ = false;
            if (std::hypot(now.x - start_.x, now.y - start_.y) > 40 * unit) moved_ = true;
        } else if (down) {
            const float dx = now.x - start_.x, dy = now.y - start_.y;
            if (!moved_) tap = true, at = now;
            else if (std::max(std::fabs(dx), std::fabs(dy)) > 90 * unit)
                swipe = std::fabs(dy) > std::fabs(dx) ? (dy < 0 ? UP : DOWN) : (dx < 0 ? LEFT : RIGHT);
        }
        down = touching;
    }

private:
    Vector2 start_{};
    bool moved_ = false;
};
