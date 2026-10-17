// The module of each game, and the default one: the original picture.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "ballbreaker_game.h"
#include "game.h"
#include "music_remix.h"

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

std::unique_ptr<MusicRemix> GameView::make_remix(int) const { return nullptr; }

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

Rectangle GameView::original_rect() const {
    Vector2 c = canvas_size();
    float scale = std::min(c.x / Spectrum::FB_W, c.y / Spectrum::FB_H);
    float w = Spectrum::FB_W * scale, h = Spectrum::FB_H * scale;
    return {(c.x - w) / 2, (c.y - h) / 2, w, h};
}

void OriginalView::draw_original_fit(const Spectrum& s) { draw_original(s, original_rect()); }

void OriginalView::draw(const Spectrum& s, bool, double) { draw_original_fit(s); }

std::unique_ptr<GameView> make_view(GameId id) {
    switch (id) {
    case GameId::BallBreaker: return std::make_unique<BallBreakerView>();
    case GameId::BallBreaker2: return std::make_unique<BallBreakerView>(BALL_BREAKER_2);
    }
    return nullptr;
}

namespace {
std::string state_file(const LoadedGame& game, const std::string& cache_dir) {
    return cache_dir.empty() ? "" : cache_dir + sha1_hex(game.data) + ".state";
}
}  // namespace

bool cached_machine(const LoadedGame& game, const std::string& cache_dir) {
    std::error_code ec;
    return game.variant->format != FileFormat::Z80 && !cache_dir.empty() &&
           std::filesystem::is_regular_file(state_file(game, cache_dir), ec);
}

std::unique_ptr<Spectrum> load_machine(const LoadedGame& game, const std::string& cache_dir,
                                       const Spectrum::TapeProgress& progress) {
    if (game.variant->format == FileFormat::Z80) return Spectrum::from_z80(game.data);
    const std::string file = state_file(game, cache_dir);
    if (!file.empty()) {  // loaded before: its machine as it was then
        std::ifstream in(file, std::ios::binary);
        std::vector<uint8_t> state((std::istreambuf_iterator<char>(in)), {});
        if (auto s = Spectrum::from_state(state)) return s;
    }
    auto s = Spectrum::from_tape(game.data, game.variant->format == FileFormat::Tzx, progress);
    if (s && !file.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(cache_dir, ec);
        std::vector<uint8_t> state = s->save_state();
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(state.data()), std::streamsize(state.size()));
    }
    return s;
}
