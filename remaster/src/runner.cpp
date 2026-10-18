// The loop every game runs in: the original in the emulator at 50 Hz, input, sound, the
// pause screen, and test scripts; the game's module (GameView) reads its state and draws.
//
// Controls: the keyboard and gamepad become Controls (game.h), which the game's module turns
// into what the original reads. Remake keys: Esc / gamepad Start pause (there R original / HD,
// M music, N sound effects (also in play), F full screen, X / Select back to the game list, and for games with levels LEFT /
// RIGHT + ENTER to jump to one), Tab original / HD at any time, R too while the game reads no
// letters (GameView::letters_free), F11 full screen. Switching views dissolves one into the
// other through growing and shrinking pixels (as in Opera Soft Remastered).
//
//   zxremakes --test <out_dir> [--game <key>] [--file <game file>] [script]
//     Script tokens: "<sec>+<key>", "<sec>-<key>", "<sec>@<cmd>"; sec = seconds of game time
//     (50 frames each). Keys are the player's, as on the PC: a-z 0-9 space enter ctrl shift
//     alt backspace left right up down, the gamepad pa pb px (buttons), pl pr pu pd, and the
//     mouse's left button click.
//     Commands: shot (canvas -> shot_<n>.png), ram (64 KB as paged + the banks ->
//     ram_<n>.bin), orig / hd (the R view, at once), r (as the key: the dissolve), level<n> (jump
//     there, 1-based, if the game can now), key<letter> (a game option key, e.g. keyl), poke<addr>=<value> (hex, e.g. poke8078=2),
//     pokeat<pc>:<addr>=<value> (once, the next time the CPU gets to pc), pause / resume (overlay),
//     mouse<x>,<y> (the mouse steers, pointing there in canvas pixels), nomouse, esc (as the key), quit.
//     The run ends at the last event.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <raylib.h>

#include "audio.h"
#include "game.h"
#include "touch.h"
#include "gamefile.h"
#include "gamepad.h"
#include "music_remix.h"
#include "sfx.h"

