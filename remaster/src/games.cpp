// The module of each game, and the default one: the original picture.

#include <algorithm>

#include "ballbreaker_game.h"
#include "game.h"

void GameView::apply(Spectrum& s, const std::set<int>& keys, uint8_t joy) {
    for (int k : held_)
        if (!keys.count(k)) s.key(k, false);
    for (int k : keys)
        if (!held_.count(k)) s.key(k, true);
    held_ = keys;
    if (joy != joy_) s.joystick(joy_ = joy);
}

void GameView::input(Spectrum& s, const Controls& c) {
    std::set<int> keys = c.keys;
    if (c.pad_b) keys.insert(' ');
    if (c.pad_x) keys.insert(0x0D);
    apply(s, keys, uint8_t(c.arrows | (c.trigger ? ZXC_JOY_FIRE : 0)));
}

std::string GameView::help() const {
    return "Keyboard = the Spectrum's (Shift = CAPS SHIFT, Alt = SYMBOL SHIFT)\n"
           "Arrows + Ctrl, gamepad = Kempston joystick;  gamepad B = Space, X = Enter";
}

OriginalView::OriginalView() : rgba_(new uint32_t[Spectrum::FB_W * Spectrum::FB_H]) {
    Image img = GenImageColor(Spectrum::FB_W, Spectrum::FB_H, BLACK);
    tex_ = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(tex_, TEXTURE_FILTER_POINT);
}

OriginalView::~OriginalView() { UnloadTexture(tex_); }

void OriginalView::draw_original(const Spectrum& s, Rectangle dest) {
    s.picture_rgba(rgba_.get());
    UpdateTexture(tex_, rgba_.get());
    DrawTexturePro(tex_, {0, 0, float(Spectrum::FB_W), float(Spectrum::FB_H)}, dest, {0, 0}, 0, WHITE);
}

void OriginalView::draw_original_fit(const Spectrum& s) {
    Vector2 c = canvas_size();
    float scale = std::min(c.x / Spectrum::FB_W, c.y / Spectrum::FB_H);
    float w = Spectrum::FB_W * scale, h = Spectrum::FB_H * scale;
    draw_original(s, {(c.x - w) / 2, (c.y - h) / 2, w, h});
}

void OriginalView::draw(const Spectrum& s, bool, double) { draw_original_fit(s); }

std::unique_ptr<GameView> make_view(GameId id) {
    switch (id) {
    case GameId::BallBreaker: return std::make_unique<BallBreakerView>();
    }
    return nullptr;
}
