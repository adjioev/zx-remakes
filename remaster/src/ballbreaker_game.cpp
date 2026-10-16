#include "ballbreaker_game.h"
#include "music_remix.h"

#include <algorithm>

#include <rlgl.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

// Code that runs once a frame in each part of the game (notes/ballbreaker.md).
constexpr uint16_t MENU_LOOP = 0x9DA7;    // main menu and the control choice
constexpr uint16_t PLAY_LOOP = 0x854C;    // sprite compositor, every frame of play
constexpr uint16_t ENTRY_LOOP = 0xABC0;   // typing a name into the hi-score table (its key loop)
constexpr uint16_t PAGE_LOOP = 0x86A5;    // a page up: hi-scores, instructions (and the name entry)
constexpr uint64_t LEAVE_FRAMES = 45;     // the menu's wipe after PLAY GAME (the original's, ~0.9 s)
constexpr uint64_t RECENT = 10;           // frames a sighting counts for
constexpr uint16_t CTRL_LOOP = 0x990B;    // the control choice (REDEFINE KEYS)
constexpr uint16_t KEYS_LOOP = 0xA086;    // LIST KEYS, waiting for a key
constexpr uint16_t MENU_CURSOR = 0xFFF8;  // the item under the menu's bar (what ENTER takes): 0-5,
                                          // the menu's own variable on its stack (0x8077 is its arrow,
                                          // which can be an item ahead)
constexpr uint16_t HISCORES = 0x80CB;     // 9 x [12 name, word score / 100]
constexpr uint16_t INSTRUCTIONS = 0xB228; // [sprite][text, last char + 0x80, '*' new line] .. 0xFF
constexpr uint16_t POINTS = 0xB893;       // 3 bytes a sprite: points / 100 first

// Entry to a complete play pass, after the previous pass's compositor finished.
constexpr uint16_t MAIN_LOOP = 0x8F57, PLAY_EXIT = 0x8BD3;
constexpr uint16_t PASS_END = 0x9138;     // a play pass's end: LD A,(0x806C), loop again if 0
constexpr uint16_t PLAY_REASON = 0x806C;  // why play stops: 0 on, 2 ball lost, 3 restart, 4 quit; else next level
constexpr double CLEARED_WAIT = 1.0;      // s after the last explosion before the next level (the shards fall)
constexpr uint16_t SERVE_COUNTDOWN = 0x8083;
constexpr uint16_t TUNE_START = 0xA73B;   // the music player: the tune from the top (its channels' data)
constexpr uint16_t LEVEL_START = 0x8B78;  // the game's own start of a level (at the top: SP = 0)
constexpr uint16_t BALL_CLOCK_READ = 0x9030, BALL_DUE = 0x8074;
constexpr float PLAY_CPU = 16;  // headroom only: yield prevents extra game passes

constexpr uint16_t CONTROL_TYPE = 0x8078;  // 0 keyboard, 1 cursor, 2 Kempston, 3 Sinclair
constexpr uint8_t KEMPSTON = 2;

constexpr uint16_t LEVEL = 0x8070, SCORE = 0x8044, MISSILES = 0x8071, LIVES = 0x807F;
constexpr uint16_t OBJECTS = 0xF3EB, OBJECTS_END = 0x804C;  // 10-byte objects, end pointer
constexpr uint16_t SPRITES = 0xB9D5;
constexpr int LEVELS = 50;
constexpr uint8_t START_LIVES = 3, START_MISSILES = 10;  // what a new game gives (0x8B5C)

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

