#include "ballbreaker_game.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

// Code that runs once a frame in each part of the game (notes/ballbreaker.md).
constexpr uint16_t MENU_LOOP = 0x9DA7;    // main menu and the control choice
constexpr uint16_t PLAY_LOOP = 0x854C;    // sprite compositor, every frame of play
constexpr uint16_t ENTRY_LOOP = 0x86A5;   // typing a name into the hi-score table
constexpr uint64_t RECENT = 10;           // frames a sighting counts for

// Entry to a complete play pass, after the previous pass's compositor finished.
constexpr uint16_t MAIN_LOOP = 0x8F57, PLAY_EXIT = 0x8BD3;
constexpr uint16_t SERVE_COUNTDOWN = 0x8083;
constexpr uint16_t BALL_CLOCK_READ = 0x9030, BALL_DUE = 0x8074;
constexpr float PLAY_CPU = 16;  // headroom only: yield prevents extra game passes

constexpr uint16_t CONTROL_TYPE = 0x8078;  // 0 keyboard, 1 cursor, 2 Kempston, 3 Sinclair
constexpr uint8_t KEMPSTON = 2;

constexpr uint16_t LEVEL = 0x8070, SCORE = 0x8044, MISSILES = 0x8071, LIVES = 0x807F;
constexpr uint16_t OBJECTS = 0xF3EB, OBJECTS_END = 0x804C;  // 10-byte objects, end pointer
constexpr uint16_t SPRITES = 0xB9D5;
constexpr int LEVELS = 50;

// The room (0-6 = A-G) of each level, 1-based groups as in the notes.
int room_of(int level) {
    static const std::vector<std::vector<int>> ROOMS = {
        {1, 4, 8, 10, 26, 27, 29, 34, 37, 45, 47, 49}, {2, 7, 13, 16, 25, 30, 33, 35, 39, 40, 42, 46, 50},
        {3, 17, 23, 32, 36, 44}, {6, 11, 18, 21, 38, 41}, {5, 12, 22, 31, 43}, {9, 14, 19, 28, 48}, {15, 20, 24}};
    for (size_t r = 0; r < ROOMS.size(); r++)
        for (int l : ROOMS[r])
            if (l == level + 1) return int(r);
    return 0;
}

// Menu keys and the game's own keyboard controls.
constexpr int MENU_UP = 'a', MENU_DOWN = 'z', MENU_SELECT = ' ';

}  // namespace

void BallBreakerView::start(Spectrum& s, const std::string&) {
    s.on_exec(MENU_LOOP, [this](const zxc_regs_t&) { seen_menu_ = frame_ + 1; });
    s.on_exec(PLAY_LOOP, [this](const zxc_regs_t&) { seen_play_ = frame_ + 1; });
    s.on_exec(ENTRY_LOOP, [this](const zxc_regs_t&) { seen_entry_ = frame_ + 1; });
    Spectrum* sp = &s;
    s.on_exec(MAIN_LOOP, [this, sp](const zxc_regs_t&) {
        static const bool off = std::getenv("BB_NO_TURBO") != nullptr;
        passes_++;
        // The serve/death countdown contains HALT and already waits on interrupts.
        // Adding a frame wait there would double those intentional pauses.
        paced_ = !off && sp->peek(SERVE_COUNTDOWN) <= 1;
        sp->set_turbo(paced_ ? PLAY_CPU : 1);
        if (paced_) sp->yield_frame();
    });
    s.on_exec(BALL_CLOCK_READ, [this, sp](const zxc_regs_t&) {
        // Read-side override: the original video interrupt still runs audio and
        // countdowns, but its ~49.86 Hz clock must not drift against our 50 Hz
        // simulation. Permit a ball update every other simulation frame. Doing
        // this immediately before LD A,(8074) avoids an ISR overwriting the flag.
        if (paced_) sp->poke(BALL_DUE, (frame_ % 2) == 0 ? 1 : 0);
    });
    s.on_exec(PLAY_EXIT, [this, sp](const zxc_regs_t&) {
        paced_ = false;
        sp->set_turbo(1);
    });
}