namespace {

struct ScriptEvent {
    double t;
    char op;
    std::string arg;
};

// What the player holds: PC keys (raylib codes) and gamepad.
struct Held {
    std::set<int> keys;
    uint8_t pad_dirs = 0;
    bool pa = false, pb = false, px = false;
    bool mouse = false, click = false;  // the mouse steering, its left button
    Vector2 pointer{-1, -1};            // in canvas pixels
    int step = 0;                       // a swipe up / down in a menu: -1 / +1
    bool drag = false;                  // a finger steering, and its move this frame (canvas pixels)
    Vector2 drag_by{0, 0};
};

// The PC keys that reach the game, by script name.
const std::vector<std::pair<std::string, int>>& pc_inputs() {
    static const std::vector<std::pair<std::string, int>> k = [] {
        std::vector<std::pair<std::string, int>> v;
        for (int c = 0; c < 26; c++) v.push_back({std::string(1, char('a' + c)), KEY_A + c});
        for (int c = 0; c < 10; c++) v.push_back({std::string(1, char('0' + c)), KEY_ZERO + c});
        v.insert(v.end(), {{"space", KEY_SPACE}, {"enter", KEY_ENTER}, {"enter", KEY_KP_ENTER},
                           {"shift", KEY_LEFT_SHIFT}, {"shift", KEY_RIGHT_SHIFT}, {"alt", KEY_LEFT_ALT},
                           {"alt", KEY_RIGHT_ALT}, {"ctrl", KEY_LEFT_CONTROL}, {"ctrl", KEY_RIGHT_CONTROL},
                           {"backspace", KEY_BACKSPACE}, {"left", KEY_LEFT}, {"right", KEY_RIGHT},
                           {"up", KEY_UP}, {"down", KEY_DOWN}});
        return v;
    }();
    return k;
}

const std::pair<const char*, uint8_t> PAD_DIRS[] = {
    {"pl", ZXC_JOY_LEFT}, {"pr", ZXC_JOY_RIGHT}, {"pu", ZXC_JOY_UP}, {"pd", ZXC_JOY_DOWN}};

// Apply a script key to what is held; false if the name is unknown.
bool hold(Held& h, const std::string& name, bool down) {
    bool known = false;
    for (const auto& [n, k] : pc_inputs())
        if (n == name) {
            known = true;
            if (down) h.keys.insert(k);
            else h.keys.erase(k);
        }
    for (auto [n, b] : PAD_DIRS)
        if (name == n) {
            known = true;
            h.pad_dirs = down ? h.pad_dirs | b : h.pad_dirs & ~b;
        }
    bool* btn = name == "pa" ? &h.pa : name == "pb" ? &h.pb : name == "px" ? &h.px : name == "click" ? &h.click : nullptr;
    if (btn) *btn = down, known = true;
    return known;
}

Controls controls_of(const Held& h) {
    auto on = [&](int k) { return h.keys.count(k) > 0; };
    Controls c;
    c.arrows = uint8_t(h.pad_dirs | (on(KEY_LEFT) ? ZXC_JOY_LEFT : 0) | (on(KEY_RIGHT) ? ZXC_JOY_RIGHT : 0) |
                       (on(KEY_UP) ? ZXC_JOY_UP : 0) | (on(KEY_DOWN) ? ZXC_JOY_DOWN : 0));
    c.dirs = uint8_t(c.arrows | (on(KEY_A) ? ZXC_JOY_LEFT : 0) | (on(KEY_D) ? ZXC_JOY_RIGHT : 0) |
                     (on(KEY_W) ? ZXC_JOY_UP : 0) | (on(KEY_S) ? ZXC_JOY_DOWN : 0));
    bool ctrl = on(KEY_LEFT_CONTROL) || on(KEY_RIGHT_CONTROL), enter = on(KEY_ENTER) || on(KEY_KP_ENTER);
    c.trigger = ctrl || h.pa;
    c.fire = c.trigger || on(KEY_SPACE);
    c.ok = enter || on(KEY_SPACE) || h.pa;
    c.pad_b = h.pb;
    c.pad_x = h.px;
    c.mouse = h.mouse && !c.dirs, c.pointer = h.pointer, c.click = h.click, c.step = h.step;
    c.drag = h.drag, c.drag_by = h.drag_by;
    for (int i = 0; i < 26; i++)
        if (on(KEY_A + i)) c.keys.insert('a' + i);
    for (int i = 0; i < 10; i++)
        if (on(KEY_ZERO + i)) c.keys.insert('0' + i);
    if (on(KEY_SPACE)) c.keys.insert(' ');
    if (enter) c.keys.insert(0x0D);
    if (on(KEY_LEFT_SHIFT) || on(KEY_RIGHT_SHIFT)) c.keys.insert(0x0E);
    if (on(KEY_LEFT_ALT) || on(KEY_RIGHT_ALT)) c.keys.insert(0x0F);
    if (on(KEY_BACKSPACE)) c.keys.insert(0x0C);  // DELETE (chips: CAPS SHIFT + 0)
    return c;
}

std::vector<ScriptEvent> parse_script(const std::string& text) {
    std::vector<ScriptEvent> out;
    std::istringstream in(text);
    for (std::string tok; in >> tok;) {
        size_t i = tok.find_first_of("+-@");
        if (i == std::string::npos || i == 0) throw std::runtime_error("bad script token: " + tok);
        std::string arg = tok.substr(i + 1);
        Held probe;
        if (tok[i] != '@' && !hold(probe, arg, true)) throw std::runtime_error("unknown key: " + arg);
        out.push_back({std::stod(tok.substr(0, i)), tok[i], arg});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
    return out;
}

constexpr const char* DEFAULT_SCRIPT = "1@shot 1+space 1.3-space 4@shot 4.1@quit";

void save_canvas(const RenderTexture2D& canvas, const std::string& path) {
    Image shot = LoadImageFromTexture(canvas.texture);
    ImageFlipVertical(&shot);
    ExportImage(shot, path.c_str());
    UnloadImage(shot);
}

constexpr float FLIP_SECONDS = 1.0f;  // R: the whole dissolve
constexpr float FLIP_MAX_BLOCK = 48;  // px at the middle of it: one Spectrum 8x8 cell, at 6x
constexpr float TOAST_SECONDS = 1.8f;

float smoothstep(float t) { return t * t * (3 - 2 * t); }

// Source rect of a canvas-space region in a render texture (stored upside down).
Rectangle rt_src(const RenderTexture2D& rt, Rectangle r) {
    return {r.x, float(rt.texture.height) - r.y - r.height, r.width, -r.height};
}

// A pause-screen line the mouse can take: its area on the canvas and the key it stands for.
struct PauseHit {
    Rectangle r;
    int key;
};

int pause_key(const std::string& name) {  // a key as the pause screen names it
    if (name == "ESC") return KEY_ESCAPE;
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return KEY_A + (name[0] - 'A');
    return 0;
}

// Text in the pause style's font (or raylib's own): its width, and drawn
float style_width(const GameView::PauseStyle& st, const std::string& s, float size) {
    return st.font ? MeasureTextEx(*st.font, s.c_str(), size, size * 0.06f).x : float(MeasureText(s.c_str(), int(size)));
}
void style_text(const GameView::PauseStyle& st, const std::string& s, Vector2 at, float size, Color c) {
    if (st.font) DrawTextEx(*st.font, s.c_str(), at, size, size * 0.06f, c);
    else DrawText(s.c_str(), int(at.x), int(at.y), int(size), c);
}

void draw_pause(Vector2 size, const std::string& title, const std::string& help, bool original, bool music,
                bool effects, int picker, int levels, bool options, Vector2 pointer, std::vector<PauseHit>& hits,
                const GameView::PauseStyle& st) {
    hits.clear();
    const Color panel = st.panel, muted = st.muted, accent = st.accent, rule = st.rule, white = st.text;
    const float scale = std::min({1.0f, (size.x - 64) / 1400, (size.y - 64) / 900});
    const float x = (size.x - 1400*scale)/2, y = (size.y - 900*scale)/2;
    auto rect = [&](float px, float py, float w, float h, Color c) {
        DrawRectangleRec({x+px*scale, y+py*scale, w*scale, h*scale}, c);
    };
    auto text = [&](const std::string& value, float px, float py, int font, Color c, float width) {
        int actual = std::max(1, int(font*scale));
        while (actual > 1 && style_width(st, value, float(actual)) > width*scale) --actual;
        style_text(st, value, {x+px*scale, y+py*scale}, float(actual), c);
    };
    // a line with a key: the mouse takes it too (it lights up under the pointer)
    auto hit = [&](float px, float py, float w, float h, int key) {
        const Rectangle r{x+px*scale, y+py*scale, w*scale, h*scale};
        if (!key) return;
        hits.push_back({r, key});
        if (CheckCollisionPointRec(pointer, r)) DrawRectangleRec(r, st.hover);
    };
    auto row = [&](const std::string& key, const std::string& label, float px, float py, float width) {
        hit(px - 8, py - 6, width + 8, 58, pause_key(key));
        rect(px, py, 90, 46, st.key);
        text(key, px+12, py+10, 25, accent, 66);
        text(label, px+110, py+9, 28, white, width-110);
    };
    DrawRectangle(0, 0, int(size.x), int(size.y), Fade(BLACK, 0.65f));
    rect(0, 0, 1400, 900, panel);
    rect(0, 0, 1400, 3, accent);
    if (st.font) {  // a frame round the panel, gold
        DrawRectangleLinesEx({x - 6*scale, y - 6*scale, 1412*scale, 912*scale}, 3*scale, accent);
        DrawRectangleLinesEx({x - 12*scale, y - 12*scale, 1424*scale, 924*scale}, 1.5f*scale, Fade(accent, 0.5f));
    }
    text(options ? "OPTIONS" : "PAUSED", 56, 42, 42, white, 850);
    text(title, 56, 100, 26, muted, 1288);
    row("ESC", options ? "Back to the menu" : "Resume game", 56, 162, 600);
    row("X", "Back to game list", 740, 162, 604);
    rect(56, 238, 1288, 1, rule);
    if (picker >= 0) {
        text("LEVEL", 56, 276, 23, muted, 160);
        hit(222, 258, 60, 62, KEY_LEFT), hit(498, 258, 60, 62, KEY_RIGHT);
        text("<", 240, 267, 40, accent, 40);
        text(std::to_string(picker+1) + " / " + std::to_string(levels), 300, 267, 40, accent, 190);
        text(">", 516, 267, 40, accent, 40);
        hit(732, 300, 612, 40, KEY_ENTER);
        text("LEFT / RIGHT   Choose", 740, 265, 25, white, 604);
        text("ENTER          Start level", 740, 307, 25, muted, 604);
    } else text(options ? "The remake's settings" : "Game paused", 56, 280, 28, muted, 1288);
    rect(56, 365, 1288, 1, rule);
    text("DISPLAY & AUDIO", 56, 399, 23, muted, 600);
    text("GAME OPTIONS", 740, 399, 23, muted, 604);
    row("R", original ? "Original graphics & sound" : "HD graphics & sound", 56, 447, 600);
    row("M", std::string("Music: ") + (music ? "On" : "Off"), 56, 509, 600);
    row("N", std::string("Sound effects: ") + (effects ? "On" : "Off"), 56, 571, 600);
    row("F", "Fullscreen", 56, 633, 600);
    std::istringstream lines(help);
    bool controls = false;
    float option_y = 447, control_y = 740;
    for (std::string line; std::getline(lines, line);) {
        if (line.empty()) { controls = true; continue; }
        if (controls) {
            text(line, 56, control_y, 25, muted, 1288);
            control_y += 38;
        } else {
            const auto split = line.find("  ");
            row(split == std::string::npos ? "" : line.substr(0, split),
                split == std::string::npos ? line : line.substr(split+2), 740, option_y, 604);
            option_y += 62;
        }
    }
    rect(56, 711, 1288, 1, rule);
    text("GAMEPAD   Start: resume     Select: back", 56, 836, 23, muted, 1288);
}

#ifdef __ANDROID__
// The pause screen for a finger: big buttons, each a key the runner already knows.
void draw_pause_touch(Vector2 size, const std::string& help, bool original, bool music, bool effects, int picker, int levels,
                      bool options, Vector2 pointer, std::vector<PauseHit>& hits, const GameView::PauseStyle& st) {
    hits.clear();
    auto value = [&](char key) {  // "T  Theme: Adventure 3D" -> "THEME: ADVENTURE 3D"
        std::istringstream in(help);
        for (std::string line; std::getline(in, line);)
            if (line.size() > 3 && line[0] == key && line[1] == ' ') {
                std::string v = line.substr(line.find_first_not_of(' ', 1));
                for (char& ch : v) ch = char(std::toupper(static_cast<unsigned char>(ch)));
                return v;
            }
        return std::string();
    };
    const float k = std::min(size.x / 1920, size.y / 1080), ox = (size.x - 1920 * k) / 2, oy = (size.y - 1080 * k) / 2;
    DrawRectangle(0, 0, int(size.x), int(size.y), Fade(BLACK, 0.72f));
    auto label = [&](const std::string& t, Vector2 c, float px, Color col) {
        float sz = px * k;
        while (sz > 10 && style_width(st, t, sz) > 480 * k) sz -= 2;
        const float w = style_width(st, t, sz);
        style_text(st, t, {c.x - w / 2, c.y - sz / 2}, sz, col);
    };
    label(options ? "OPTIONS" : "PAUSED", {ox + 960 * k, oy + 80 * k}, 64, st.accent);
    auto button = [&](float x, float y, float w, float h, const std::string& t, int key, bool lit = false) {
        const Rectangle r{ox + x * k, oy + y * k, w * k, h * k};
        hits.push_back({r, key});
        const bool over = CheckCollisionPointRec(pointer, r);
        DrawRectangleRounded(r, 0.25f, 8, over ? st.hover : st.key);
        DrawRectangleRoundedLinesEx(r, 0.25f, 8, 3 * k, lit ? st.accent : Fade(st.accent, 0.55f));
        label(t, {r.x + r.width / 2, r.y + r.height / 2}, 48, lit ? st.accent : st.text);
    };
    const float W3 = 540, H = 150, X[3] = {90, 690, 1290};
    button(X[0], 160, W3, H, options ? "BACK" : "RESUME", KEY_ESCAPE, true);
    button(X[1], 160, W3, H, "GAME LIST", KEY_X);
    button(X[2], 160, W3, H, original ? "GRAPHICS: 1987" : "GRAPHICS: REMASTERED", KEY_R);
    button(X[0], 340, W3, H, music ? "MUSIC: ON" : "MUSIC: OFF", KEY_M);
    button(X[1], 340, W3, H, effects ? "SOUNDS: ON" : "SOUNDS: OFF", KEY_N);
    if (std::string v = value('T'); !v.empty()) button(X[2], 340, W3, H, v, KEY_T);
    if (std::string v = value('L'); !v.empty()) button(X[0], 520, W3, H, v, KEY_L);
    if (std::string v = value('O'); !v.empty()) button(X[1], 520, W3, H, v, KEY_O);
    if (picker >= 0 && levels > 0) {
        button(X[0], 760, 240, H, "<", KEY_LEFT);
        button(X[0] + 300, 760, 1140, H, "PLAY SCREEN " + std::to_string(picker + 1) + " / " + std::to_string(levels), KEY_ENTER);
        button(X[2] + 300, 760, 240, H, ">", KEY_RIGHT);
    }
}

// A medallion for the touch buttons: dark bronze, a gold rim with notches, a light that breathes
void medallion(Vector2 c, float r, bool down, double t) {
    const float glow = (down ? 1.0f : 0.55f) + 0.15f * float(std::sin(t * 3));
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(c, r * 1.35f, Color{60, 200, 255, uint8_t(90 * glow)}, Color{60, 200, 255, 0});
    EndBlendMode();
    DrawCircleGradient(c, r, Color{78, 60, 40, 230}, Color{26, 18, 10, 230});
    DrawRing(c, r - 7, r, 0, 360, 64, Color{226, 182, 92, 255});
    DrawRing(c, r - 14, r - 11, 0, 360, 64, Fade(Color{226, 182, 92, 255}, 0.6f));
    for (int i = 0; i < 16; i++) {  // its notches
        const float a = float(i) * 2 * PI / 16, ca = std::cos(a), sa = std::sin(a);
        DrawLineEx({c.x + ca * (r - 22), c.y + sa * (r - 22)}, {c.x + ca * (r - 15), c.y + sa * (r - 15)}, 2, Fade(Color{226, 182, 92, 255}, 0.7f));
    }
    if (down) DrawCircleV(c, r - 16, Fade(Color{90, 210, 255, 255}, 0.18f));
}
#endif

}  // namespace

GameExit run_game(const LoadedGame& game, const std::string& assets, const std::string& test_dir,
                  const std::string& script_text) {
    const GameInfo& info = game_info(game.variant->game);
    std::vector<ScriptEvent> script;
    if (!test_dir.empty()) {
        // a scripted run: the games keep nothing of it (hi-scores...); ZX_TEST_HISCORES=1 to check that they do
        if (!std::getenv("ZX_TEST_HISCORES")) {
#ifdef _WIN32
            _putenv_s("ZXREMAKES_TEST", "1");
#else
            setenv("ZXREMAKES_TEST", "1", 1);
#endif
        }
        try {
            script = parse_script(script_text.empty() ? DEFAULT_SCRIPT : script_text);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return GameExit::Quit;
        }
    }
    // a tape the first time: loaded in the background (seconds), its loading screen up meanwhile;
    // then its machine is kept (not in test runs) and the next times start at once
    // (ZX_TEST_CACHE=1: test runs too, to check it)
    static const bool test_cache = std::getenv("ZX_TEST_CACHE") != nullptr;
    const bool cache = test_dir.empty() || test_cache;
    const std::string cache_dir = cache ? user_data_dir("ZX Remakes") + "cache/" : "";
    std::unique_ptr<Spectrum> spec;
    if (game.variant->format == FileFormat::Z80 || cached_machine(game, cache_dir) || !cache) {
        spec = load_machine(game, cache_dir);
    } else {
        struct Shown {
            std::mutex m;
            std::vector<uint8_t> screen = std::vector<uint8_t>(6912, 0);
            float done = 0;
        } shown;
        auto loading = std::async(std::launch::async, [&] {
            return load_machine(game, cache_dir, [&](const uint8_t* scr, float done) {
                std::lock_guard<std::mutex> lock(shown.m);
                shown.screen.assign(scr, scr + 6912);
                shown.done = done;
            });
        });
        Image blank = GenImageColor(Spectrum::SCREEN_W, Spectrum::SCREEN_H, BLACK);
        Texture2D tex = LoadTextureFromImage(blank);
        UnloadImage(blank);
        SetTextureFilter(tex, TEXTURE_FILTER_POINT);
        std::vector<uint32_t> rgba(size_t(Spectrum::SCREEN_W) * Spectrum::SCREEN_H);
        double t0 = GetTime();
        while (loading.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            float done;
            {
                std::lock_guard<std::mutex> lock(shown.m);
                Spectrum::screen_rgba(shown.screen.data(), rgba.data(), std::fmod(GetTime() - t0, 0.64) < 0.32);
                done = shown.done;
            }
            UpdateTexture(tex, rgba.data());
            float w = float(GetScreenWidth()), h = float(GetScreenHeight());
            float k = std::min(w / Spectrum::SCREEN_W, (h - 120) / Spectrum::SCREEN_H) * 0.85f;
            Rectangle dst{(w - Spectrum::SCREEN_W * k) / 2, (h - 120 - Spectrum::SCREEN_H * k) / 2, Spectrum::SCREEN_W * k,
                          Spectrum::SCREEN_H * k};
            BeginDrawing();
            ClearBackground({14, 14, 20, 255});
            DrawTexturePro(tex, {0, 0, float(Spectrum::SCREEN_W), float(Spectrum::SCREEN_H)}, dst, {0, 0}, 0, WHITE);
            std::string msg = std::string("Loading ") + info.title + " from tape (the first time only)";
            DrawText(msg.c_str(), int((w - float(MeasureText(msg.c_str(), 28))) / 2), int(h - 100), 28, Fade(RAYWHITE, 0.8f));
            float bw = std::min(w * 0.6f, 900.0f), bx = (w - bw) / 2, by = h - 52;
            DrawRectangleRounded({bx, by, bw, 12}, 1, 8, {40, 40, 54, 255});
            DrawRectangleRounded({bx, by, std::max(12.0f, bw * std::clamp(done, 0.0f, 1.0f)), 12}, 1, 8, {255, 214, 96, 255});
            EndDrawing();
        }
        UnloadTexture(tex);
        spec = loading.get();
    }
    if (!spec) {
        TraceLog(LOG_ERROR, "Could not load %s", game.variant->name);
        return GameExit::Back;
    }
    TraceLog(LOG_INFO, "Game: %s (%dK)", game.variant->name, spec->model());
    SetWindowTitle((std::string(info.title) + " - ZX Remakes").c_str());

    std::unique_ptr<GameView> view = make_view(info.id);
    view->start(*spec, assets + "/" + info.key);
    Vector2 csize = view->canvas_size();
    RenderTexture2D canvas = LoadRenderTexture(int(csize.x), int(csize.y));
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_BILINEAR);
    std::unique_ptr<SpectrumAudio> audio;
    if (test_dir.empty()) audio = std::make_unique<SpectrumAudio>(Spectrum::SAMPLE_RATE);
    // the remake's sound (with the remake's picture; R: the original's); ZX_AUDIO_DUMP=file.wav saves it
    std::unique_ptr<MusicRemix> remix = view->make_remix(Spectrum::SAMPLE_RATE);
    SfxBank sfx(Spectrum::SAMPLE_RATE);
    bool remastered_sound = remix != nullptr;
    // M music, N sound effects (the remake's, and the original's own while its tune is not on); in
    // the original's sound of a game with no remix both are one: it is silent only with both off
    bool music_on = true, effects_on = true;
    bool mouse_on = false;  // the mouse steers (moved or clicked since the last direction key)
    // in play with the mouse the pointer is the game's: held in the window (a pointer of our own,
    // moved by the mouse's motion, kept inside), let go on the pause screen and in the menus
    bool captured = false;
#ifdef __ANDROID__
    // a touch screen: where the steering finger was last, and the on-screen buttons
    Vector2 last_touch{-1, -1};
    bool touched = false, fire_down = false;
    TouchGesture gesture;
    int swipe_frames = 0;  // a swipe left / right in a menu: held as its direction this many frames
    uint8_t swipe_dir = 0;
    Texture2D fire_tex = LoadTexture((assets + "/touch/fire.png").c_str());
    Texture2D pause_tex = LoadTexture((assets + "/touch/pause.png").c_str());
    for (Texture2D* tx : {&fire_tex, &pause_tex})
        if (tx->id) GenTextureMipmaps(tx), SetTextureFilter(*tx, TEXTURE_FILTER_TRILINEAR);
    auto touch_buttons = [] {
        const float sw = float(GetScreenWidth()), sh = float(GetScreenHeight()), u = std::min(sw, sh) / 1080.0f;
        return std::pair{Rectangle{24 * u, 24 * u, 150 * u, 150 * u},                   // II: pause, top left
                         Rectangle{20 * u, sh / 2 - 60 * u, 240 * u, 240 * u}};  // FIRE: the left side
    };
#endif
    auto esc_pressed = [] { return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK); };  // (Android's back)
    Vector2 pointer{0, 0};
    std::vector<PauseHit> pause_hits;  // the pause screen's lines the mouse can take
    auto to_canvas = [&](Vector2 m) {
        float fit = std::min(float(GetScreenWidth()) / csize.x, float(GetScreenHeight()) / csize.y);
        return Vector2{(m.x - (GetScreenWidth() - csize.x * fit) / 2) / fit, (m.y - (GetScreenHeight() - csize.y * fit) / 2) / fit};
    };
    bool options_open = false;  // the pause screen opened from the game's menu (its OPTIONS item)
    view->set_remake(remastered_sound);
    std::vector<SfxEvent> sfx_events;
    const char* dump_path = std::getenv("ZX_AUDIO_DUMP");
    std::vector<int16_t> dump;

    Held script_held;
    Pad pad;
    bool original = false, paused = false;
    // R / Tab: the view dissolves into the other one (as in Opera Soft Remastered)
    bool shown_original = false, flip_to_original = false;
    float flip = 1;
    RenderTexture2D rt_hd{}, rt_orig{}, rt_blocks{};
    std::string toast_msg;
    double toast_until = 0;
    auto toast = [&](const std::string& m) {
        toast_msg = m;
        toast_until = GetTime() + TOAST_SECONDS;
    };
    // R / Tab: the original 1987 game, picture and sound, or the remake's, with its music and effects
    auto set_sound = [&] {
        remastered_sound = remix && !original;
        view->set_remake(remastered_sound);
    };
    auto toggle_view = [&] {
        original = shown_original = flip_to_original = !original;
        flip = 0;  // dissolve into the other view
        set_sound();
        toast(std::string(original ? "ORIGINAL 1987" : "REMASTERED") + view->view_changed(*spec, original));
    };
    int picker = -1;  // pause screen: the level to jump to, -1 = the game has none
    GameExit exit = GameExit::Quit;
    size_t next_event = 0;
    int shots = 0, dumps = 0;
    double acc = 0;
    std::vector<float> samples, stereo;

    auto frame = [&] {
        spec->run_frame();
        view->after_frame(*spec);
        // the frame's sound: the original's (mono), or the tune re-arranged; the effects on top
        samples.resize(8192);
        int n = 0, got;
        while (n < 8192 && (got = spec->audio(samples.data() + n, 8192 - n)) > 0) n += got;
        stereo.assign(size_t(n) * 2, 0.0f);
        uint8_t regs[16];
        spec->ay_regs(regs);
        static const bool ay_log = std::getenv("ZX_AY_LOG") != nullptr;
        if (ay_log) {
            std::fprintf(stderr, "ay %llu", (unsigned long long)spec->frames());
            for (int r = 0; r < 14; r++) std::fprintf(stderr, " %02x", regs[r]);
            std::fprintf(stderr, "\n");
        }
        static const bool music_log = std::getenv("ZX_MUSIC_LOG") != nullptr;
        static int was_playing = -1;
        if (music_log && remix && int(remix->playing()) != was_playing)
            was_playing = remix->playing(), std::fprintf(stderr, "frame %llu remix %s\n", (unsigned long long)spec->frames(), was_playing ? "on" : "off");
        if (remix && remastered_sound && remix->playing()) remix->frame(regs, music_on ? stereo.data() : nullptr, n);
        else {
            if (remix) remix->frame(regs, nullptr, n);  // keep following the tune
            // the original's AY: its tune or its effects. With a remix following it, which one is
            // known (the tune: M, effects: N), in the original's sound (R) too; a game without a
            // remix: its tune in the remake's sound, both together in the original's
            bool hear = remix ? (remix->playing() ? music_on : effects_on)
                              : remastered_sound ? music_on : (music_on || effects_on);
            if (hear)
                for (int i = 0; i < n; i++) stereo[size_t(i) * 2] = stereo[size_t(i) * 2 + 1] = samples[size_t(i)];
        }
        if (view->music_restarted() && remix) {
            remix->restart();
            if (std::getenv("ZX_MUSIC_LOG")) std::fprintf(stderr, "frame %llu tune from the top\n", (unsigned long long)spec->frames());
        }
        sfx_events.clear();
        view->take_sfx(sfx_events);
        static const bool sfx_log = std::getenv("ZX_SFX_LOG") != nullptr;
        for (const SfxEvent& e : sfx_events) {
            if (sfx_log) std::fprintf(stderr, "frame %llu sfx %d pan %.2f pitch %d\n", (unsigned long long)spec->frames(), int(e.kind), e.pan, e.pitch);
            if (remastered_sound && effects_on) sfx.play(e);
        }
        sfx.mix(stereo.data(), n);
        if (audio) audio->push(stereo.data(), n);
        if (dump_path)
            for (float v : stereo) dump.push_back(int16_t(std::clamp(v * 0.8f, -1.0f, 1.0f) * 32767));
    };
    // The old view breaks up into ever bigger pixels, up to one block per Spectrum cell; the new
    // one comes out of them, its pixels shrinking back to full detail. Meanwhile the picture eases
    // between the remake's layout (the whole canvas) and the original's (original_rect).
    auto draw_blocky = [&](const RenderTexture2D& from, Rectangle src_area, Rectangle dst, float block, float alpha) {
        int nx = std::max(1, int(dst.width / block)), ny = std::max(1, int(dst.height / block));
        EndTextureMode();
        BeginTextureMode(rt_blocks);
        ClearBackground(BLACK);
        DrawTexturePro(from.texture, rt_src(from, src_area), {0, 0, float(nx), float(ny)}, {0, 0}, 0, WHITE);
        EndTextureMode();
        BeginTextureMode(canvas);
        DrawTexturePro(rt_blocks.texture, rt_src(rt_blocks, {0, 0, float(nx), float(ny)}), dst, {0, 0}, 0, Fade(WHITE, alpha));
    };
    auto draw_flip = [&] {
        const float e = smoothstep(flip);
        const float k = flip_to_original ? e : 1 - e;  // 0 = the remake's layout, 1 = the original's
        const Rectangle full{0, 0, csize.x, csize.y}, orect = view->original_rect();
        const Rectangle dst{orect.x * k, orect.y * k, full.width + (orect.width - full.width) * k,
                            full.height + (orect.height - full.height) * k};
        const float peak = 1 - std::fabs(2 * flip - 1);  // 0 at both ends, 1 in the middle
        const float block = std::max(1.0f, FLIP_MAX_BLOCK * peak * peak);
        const float mix = std::clamp((flip - 0.4f) / 0.2f, 0.0f, 1.0f);
        auto draw_view = [&](bool orig, float alpha) {
            if (alpha <= 0) return;
            const RenderTexture2D& rt = orig ? rt_orig : rt_hd;
            Rectangle area = orig ? orect : full;
            float native = orig ? dst.width / Spectrum::FB_W : 1.0f;  // a Spectrum pixel is already this big
            if (block <= native) DrawTexturePro(rt.texture, rt_src(rt, area), dst, {0, 0}, 0, Fade(WHITE, alpha));
            else draw_blocky(rt, area, dst, block, alpha);
        };
        draw_view(!flip_to_original, 1.0f);  // the old view
        draw_view(flip_to_original, mix);    // the new one fades in over it
    };
    auto render = [&] {
        // game time, continuous: the frames run plus the time not yet run (scripted runs: whole
        // frames, repeatable shots); smoothing and effects go by it (and stop on pause)
        double now = double(spec->frames()) / Spectrum::FRAME_HZ + (test_dir.empty() ? acc : 0.0);
        if (flip < 1) {
            if (!rt_hd.id) {
                rt_hd = LoadRenderTexture(int(csize.x), int(csize.y));
                rt_orig = LoadRenderTexture(int(csize.x), int(csize.y));
                rt_blocks = LoadRenderTexture(int(csize.x), int(csize.y));
                SetTextureFilter(rt_blocks.texture, TEXTURE_FILTER_POINT);
            }
            view->prepare(*spec, false, now);
            BeginTextureMode(rt_hd);
            ClearBackground(BLACK);
            view->draw(*spec, false, now);
            EndTextureMode();
            BeginTextureMode(rt_orig);
            ClearBackground(BLACK);
            view->draw(*spec, true, now);
            EndTextureMode();
            BeginTextureMode(canvas);
            ClearBackground(BLACK);
            draw_flip();
        } else {
            view->prepare(*spec, original, now);
            BeginTextureMode(canvas);
            ClearBackground(BLACK);
            view->draw(*spec, original, now);
        }
        double wall = GetTime();
        if (paused)
#ifdef __ANDROID__
            draw_pause_touch(csize, view->help(), original, music_on, effects_on, picker, view->levels(), options_open,
                             IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? to_canvas(GetMousePosition()) : Vector2{-1, -1}, pause_hits,
                             view->pause_style(original));
#else
            draw_pause(csize, info.title, view->help(), original, music_on, effects_on, picker, view->levels(), options_open,
                       test_dir.empty() ? to_canvas(GetMousePosition()) : Vector2{-1, -1}, pause_hits, view->pause_style(original));
#endif
        if (!paused && wall < toast_until) {
            float a = std::min(1.0f, float(toast_until - wall) / 0.4f);
            const GameView::PauseStyle st = view->pause_style(original);
            const float w = style_width(st, toast_msg, 40) + 60;
            const Rectangle box{(csize.x - w) / 2, 40, w, 64};
            DrawRectangleRounded(box, 0.4f, 8, st.font ? Fade(st.panel, 0.92f * a) : Fade(BLACK, 0.7f * a));
            if (st.font) DrawRectangleRoundedLinesEx(box, 0.4f, 8, 2, Fade(st.accent, a));
            style_text(st, toast_msg, {box.x + 30, 52}, 40, Fade(st.font ? st.accent : RAYWHITE, a));
        }
        EndTextureMode();
    };

    while (true) {
        if (!test_dir.empty()) {  // scripted: one emulated frame per loop, as fast as it goes
            if (WindowShouldClose()) break;
            double game_t = double(spec->frames()) / Spectrum::FRAME_HZ;
            bool quit = false;
            for (; next_event < script.size() && script[next_event].t <= game_t; next_event++) {
                const ScriptEvent& e = script[next_event];
                if (e.op == '+' || e.op == '-') hold(script_held, e.arg, e.op == '+');
                else if (e.arg == "shot") {
                    render();
                    char name[32];
                    std::snprintf(name, sizeof name, "/shot_%03d.png", shots++);
                    save_canvas(canvas, test_dir + name);
                } else if (e.arg == "ram") {
                    char name[32];
                    std::snprintf(name, sizeof name, "/ram_%03d.bin", dumps++);
                    auto d = spec->dump();
                    if (FILE* f = std::fopen((test_dir + name).c_str(), "wb")) {
                        std::fwrite(d.data(), 1, d.size(), f);
                        std::fclose(f);
                    }
                } else if (e.arg == "pause") { paused = true, options_open = false; picker = view->levels() > 0 && view->can_jump() ? view->level(*spec) : -1; }
                else if (e.arg == "resume") paused = false;
                else if (e.arg == "esc") {  // as the key: the game's module first
                    if (GameView::Escape r = view->escape(); r != GameView::Escape::Done)
                        paused = true, options_open = r == GameView::Escape::Options, picker = -1;
                }
                else if (e.arg.rfind("mouse", 0) == 0 && e.arg.find(',') != std::string::npos) {
                    size_t comma = e.arg.find(',');
                    script_held.mouse = true;
                    script_held.pointer = {std::stof(e.arg.substr(5, comma - 5)), std::stof(e.arg.substr(comma + 1))};
                } else if (e.arg == "nomouse") script_held.mouse = false;
                else if (e.arg == "orig") original = shown_original = true, flip = 1, set_sound();  // at once
                else if (e.arg == "hd") original = shown_original = false, flip = 1, set_sound();
                else if (e.arg == "r") toggle_view();  // as the key: the dissolve
                else if (e.arg.rfind("key", 0) == 0 && e.arg.size() == 4) {  // a game option key, e.g. keyl
                    int k = KEY_A + (std::tolower(static_cast<unsigned char>(e.arg[3])) - 'a');
                    if (std::string m = view->on_key(*spec, k); !m.empty()) toast(m);
                }
                else if (e.arg.rfind("level", 0) == 0 && view->can_jump())
                    view->jump_to(*spec, std::stoi(e.arg.substr(5)) - 1);
                else if (e.arg.rfind("pokeat", 0) == 0 && e.arg.find(':') != std::string::npos) {
                    // the next time the CPU gets to pc: poke (e.g. the level number as a level starts)
                    size_t colon = e.arg.find(':'), eq = e.arg.find('=');
                    auto pc = uint16_t(std::stoul(e.arg.substr(6, colon - 6), nullptr, 16));
                    auto addr = uint16_t(std::stoul(e.arg.substr(colon + 1, eq - colon - 1), nullptr, 16));
                    auto value = uint8_t(std::stoul(e.arg.substr(eq + 1), nullptr, 16));
                    auto done = std::make_shared<bool>(false);
                    Spectrum* s = spec.get();
                    spec->on_exec(pc, [=](const zxc_regs_t&) {
                        if (!*done) s->poke(addr, value), *done = true;
                    });
                } else if (e.arg.rfind("poke", 0) == 0 && e.arg.find('=') != std::string::npos) {
                    size_t eq = e.arg.find('=');
                    spec->poke(uint16_t(std::stoul(e.arg.substr(4, eq - 4), nullptr, 16)),
                               uint8_t(std::stoul(e.arg.substr(eq + 1), nullptr, 16)));
                } else if (e.arg == "quit") quit = true;
                else std::fprintf(stderr, "unknown script command: %s\n", e.arg.c_str());
            }
            if (quit || next_event == script.size()) break;
            view->input(*spec, controls_of(script_held));
            frame();
            if (view->take_options()) paused = options_open = true, picker = -1;
            if (view->take_exit()) break;
            if (flip < 1) flip = std::min(1.0f, flip + 1.0f / (FLIP_SECONDS * float(Spectrum::FRAME_HZ)));
            if (spec->frames() % 25 == 0) {  // keep the window alive (and showing something)
                render();
                BeginDrawing();
                ClearBackground(BLACK);
                DrawTexturePro(canvas.texture, {0, 0, csize.x, -csize.y}, {0, 0, float(GetScreenWidth()), float(GetScreenHeight())}, {0, 0}, 0, WHITE);
                EndDrawing();
            }
            continue;
        }

        if (WindowShouldClose()) break;
        pad.update();
#ifdef __ANDROID__
        gesture.update();
#endif
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (flip < 1) flip = std::min(1.0f, flip + GetFrameTime() / FLIP_SECONDS);
        if (IsKeyPressed(KEY_TAB)) toggle_view();
        if (paused) {
            // a click on a line is its key
            int clicked = 0;
#ifdef __ANDROID__
            // a tap on a button is its key; a swipe left / right steps the level
            if (gesture.tap)
                for (const PauseHit& ph : pause_hits)
                    if (CheckCollisionPointRec(to_canvas(gesture.at), ph.r)) clicked = ph.key;
            if (gesture.swipe == TouchGesture::LEFT) clicked = KEY_LEFT;
            if (gesture.swipe == TouchGesture::RIGHT) clicked = KEY_RIGHT;
#else
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                for (const PauseHit& ph : pause_hits)
                    if (CheckCollisionPointRec(to_canvas(GetMousePosition()), ph.r)) clicked = ph.key;
#endif
            auto key = [&](int k) { return IsKeyPressed(k) || clicked == k || (k == KEY_ESCAPE && IsKeyPressed(KEY_BACK)); };
            int n = view->levels();
            if (picker >= 0 && n > 0) {
                if (key(KEY_LEFT) || pad.pressed(Pad::LEFT)) picker = (picker + n - 1) % n;
                if (key(KEY_RIGHT) || pad.pressed(Pad::RIGHT)) picker = (picker + 1) % n;
            }
            if ((key(KEY_ENTER) || pad.pressed(Pad::A)) && picker >= 0 && picker != view->level(*spec)) {
                view->jump_to(*spec, picker);
                char buf[32];
                std::snprintf(buf, sizeof buf, "SCREEN %d", picker + 1);
                toast(buf);
                paused = false;
            } else if (key(KEY_ESCAPE) || key(KEY_ENTER) || pad.pressed(Pad::START) ||
                       IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
                paused = false;
            if (key(KEY_R)) toggle_view();
            for (int k = KEY_A; k <= KEY_Z; k++)  // the game's options (the runner's own letters aside)
                if (k != KEY_R && k != KEY_M && k != KEY_N && k != KEY_F && k != KEY_X && key(k))
                    if (std::string m = view->on_key(*spec, k); !m.empty()) toast(m);
            if (key(KEY_M)) music_on = !music_on, toast(music_on ? "MUSIC: ON" : "MUSIC: OFF");
            if (key(KEY_N)) effects_on = !effects_on, toast(effects_on ? "SOUND EFFECTS: ON" : "SOUND EFFECTS: OFF");
            if (key(KEY_F)) ToggleBorderlessWindowed();
            if (key(KEY_X) || pad.pressed(Pad::SELECT)) {
                exit = GameExit::Back;
                break;
            }
            if (!paused) {
                acc = 0;
                if (audio) audio->clear();
            }
        } else if ((esc_pressed() || pad.pressed(Pad::START) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)
#ifdef __ANDROID__
                    || (view->steers() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                        CheckCollisionPointRec(GetMousePosition(), touch_buttons().first))
#endif
                        ) &&
                   view->escape() != GameView::Escape::Done) {
            paused = true, options_open = view->escape() == GameView::Escape::Options;
            picker = view->levels() > 0 && view->can_jump() ? view->level(*spec) : -1;
            view->input(*spec, Controls{});  // let go of everything
        } else if (IsKeyPressed(KEY_R) && view->letters_free()) {
            toggle_view();  // the game reads no letters now: R is the remake's
        } else if (IsKeyPressed(KEY_M) && view->letters_free()) {
            music_on = !music_on, toast(music_on ? "MUSIC: ON" : "MUSIC: OFF");
        } else if (IsKeyPressed(KEY_N) && view->letters_free()) {
            effects_on = !effects_on, toast(effects_on ? "SOUND EFFECTS: ON" : "SOUND EFFECTS: OFF");
        } else if (int k = GetKeyPressed(); k >= KEY_A && k <= KEY_Z && view->letters_free()) {
            if (std::string m = view->on_key(*spec, k); !m.empty()) toast(m);  // a game option
        } else {
            Held h;
            for (const auto& [name, k] : pc_inputs())
                if (IsKeyDown(k)) h.keys.insert(k);
            const std::pair<Pad::Btn, uint8_t> DIRS[] = {
                {Pad::LEFT, ZXC_JOY_LEFT}, {Pad::RIGHT, ZXC_JOY_RIGHT}, {Pad::UP, ZXC_JOY_UP}, {Pad::DOWN, ZXC_JOY_DOWN}};
            for (auto [b, bit] : DIRS)
                if (pad.down(b)) h.pad_dirs |= bit;
            h.pa = pad.down(Pad::A), h.pb = pad.down(Pad::B), h.px = pad.down(Pad::X);
#ifdef __ANDROID__
            // touch: in play the first finger steers (the bat follows it, and stays when it lifts), a
            // second one or the FIRE button fires; in the menus a tap is a click where it was let go,
            // a swipe a direction (held a few frames, as a key the game sees)
            if (view->steers()) {
                const auto [pause_btn, fire_btn] = touch_buttons();
                bool fire = false, touching = false;
                Vector2 at{-1, -1};
                for (int i = 0; i < GetTouchPointCount(); i++) {
                    const Vector2 p = GetTouchPosition(i);
                    if (CheckCollisionPointRec(p, fire_btn)) fire = true;
                    else if (CheckCollisionPointRec(p, pause_btn)) continue;
                    else if (!touching) at = p, touching = true;
                    else fire = true;
                }
                // the steering finger: its move, not where it is (put down anywhere, it moves nothing)
                if (touching && touched) {
                    const Vector2 a = to_canvas(at), b = to_canvas(last_touch);
                    h.drag_by = {a.x - b.x, a.y - b.y};
                }
                h.drag = touching;
                last_touch = at, touched = touching;
                fire_down = fire;
                h.mouse = false, h.click = fire;
                swipe_frames = 0;
            } else {
                h.mouse = gesture.tap, h.click = gesture.tap;
                h.pointer = to_canvas(gesture.tap ? gesture.at : gesture.now);
                // up / down: a step through the menu (the module walks the bar there); left / right: a
                // direction held a moment (pages)
                if (gesture.swipe == TouchGesture::UP) h.step = -1;
                if (gesture.swipe == TouchGesture::DOWN) h.step = 1;
                if (gesture.swipe == TouchGesture::LEFT) swipe_dir = ZXC_JOY_LEFT, swipe_frames = 3;
                if (gesture.swipe == TouchGesture::RIGHT) swipe_dir = ZXC_JOY_RIGHT, swipe_frames = 3;
                if (swipe_frames > 0) h.pad_dirs |= swipe_dir, swipe_frames--;
                touched = false, fire_down = false;
            }
            mouse_on = h.mouse;
#else
            // the mouse takes over the steering once it moves or clicks, until a direction is held
            Vector2 md = GetMouseDelta();
            h.click = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            if (std::fabs(md.x) + std::fabs(md.y) > 2 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) mouse_on = true;
            if (captured)  // our pointer, moved by the mouse, kept in the window
                pointer = {std::clamp(pointer.x + md.x, 0.0f, float(GetScreenWidth() - 1)),
                           std::clamp(pointer.y + md.y, 0.0f, float(GetScreenHeight() - 1))};
            h.pointer = to_canvas(captured ? pointer : GetMousePosition());
            h.mouse = mouse_on;
#endif
            Controls c = controls_of(h);
            if (c.dirs) mouse_on = false;

            acc += GetFrameTime();
            const double step = 1.0 / Spectrum::FRAME_HZ;
            for (int n = 0; acc >= step && n < 4; n++, acc -= step) {
                view->input(*spec, c);
                frame();
                if (view->take_options()) {  // (the module waits for the keys to be let go after)
                    paused = options_open = true, picker = -1;
                    break;
                }
            }
            if (view->take_exit()) {
                exit = GameExit::Back;
                break;
            }
            if (acc > step) acc = 0;  // too far behind (e.g. the window was dragged): skip
        }

#ifndef __ANDROID__  // (no pointer to hold on a touch screen)
        if (bool hold = test_dir.empty() && mouse_on && !paused && view->hides_pointer(); hold != captured) {
            if (hold) pointer = GetMousePosition(), DisableCursor();  // (hidden, and kept in the window)
            else EnableCursor(), SetMousePosition(int(pointer.x), int(pointer.y));  // back where it was
            captured = hold;
        }
#endif
        render();
        BeginDrawing();
        ClearBackground(BLACK);
        float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
        float scale = std::min(sw / csize.x, sh / csize.y);
        Rectangle dest{(sw - csize.x * scale) / 2, (sh - csize.y * scale) / 2, csize.x * scale, csize.y * scale};
        DrawTexturePro(canvas.texture, {0, 0, csize.x, -csize.y}, dest, {0, 0}, 0, WHITE);
#ifdef __ANDROID__
        if (!paused && view->steers()) {  // the touch buttons: the medallions (their art, else drawn)
            const auto [pause_btn, fire_btn] = touch_buttons();
            const double tt = GetTime();
            auto art = [&](const Texture2D& tx, Rectangle r, bool down) {
                const float k = down ? 0.93f : 1.0f, w = r.width * k, x = r.x + (r.width - w) / 2, y = r.y + (r.height - w) / 2;
                if (down) {
                    BeginBlendMode(BLEND_ADDITIVE);
                    DrawCircleGradient({r.x + r.width / 2, r.y + r.height / 2}, r.width * 0.7f, Color{80, 220, 255, 120}, Color{80, 220, 255, 0});
                    EndBlendMode();
                }
                DrawTexturePro(tx, {0, 0, float(tx.width), float(tx.height)}, {x, y, w, w}, {0, 0}, 0,
                               down ? WHITE : Color{235, 235, 235, 235});
            };
            if (fire_tex.id) art(fire_tex, fire_btn, fire_down);
            else medallion({fire_btn.x + fire_btn.width / 2, fire_btn.y + fire_btn.height / 2}, fire_btn.width / 2, fire_down, tt);
            if (pause_tex.id) art(pause_tex, pause_btn, false);
            else medallion({pause_btn.x + pause_btn.width / 2, pause_btn.y + pause_btn.height / 2}, pause_btn.width / 2, false, tt);
        }
#endif
        EndDrawing();
    }
    if (dump_path && !dump.empty())
        if (FILE* f = std::fopen(dump_path, "wb")) {  // 16-bit stereo WAV
            auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
            auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
            uint32_t bytes = uint32_t(dump.size() * 2);
            std::fwrite("RIFF", 1, 4, f), u32(36 + bytes), std::fwrite("WAVEfmt ", 1, 8, f);
            u32(16), u16(1), u16(2), u32(Spectrum::SAMPLE_RATE), u32(Spectrum::SAMPLE_RATE * 4), u16(4), u16(16);
            std::fwrite("data", 1, 4, f), u32(bytes);
            std::fwrite(dump.data(), 2, dump.size(), f);
            std::fclose(f);
        }
    if (captured) EnableCursor();  // the game left with the pointer held: give it back
#ifdef __ANDROID__
    for (Texture2D* tx : {&fire_tex, &pause_tex})
        if (tx->id) UnloadTexture(*tx);
#endif
    UnloadRenderTexture(canvas);
    for (auto* rt : {&rt_hd, &rt_orig, &rt_blocks})
        if (rt->id) UnloadRenderTexture(*rt);
    SetWindowTitle("ZX Remakes");
    return exit;
}
