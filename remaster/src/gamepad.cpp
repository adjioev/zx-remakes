#include "gamepad.h"

namespace {

constexpr float STICK = 0.5f;  // stick deflection that counts as a d-pad press

}  // namespace

void Pad::update() {
    for (int i = 0; i < COUNT; i++) was_[i] = now_[i];
    id_ = -1;
    for (int g = 0; g < 4; g++)
        if (IsGamepadAvailable(g)) {
            id_ = g;
            break;
        }
    if (id_ < 0) {
        for (bool& b : now_) b = false;
        return;
    }
    auto btn = [&](int b) { return IsGamepadButtonDown(id_, b); };
    float ax = GetGamepadAxisMovement(id_, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(id_, GAMEPAD_AXIS_LEFT_Y);
    now_[LEFT] = btn(GAMEPAD_BUTTON_LEFT_FACE_LEFT) || ax < -STICK;
    now_[RIGHT] = btn(GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax > STICK;
    now_[UP] = btn(GAMEPAD_BUTTON_LEFT_FACE_UP) || ay < -STICK;
    now_[DOWN] = btn(GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ay > STICK;
    now_[A] = btn(GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    now_[B] = btn(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    now_[X] = btn(GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    now_[Y] = btn(GAMEPAD_BUTTON_RIGHT_FACE_UP);
    now_[LB] = btn(GAMEPAD_BUTTON_LEFT_TRIGGER_1);
    now_[RB] = btn(GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
    now_[START] = btn(GAMEPAD_BUTTON_MIDDLE_RIGHT);
    now_[SELECT] = btn(GAMEPAD_BUTTON_MIDDLE_LEFT);
}