void BallBreakerView::after_frame(Spectrum& s) {
    auto recent = [&](uint64_t seen) { return seen && frame_ + 1 - seen < RECENT; };
    uint64_t latest = std::max({seen_menu_, seen_play_, seen_entry_});
    if (latest && recent(latest)) {
        if (latest == seen_entry_) mode_ = Mode::NameEntry;
        else if (latest == seen_menu_) mode_ = Mode::Menu;
        else mode_ = Mode::Play;
    } else if (mode_ != Mode::Boot) {
        mode_ = Mode::Play;  // between the menu and play: "SCREEN:1", the room sliding in
    }
    // the arrows are the Kempston joystick: keep the game on it (choosing another in its
    // menu would leave the player without controls)
    if (mode_ == Mode::Menu && s.peek(CONTROL_TYPE) != KEMPSTON) s.poke(CONTROL_TYPE, KEMPSTON);
    frame_++;
    game_t_ = double(frame_) / Spectrum::FRAME_HZ;
    if (mode_ == Mode::Play) smooth_sample(s);
    else tracks_.clear();
    if (std::getenv("BB_TURBO_LOG"))
        std::fprintf(stderr, "frame %llu paced %d passes %llu ball %u,%u score %u lives %u\n",
                     (unsigned long long)frame_, paced_, (unsigned long long)passes_,
                     s.peek(0x8064), s.peek(0x8065), s.peek16(SCORE), s.peek(LIVES));
}

void BallBreakerView::input(Spectrum& s, const Controls& c) {
    std::set<int> keys;
    uint8_t joy = 0;
    switch (mode_) {
    case Mode::Boot:  // the loading screen waits for any key
        if (c.any()) keys.insert(' ');
        break;
    case Mode::Menu:
        if (c.dirs & ZXC_JOY_UP) keys.insert(MENU_UP);
        if (c.dirs & ZXC_JOY_DOWN) keys.insert(MENU_DOWN);
        if (c.ok || c.fire) keys.insert(MENU_SELECT);
        break;
    case Mode::NameEntry:  // the keyboard as it is; the gamepad can only finish
        keys = c.keys;
        if (c.ok && !c.keys.count(' ')) keys.insert(0x0D);
        break;
    case Mode::Play:
        // the bat runs along the right-hand edge, lower left to upper right: up is right
        if (c.dirs & (ZXC_JOY_LEFT | ZXC_JOY_DOWN)) joy |= ZXC_JOY_LEFT;
        if (c.dirs & (ZXC_JOY_RIGHT | ZXC_JOY_UP)) joy |= ZXC_JOY_RIGHT;
        if (c.fire) joy |= ZXC_JOY_FIRE;
        break;
    }
    apply(s, keys, joy);
}

std::string BallBreakerView::help() const {
    return "Arrows / WASD / gamepad: move the bat (and choose in the menus)\n"
           "Space / Ctrl / gamepad A: fire a missile;  Enter / Space: select\n"
           "Hi-score: type your name, Enter";
}

BBState BallBreakerView::read_state(const Spectrum& s) const {
    BBState st;
    st.level = s.peek(LEVEL) % LEVELS;
    st.room = room_of(st.level);
    st.score = s.peek16(SCORE) * 100;
    st.missiles = s.peek(MISSILES);
    st.lives = s.peek(LIVES);
    uint16_t end = s.peek16(OBJECTS_END);
    int slot = 0;
    for (uint16_t a = OBJECTS; a + 10 <= end && slot < 200; a += 10, slot++) {
        BBObject o{float(s.peek(uint16_t(a + 1))), float(s.peek(uint16_t(a + 2))), s.peek(uint16_t(a + 3)),
                   float(s.peek(uint16_t(a + 7))), float(s.peek(uint16_t(a + 8))), float(s.peek(uint16_t(a + 9))), slot};
        if (o.sx == 0) continue;  // a free slot or a brick that is gone
        st.objects.push_back(o);
    }
    return st;
}

bool BallBreakerView::hd_now(bool original) const {
    return !original && mode_ == Mode::Play && seen_play_ && frame_ + 1 - seen_play_ < RECENT;
}

void BallBreakerView::prepare(const Spectrum& s, bool original, double t) {
    if (!hd_now(original)) return;
    if (!hd_) hd_ = std::make_unique<BBRenderer>();
    if (!hd_->has_sprites()) {
        std::vector<uint8_t> data(60 * 120);
        for (size_t i = 0; i < data.size(); i++) data[i] = s.peek(uint16_t(SPRITES + i));
        hd_->load_sprites(data.data());
    }
    for (const auto& [o, points] : bursts_) hd_->burst(o, points);
    bursts_.clear();
    state_ = latest_.objects.empty() ? read_state(s) : latest_;
    smooth_apply(state_, t);
    hd_->prepare(state_, t);
    if (const char* d = std::getenv("BB_GLOW_DUMP")) hd_->debug_dump(d);
}

