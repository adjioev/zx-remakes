#pragma once

#include <raylib.h>

// First connected gamepad as edge-triggered buttons; the left stick acts as a d-pad.
class Pad {
public:
    enum Btn { LEFT, RIGHT, UP, DOWN, A, B, X, Y, LB, RB, START, SELECT, COUNT };

    void update();
    bool down(Btn b) const { return now_[b]; }
    bool pressed(Btn b) const { return now_[b] && !was_[b]; }
    bool released(Btn b) const { return !now_[b] && was_[b]; }
    bool connected() const { return id_ >= 0; }
    const char* name() const { return id_ >= 0 ? GetGamepadName(id_) : ""; }

private:
    int id_ = -1;
    bool now_[COUNT] = {}, was_[COUNT] = {};
};