void BallBreakerView::start(Spectrum& s, const std::string& assets) {
    fonts_dir_ = assets + "/../fonts";
    intro_path_ = assets + "/intro.png";
    creatures_path_ = assets + "/creatures/frames.png";
    s.on_exec(CTRL_LOOP, [this](const zxc_regs_t&) { seen_ctrl_ = frame_ + 1; });
    s.on_exec(KEYS_LOOP, [this](const zxc_regs_t&) { seen_keys_ = frame_ + 1; });
    s.on_exec(PAGE_LOOP, [this](const zxc_regs_t&) { seen_page_ = frame_ + 1; });
    s.on_exec(MENU_LOOP, [this](const zxc_regs_t&) { seen_menu_ = frame_ + 1; });
    s.on_exec(PLAY_LOOP, [this](const zxc_regs_t&) { seen_play_ = frame_ + 1; });
    s.on_exec(ENTRY_LOOP, [this](const zxc_regs_t&) { seen_entry_ = frame_ + 1; });
    Spectrum* sp = &s;
    s.on_exec(MAIN_LOOP, [this, sp](const zxc_regs_t&) {
        if (pending_level_ >= 0) {  // the jump (jump_to): the level number, then the level start
            sp->poke(LEVEL, uint8_t(pending_level_));
            pending_level_ = -1;
            paced_ = false;
            sp->set_turbo(1);
            return;
        }
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
    // The tune: the game starts it over (the menu); with the remastered sound a new game does too
    // (the original plays on from the menu), as the player's own start does: the channels' data
    // from the top, their counters cleared. The player runs in the interrupt: never half done here.
    s.on_exec(TUNE_START, [this](const zxc_regs_t&) { music_restarted_ = true; });
    // The level cleared (the remake): at the end of a play pass the game is told to stop with a reason
    // that is none of its own (2 ball lost, 3 restart, 4 quit): it then goes on to the next level
    // (0x8C0B), as the original does - which waits for the ball to fall first.
    s.on_exec(PASS_END, [this, sp](const zxc_regs_t&) {
        if (!advance_) return;
        advance_ = false;
        sp->poke(PLAY_REASON, 1);
    });
    s.on_exec(LEVEL_START, [this, sp](const zxc_regs_t&) {
        if (!remake_ || sp->peek16(SCORE) != 0 || sp->peek(LEVEL) != 0) return;
        sp->poke16(0x80B3, 0x5E96), sp->poke16(0x80B5, 0x5FFC), sp->poke16(0x80B7, 0x6143);
        for (uint16_t a : {0x80AF, 0x80B0, 0x80B1, 0x5B21, 0x5B25, 0x5B29, 0x80C7, 0x80C8, 0x80C9}) sp->poke(a, 0);
        sp->poke(0x80AE, 0xE0), sp->poke16(0x8096, 0x0707), sp->poke(0x80C6, 0xF0);
        music_restarted_ = true;
    });
    s.on_exec(PLAY_EXIT, [this, sp](const zxc_regs_t&) {
        paced_ = false;
        sp->set_turbo(1);
    });
}

void BallBreakerView::after_frame(Spectrum& s) {
    auto recent = [&](uint64_t seen) { return seen && frame_ + 1 - seen < RECENT; };
    using Sc = BBMenu::Screen;
    Mode before = mode_;
    uint64_t latest = std::max({seen_menu_, seen_play_, seen_ctrl_, seen_keys_});
    // PLAY GAME taken: the original wipes the menu away (passing through the menu's code again)
    // before the level's card; that is still the way out, not the menu back
    bool leaving = left_menu_ && frame_ + 1 - left_menu_ < LEAVE_FRAMES;
    if (leaving && latest == seen_menu_) latest = std::max({seen_play_, seen_ctrl_, seen_keys_});
    if (recent(seen_entry_)) mode_ = Mode::NameEntry, screen_ = Sc::HiScores;
    else if (leaving && !(latest && recent(latest))) mode_ = Mode::Play, screen_ = Sc::LevelCard;
    else if (latest && recent(latest)) {
        if (latest == seen_menu_) {
            mode_ = Mode::Menu, screen_ = Sc::Main;
            // (it is at the stack's top: as ENTER is taken the game's calls write over it - those
            // values are no item, the cursor stays where it was)
            if (int c = s.peek(MENU_CURSOR); c <= 5) cursor_ = c;
        } else if (latest == seen_ctrl_) mode_ = Mode::Menu, screen_ = Sc::Controls;  // same keys as the menu
        else if (latest == seen_keys_) mode_ = Mode::Screen, screen_ = Sc::Keys;
        else mode_ = Mode::Play, screen_ = Sc::None;
    } else if (mode_ != Mode::Boot) {
        // none of the loops: a page the menu opened, the hi-scores after a game, or the level intro
        if (screen_ == Sc::Main) {
            const Sc pages[6] = {Sc::LevelCard, Sc::Controls, Sc::Keys, Sc::HiScores, Sc::Instructions, Sc::Main};
            screen_ = pages[cursor_];
            if (cursor_ == 0) left_menu_ = frame_ + 1;
        } else if (before == Mode::Play && screen_ == Sc::None)
            screen_ = s.peek(LIVES) == 0 ? Sc::HiScores : Sc::LevelCard;
        mode_ = screen_ == Sc::LevelCard || screen_ == Sc::None ? Mode::Play : Mode::Screen;
    }
    if (screen_ != last_screen_) last_screen_ = screen_, screen_since_ = frame_;
    static const bool mode_log = std::getenv("BB_MODE_LOG") != nullptr;
    static int last_logged = -1;
    int sig = int(mode_) * 100 + int(screen_) * 10 + cursor_;
    if (mode_log && sig != last_logged) {
        std::fprintf(stderr, "frame %llu mode %d screen %d cursor %d (8077=%d)\n", (unsigned long long)frame_, int(mode_),
                     int(screen_), cursor_, s.peek(MENU_CURSOR));
        last_logged = sig;
    }
    // the arrows are the Kempston joystick: keep the game on it (choosing another in its
    // menu would leave the player without controls)
    if (mode_ == Mode::Menu && s.peek(CONTROL_TYPE) != KEMPSTON) s.poke(CONTROL_TYPE, KEMPSTON);
    // options: the counters kept topped up (a life is lost from 3 to 2, back to 3: never 0)
    if (mode_ == Mode::Play && s.peek(LIVES) < lives_)
        sfx_.push_back({Sfx::LifeLost, 0, 0.9f});  // (unlimited: lost, then given back)
    lives_ = s.peek(LIVES);
    if (mode_ == Mode::Play) {
        if (unlimited_lives_ && s.peek(LIVES) < START_LIVES) s.poke(LIVES, START_LIVES);
        if (unlimited_missiles_ && s.peek(MISSILES) < START_MISSILES) s.poke(MISSILES, START_MISSILES);
    }
    frame_++;
    game_t_ = double(frame_) / Spectrum::FRAME_HZ;
    if (mode_ == Mode::Play) smooth_sample(s), check_cleared();
    else tracks_.clear();
    if (std::getenv("BB_TURBO_LOG"))
        std::fprintf(stderr, "frame %llu paced %d passes %llu ball %u,%u score %u lives %u\n",
                     (unsigned long long)frame_, paced_, (unsigned long long)passes_,
                     s.peek(0x8064), s.peek(0x8065), s.peek16(SCORE), s.peek(LIVES));
}

void BallBreakerView::input(Spectrum& s, const Controls& c) {
    std::set<int> keys;
    uint8_t joy = 0;
    // a key that chose a menu item is still down when the page it opens comes up: it must not
    // count there (it would close the page, or choose again). Nothing until all is let go.
    if (screen_ != input_screen_) {
        input_screen_ = screen_;
        wait_release_ = screen_ != BBMenu::Screen::None;
    }
    if (wait_release_) {
        if (c.any()) {
            apply(s, keys, joy);
            return;
        }
        wait_release_ = false;
    }
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
    case Mode::Screen:  // a page waiting for any key
        if (c.ok || c.fire || c.dirs) keys.insert(' ');
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
    return std::string("Arrows / WASD / gamepad: move the bat (and choose in the menus)\n"
                       "Space / Ctrl / gamepad A: fire a missile;  Enter / Space: select\n"
                       "Hi-score: type your name, Enter\n") +
           "L  unlimited lives: " + (unlimited_lives_ ? "ON" : "off") + "      K  unlimited missiles: " +
           (unlimited_missiles_ ? "ON" : "off");
}

std::string BallBreakerView::on_key(Spectrum&, int key) {
    if (key == KEY_L) {
        unlimited_lives_ = !unlimited_lives_;
        return unlimited_lives_ ? "UNLIMITED LIVES: ON" : "UNLIMITED LIVES: OFF";
    }
    if (key == KEY_K) {
        unlimited_missiles_ = !unlimited_missiles_;
        return unlimited_missiles_ ? "UNLIMITED MISSILES: ON" : "UNLIMITED MISSILES: OFF";
    }
    return "";
}

BBState BallBreakerView::read_state(const Spectrum& s) const {
    BBState st;
    st.level = s.peek(LEVEL) % LEVELS;
    st.room = room_of(st.level);
    st.score = s.peek16(SCORE) * 100;
    st.missiles = s.peek(MISSILES);
    st.lives = s.peek(LIVES);
    st.unlimited_lives = unlimited_lives_, st.unlimited_missiles = unlimited_missiles_;
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
    if (original) return false;
    if (mode_ == Mode::Boot) return FileExists(intro_path_.c_str());  // the title picture, if made
    bool playing = mode_ == Mode::Play && seen_play_ && frame_ + 1 - seen_play_ < RECENT;
    return playing || screen_ != BBMenu::Screen::None;
}

void BallBreakerView::read_menu(const Spectrum& s, BBMenu& m) const {
    m.screen = mode_ == Mode::Boot ? BBMenu::Screen::Loading : screen_;
    if (mode_ == Mode::Play && seen_play_ && frame_ + 1 - seen_play_ < RECENT) m.screen = BBMenu::Screen::None;
    m.cursor = cursor_;
    m.since = double(frame_ - screen_since_) / Spectrum::FRAME_HZ;
    m.leave = left_menu_ && m.screen == BBMenu::Screen::LevelCard ? std::min(1.0f, float(frame_ + 1 - left_menu_) / LEAVE_FRAMES) : 1.0f;
    for (int i = 0; i < 9; i++) {
        uint16_t a = uint16_t(HISCORES + i * 14);
        std::string name;
        for (int k = 0; k < 12; k++) {
            uint8_t c = s.peek(uint16_t(a + k));
            name += c >= 32 && c < 127 ? char(c) : ' ';
        }
        while (!name.empty() && name.back() == ' ') name.pop_back();
        m.scores.push_back({name, s.peek16(uint16_t(a + 12)) * 100});
    }
    if (mode_ == Mode::NameEntry) {  // the row being typed into: the one with this game's score
        int score = s.peek16(SCORE) * 100;
        for (int i = 0; i < 9; i++)
            if (m.scores[size_t(i)].score == score) {
                m.entry_row = i;
                break;
            }
    }
    uint16_t a = INSTRUCTIONS;
    for (int n = 0; n < 64; n++) {
        uint8_t sprite = s.peek(a++);
        if (sprite == 0xFF) break;
        std::string text;
        for (int k = 0; k < 80; k++) {
            uint8_t c = s.peek(a++);
            char ch = char(c & 0x7F);
            if (ch != ' ' || !text.empty()) text += ch;
            if (c & 0x80) break;
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        m.items.push_back({sprite, text, s.peek(uint16_t(POINTS + sprite * 3)) * 100});
    }
}

void BallBreakerView::prepare(const Spectrum& s, bool original, double t) {
    if (!hd_now(original)) return;
    if (!hd_) hd_ = std::make_unique<BBRenderer>(fonts_dir_, intro_path_, creatures_path_);
    if (!hd_->has_sprites()) {
        std::vector<uint8_t> data(60 * 120);
        for (size_t i = 0; i < data.size(); i++) data[i] = s.peek(uint16_t(SPRITES + i));
        hd_->load_sprites(data.data());
    }
    for (const auto& [o, points] : bursts_) hd_->burst(o, points);
    bursts_.clear();
    static const bool stress = std::getenv("BB_STRESS") != nullptr;  // every brick bursts at once (perf check)
    static bool stressed = false;
    if (stress && !stressed && !latest_.objects.empty()) {
        for (const BBObject& o : latest_.objects)
            if (o.slot >= 3) hd_->burst(o, 100);
        stressed = true;
    }
    perf_t0_ = GetTime();
    state_ = latest_.objects.empty() || mode_ != Mode::Play ? read_state(s) : latest_;
    read_menu(s, state_.menu);
    if (state_.menu.screen != BBMenu::Screen::None && state_.menu.screen != BBMenu::Screen::LevelCard)
        state_.objects.clear();  // the menus: no play objects behind them
    else
        smooth_apply(state_, t);
    hd_->prepare(state_, t);
    if (const char* d = std::getenv("BB_GLOW_DUMP")) hd_->debug_dump(d);
}

void BallBreakerView::draw(const Spectrum& s, bool original, double t) {
    if (hd_now(original) && hd_) {
        hd_->draw(state_, t);
        static const bool perf = std::getenv("BB_PERF") != nullptr;  // CPU time of a HD frame
        if (perf) {
            rlDrawRenderBatchActive();
            std::fprintf(stderr, "hd frame %.2f ms\n", (GetTime() - perf_t0_) * 1000);
        }
    } else
        draw_original_fit(s);
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
    size_t bursts_before = bursts_.size();
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
    sounds(before, bursts_before);
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

void BallBreakerView::sounds(const BBState& before, size_t bursts_before) {
    auto pan = [](const BBObject& o) { return std::clamp((o.sx + 12) / 128.0f - 1, -1.0f, 1.0f) * 0.7f; };
    auto find = [](const BBState& st, int slot) -> const BBObject* {
        for (const BBObject& o : st.objects)
            if (o.slot == slot) return &o;
        return nullptr;
    };
    if (!before.objects.empty() && before.level != latest_.level) sfx_.push_back({Sfx::LevelStart, 0, 0.8f});
    // bricks: a crunch with a note, higher for more points; a bomb booms
    if (bursts_.size() > bursts_before) heard_.brick_t = game_t_;
    for (size_t i = bursts_before; i < bursts_.size(); i++) {
        const auto& [o, points] = bursts_[i];
        static const int STEPS[6] = {0, 3, 5, 7, 10, 12};  // A minor pentatonic
        int pitch = STEPS[std::clamp(points / 100 - 1, 0, 5)];
        sfx_.push_back({o.sprite == 0x06 ? Sfx::Bomb : Sfx::Brick, pan(o), 1, pitch});
    }
    if (const BBObject* m = find(latest_, 2); m && !find(before, 2)) sfx_.push_back({Sfx::Missile, pan(*m), 0.8f});
    // the ball: a turn in its path is a bounce (off the bat: near it), coming down on the floor a landing
    const BBObject* b = find(latest_, 0);
    if (!b) {
        heard_.have = false;
        return;
    }
    float h = (100 - b->x / 2.0f) - b->sy;
    if (std::getenv("BB_BALL_LOG") && (b->x != heard_.x || b->z != heard_.z)) {
        const BBObject* bat = find(latest_, 1);
        std::fprintf(stderr, "t %.2f ball x %.0f y %.0f z %.0f h %.1f bat x %.0f z %.0f\n", game_t_, b->x, b->y, b->z, h,
                     bat ? bat->x : -1, bat ? bat->z : -1);
    }
    if (heard_.have && (b->x != heard_.x || b->z != heard_.z)) {
        float dx = b->x - heard_.x, dz = b->z - heard_.z;
        if (std::abs(dx) > 12 || std::abs(dz) > 12) heard_.dx = heard_.dz = 0;  // a jump: a new ball
        else {
            int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sz = dz > 0 ? 1 : dz < 0 ? -1 : 0;
            bool turned = (sx && heard_.dx && sx != heard_.dx) || (sz && heard_.dz && sz != heard_.dz);
            if (turned && game_t_ - heard_.hit_t > 0.1) {
                heard_.hit_t = game_t_;
                const BBObject* bat = find(latest_, 1);
                bool off_bat = bat && b->z < 20 && heard_.dz < 0 && sz > 0;  // back from the near edge: the bat
                if (off_bat) sfx_.push_back({Sfx::BatHit, pan(*b), 0.9f});
                else if (game_t_ - heard_.brick_t > 0.12) sfx_.push_back({Sfx::Bounce, pan(*b), 0.8f});
            }
            if (sx) heard_.dx = sx;
            if (sz) heard_.dz = sz;
        }
    }
    if (heard_.have && heard_.h > 3 && h <= 1.2f && game_t_ - heard_.land_t > 0.2) {
        heard_.land_t = game_t_;
        sfx_.push_back({Sfx::Land, pan(*b), 0.6f});
    }
    heard_.have = true, heard_.x = b->x, heard_.z = b->z, heard_.h = h;
}

std::unique_ptr<MusicRemix> BallBreakerView::make_remix(int sample_rate) const {
    return std::make_unique<MusicRemix>(sample_rate);
}

void BallBreakerView::take_sfx(std::vector<SfxEvent>& out) {
    if (mode_ == Mode::Play) out.insert(out.end(), sfx_.begin(), sfx_.end());
    sfx_.clear();
}

void BallBreakerView::check_cleared() {
    // the remake: no breakable brick left, the last one's explosion over (and its shards fallen):
    // on to the next level, without waiting for the ball to fall
    if (latest_.level != cleared_level_) cleared_level_ = latest_.level, had_bricks_ = false, cleared_t_ = -1;
    int left = 0;
    bool exploding = false;
    for (const BBObject& o : latest_.objects) {
        if (o.slot < 3) continue;
        if (o.sprite >= 0x19 && o.sprite <= 0x1f) exploding = true;
        else if (!(o.sprite >= 0x11 && o.sprite <= 0x18) && o.sprite != 0x10) left++;  // not aliens, missiles, the unbreakable
    }
    if (left > 0 || exploding || !remake_) {
        had_bricks_ |= left > 0;
        cleared_t_ = -1;
        return;
    }
    if (!had_bricks_) return;  // (a level being built)
    if (cleared_t_ < 0) cleared_t_ = game_t_;
    else if (game_t_ - cleared_t_ >= CLEARED_WAIT) advance_ = true, had_bricks_ = false;
}

int BallBreakerView::level(const Spectrum& s) const { return s.peek(LEVEL) % LEVELS; }

void BallBreakerView::jump_to(Spectrum& s, int level) {
    // At the start of the next play pass (nothing half done) the level number is set and the CPU
    // goes into the game's level start, as after a cleared level: the room, its bricks, the
    // "SCREEN" intro; score, lives and missiles stay.
    pending_level_ = std::clamp(level, 0, LEVELS - 1);
    s.jump_at(MAIN_LOOP, LEVEL_START, 0x0000);
}