void BallBreakerView::draw(const Spectrum& s, bool original, double t) {
    if (hd_now(original) && hd_) hd_->draw(state_, t);
    else draw_original_fit(s);
}

namespace {
constexpr double RENDER_DELAY = 0.04;
constexpr float TELEPORT = 24;  // playfield pixels: bigger jumps are taken at once
}  // namespace

void BallBreakerView::track_pos(const Track& tr, double t, float* out) {
    const auto& samples = tr.samples;
    const Sample* left = &samples.front();
    const Sample* right = left;
    for (const auto& sample : samples) {
        right = &sample;
        if (sample.t >= t) break;
        left = right;
    }
    double span = right->t - left->t;
    float a = span > 0 ? float(std::clamp((t - left->t) / span, 0.0, 1.0)) : 0;
    for (int i = 0; i < 5; i++) out[i] = left->pos[i] + (right->pos[i] - left->pos[i]) * a;
}

void BallBreakerView::smooth_sample(const Spectrum& s) {
    BBState before = std::move(latest_);
    latest_ = read_state(s);
    // bricks hit since the last frame: the game turns them into its explosion (sprites 0x19-0x1F)
    // or drops them; effects for those (not when a whole level goes or comes)
    auto exploding = [](int sprite) { return sprite >= 0x19 && sprite <= 0x1f; };
    if (!before.objects.empty() && before.level == latest_.level) {
        std::vector<BBObject> hit;
        for (const BBObject& o : before.objects) {
            if (o.slot < 3 || exploding(o.sprite)) continue;
            const BBObject* now = nullptr;
            for (const BBObject& n : latest_.objects)
                if (n.slot == o.slot) now = &n;
            if (!now || exploding(now->sprite)) hit.push_back(o);
        }
        // the score goes up a frame or two before the explosion starts
        if (latest_.score > before.score) gain_ += latest_.score - before.score, gain_t_ = game_t_;
        if (game_t_ - gain_t_ > 0.3) gain_ = 0;
        int points = hit.empty() ? 0 : gain_ / int(hit.size());
        if (!hit.empty()) gain_ = 0;
        if (hit.size() <= 6)
            for (const BBObject& o : hit) bursts_.push_back({o, points});
    }
    if (before.level != latest_.level) tracks_.clear();
    double t = game_t_;
    std::map<int, Track> next;
    for (const BBObject& o : latest_.objects) {
        float v[5] = {o.sx, o.sy, o.x, o.y, o.z};
        Track tr;
        if (auto it = tracks_.find(o.slot); it != tracks_.end()) tr = it->second;
        bool moved = tr.samples.empty();
        if (!tr.samples.empty()) {
            const auto& last = tr.samples.back();
            bool teleport = std::abs(v[0] - last.pos[0]) > TELEPORT ||
                            std::abs(v[1] - last.pos[1]) > TELEPORT;
            for (int i = 0; i < 5; i++) moved |= v[i] != last.pos[i];
            if (teleport) tr.samples.clear();
            // After a rest, keep a stationary endpoint immediately before movement.
            else if (moved && t - last.t > RENDER_DELAY + 0.001) {
                Sample rest = last;
                rest.t = t - 1.0 / Spectrum::FRAME_HZ;
                tr.samples.push_back(rest);
            }
        }
        if (moved) {
            Sample sample{t, {v[0], v[1], v[2], v[3], v[4]}};
            tr.samples.push_back(sample);
        }
        while (tr.samples.size() > 2 && tr.samples[1].t <= t - RENDER_DELAY)
            tr.samples.pop_front();
        next[o.slot] = std::move(tr);
    }
    tracks_ = std::move(next);
}

void BallBreakerView::smooth_apply(BBState& st, double t) const {
    for (BBObject& o : st.objects) {
        auto it = tracks_.find(o.slot);
        if (it == tracks_.end()) continue;
        float v[5];
        track_pos(it->second, t - RENDER_DELAY, v);
        o.sx = v[0], o.sy = v[1], o.x = v[2], o.y = v[3], o.z = v[4];
    }
}
