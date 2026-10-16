// The loop every game runs in: the original in the emulator at 50 Hz, input, sound, the
// pause screen, and test scripts; the game's module (GameView) reads its state and draws.
//
// Controls: the keyboard and gamepad become Controls (game.h), which the game's module turns
// into what the original reads. Remake keys: Esc / gamepad Start pause (there R original / HD,
// M sound, F full screen, X / Select back to the game list, and for games with levels LEFT /
// RIGHT + ENTER to jump to one), Tab original / HD at any time, R too while the game reads no
// letters (GameView::letters_free), F11 full screen. Switching views dissolves one into the
// other through growing and shrinking pixels (as in Opera Soft Remastered).
//
//   zxremakes --test <out_dir> [--game <key>] [script]
//     Script tokens: "<sec>+<key>", "<sec>-<key>", "<sec>@<cmd>"; sec = seconds of game time
//     (50 frames each). Keys are the player's, as on the PC: a-z 0-9 space enter ctrl shift
//     alt backspace left right up down, and the gamepad pa pb px (buttons), pl pr pu pd.
//     Commands: shot (canvas -> shot_<n>.png), ram (64 KB as paged + the banks ->
//     ram_<n>.bin), orig / hd (the R view, at once), r (as the key: the dissolve), level<n> (jump
//     there, 1-based, if the game can now), key<letter> (a game option key, e.g. keyl), poke<addr>=<value> (hex, e.g. poke8078=2),
//     pokeat<pc>:<addr>=<value> (once, the next time the CPU gets to pc), quit.
//     The run ends at the last event.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    bool* btn = name == "pa" ? &h.pa : name == "pb" ? &h.pb : name == "px" ? &h.px : nullptr;
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
    for (int i = 0; i < 26; i++)
        if (on(KEY_A + i)) c.keys.insert('a' + i);
    for (int i = 0; i < 10; i++)
        if (on(KEY_ZERO + i)) c.keys.insert('0' + i);
    if (on(KEY_SPACE)) c.keys.insert(' ');
    if (enter) c.keys.insert(0x0D);
    if (on(KEY_LEFT_SHIFT) || on(KEY_RIGHT_SHIFT)) c.keys.insert(0x0E);
    if (on(KEY_LEFT_ALT) || on(KEY_RIGHT_ALT)) c.keys.insert(0x0F);
    if (on(KEY_BACKSPACE)) c.keys.insert(0x0E), c.keys.insert('0');  // DELETE = CAPS SHIFT + 0
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

void draw_pause(Vector2 size, const std::string& title, const std::string& help, bool original, bool muted,
                int picker, int levels) {
    DrawRectangle(0, 0, int(size.x), int(size.y), Fade(BLACK, 0.7f));
    auto centred = [&](const std::string& t, float y, int px, Color c) {
        DrawText(t.c_str(), int(size.x / 2 - float(MeasureText(t.c_str(), px)) / 2), int(y), px, c);
    };
    float y = size.y * 0.25f;
    centred(title, y, 90, {255, 214, 96, 255});
    centred("PAUSED", y + 120, 50, RAYWHITE);
    centred("ESC  continue      X  back to the game list", y + 230, 40, RAYWHITE);
    centred(std::string("R  ") + (original ? "original 1987" : "remake") + " (picture and sound)      M  sound: " +
                (muted ? "off" : "on") + "      F  full screen",
            y + 290, 40, RAYWHITE);
    centred("Tab  original / HD      F11  full screen      gamepad: Start pause, Select back", y + 350, 32,
            Fade(RAYWHITE, 0.7f));
    float hy = y + 430;
    if (picker >= 0) {  // jump to another level
        char buf[96];
        std::snprintf(buf, sizeof buf, "SCREEN   <  %d  >   of %d      LEFT / RIGHT  choose      ENTER  go", picker + 1, levels);
        centred(buf, y + 410, 40, {255, 214, 96, 255});
        hy += 50;
    }
    std::istringstream lines(help);
    for (std::string l; std::getline(lines, l); hy += 44) centred(l, hy, 32, Fade(RAYWHITE, 0.7f));
}

}  // namespace

GameExit run_game(const LoadedGame& game, const std::string& assets, const std::string& test_dir,
                  const std::string& script_text) {
    const GameInfo& info = game_info(game.variant->game);
    std::vector<ScriptEvent> script;
    if (!test_dir.empty()) {
        try {
            script = parse_script(script_text.empty() ? DEFAULT_SCRIPT : script_text);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return GameExit::Quit;
        }
    }
    std::unique_ptr<Spectrum> spec = Spectrum::from_z80(game.data);
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
        toast(original ? "ORIGINAL 1987" : "REMASTERED");
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
        if (remix && remastered_sound && remix->playing()) remix->frame(regs, stereo.data(), n);
        else {
            if (remix) remix->frame(regs, nullptr, n);  // keep following the tune
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
            if (remastered_sound) sfx.play(e);
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
            draw_pause(csize, info.title, view->help(), original, audio && audio->muted(), picker, view->levels());
        if (wall < toast_until) {
            float a = std::min(1.0f, float(toast_until - wall) / 0.4f);
            int w = MeasureText(toast_msg.c_str(), 40) + 60;
            DrawRectangleRounded({(csize.x - float(w)) / 2, 40, float(w), 64}, 0.4f, 8, Fade(BLACK, 0.7f * a));
            DrawText(toast_msg.c_str(), int((csize.x - float(w)) / 2) + 30, 52, 40, Fade(RAYWHITE, a));
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
                } else if (e.arg == "orig") original = shown_original = true, flip = 1, set_sound();  // at once
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
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (flip < 1) flip = std::min(1.0f, flip + GetFrameTime() / FLIP_SECONDS);
        if (IsKeyPressed(KEY_TAB)) toggle_view();
        if (paused) {
            int n = view->levels();
            if (picker >= 0 && n > 0) {
                if (IsKeyPressed(KEY_LEFT) || pad.pressed(Pad::LEFT)) picker = (picker + n - 1) % n;
                if (IsKeyPressed(KEY_RIGHT) || pad.pressed(Pad::RIGHT)) picker = (picker + 1) % n;
            }
            if ((IsKeyPressed(KEY_ENTER) || pad.pressed(Pad::A)) && picker >= 0 && picker != view->level(*spec)) {
                view->jump_to(*spec, picker);
                char buf[32];
                std::snprintf(buf, sizeof buf, "SCREEN %d", picker + 1);
                toast(buf);
                paused = false;
            } else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || pad.pressed(Pad::START))
                paused = false;
            if (IsKeyPressed(KEY_R)) toggle_view();
            for (int k = KEY_A; k <= KEY_Z; k++)  // the game's options (the runner's own letters aside)
                if (k != KEY_R && k != KEY_M && k != KEY_F && k != KEY_X && IsKeyPressed(k))
                    if (std::string m = view->on_key(*spec, k); !m.empty()) toast(m);
            if (IsKeyPressed(KEY_M) && audio) audio->toggle_mute();
            if (IsKeyPressed(KEY_F)) ToggleBorderlessWindowed();
            if (IsKeyPressed(KEY_X) || pad.pressed(Pad::SELECT)) {
                exit = GameExit::Back;
                break;
            }
            if (!paused) {
                acc = 0;
                if (audio) audio->clear();
            }
        } else if (IsKeyPressed(KEY_ESCAPE) || pad.pressed(Pad::START)) {
            paused = true;
            picker = view->levels() > 0 && view->can_jump() ? view->level(*spec) : -1;
            view->input(*spec, Controls{});  // let go of everything
        } else if (IsKeyPressed(KEY_R) && view->letters_free()) {
            toggle_view();  // the game reads no letters now: R is the remake's
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
            Controls c = controls_of(h);

            acc += GetFrameTime();
            const double step = 1.0 / Spectrum::FRAME_HZ;
            for (int n = 0; acc >= step && n < 4; n++, acc -= step) {
                view->input(*spec, c);
                frame();
            }
            if (acc > step) acc = 0;  // too far behind (e.g. the window was dragged): skip
        }

        render();
        BeginDrawing();
        ClearBackground(BLACK);
        float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
        float scale = std::min(sw / csize.x, sh / csize.y);
        Rectangle dest{(sw - csize.x * scale) / 2, (sh - csize.y * scale) / 2, csize.x * scale, csize.y * scale};
        DrawTexturePro(canvas.texture, {0, 0, csize.x, -csize.y}, dest, {0, 0}, 0, WHITE);
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
    UnloadRenderTexture(canvas);
    for (auto* rt : {&rt_hd, &rt_orig, &rt_blocks})
        if (rt->id) UnloadRenderTexture(*rt);
    SetWindowTitle("ZX Remakes");
    return exit;
}
