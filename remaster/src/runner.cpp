// The loop every game runs in: the original in the emulator at 50 Hz, input, sound, the
// pause screen, and test scripts; the game's module (GameView) reads its state and draws.
//
// Controls: the keyboard and gamepad become Controls (game.h), which the game's module turns
// into what the original reads. Remake keys: Esc / gamepad Start pause (there R original / HD,
// M sound, F full screen, X / Select back to the game list), Tab original / HD at any time,
// F11 full screen.
//
//   zxremakes --test <out_dir> [--game <key>] [script]
//     Script tokens: "<sec>+<key>", "<sec>-<key>", "<sec>@<cmd>"; sec = seconds of game time
//     (50 frames each). Keys are the player's, as on the PC: a-z 0-9 space enter ctrl shift
//     alt backspace left right up down, and the gamepad pa pb px (buttons), pl pr pu pd.
//     Commands: shot (canvas -> shot_<n>.png), ram (64 KB as paged + the banks ->
//     ram_<n>.bin), orig / hd (the R view), poke<addr>=<value> (hex, e.g. poke8078=2),
//     pokeat<pc>:<addr>=<value> (once, the next time the CPU gets to pc), quit.
//     The run ends at the last event.

#include <algorithm>
#include <cstdio>
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

void draw_pause(Vector2 size, const std::string& title, const std::string& help, bool original, bool muted) {
    DrawRectangle(0, 0, int(size.x), int(size.y), Fade(BLACK, 0.7f));
    auto centred = [&](const std::string& t, float y, int px, Color c) {
        DrawText(t.c_str(), int(size.x / 2 - float(MeasureText(t.c_str(), px)) / 2), int(y), px, c);
    };
    float y = size.y * 0.25f;
    centred(title, y, 90, {255, 214, 96, 255});
    centred("PAUSED", y + 120, 50, RAYWHITE);
    centred("ESC  continue      X  back to the game list", y + 230, 40, RAYWHITE);
    centred(std::string("R  graphics: ") + (original ? "original 1987" : "HD") + "      M  sound: " +
                (muted ? "off" : "on") + "      F  full screen",
            y + 290, 40, RAYWHITE);
    centred("Tab  original / HD      F11  full screen      gamepad: Start pause, Select back", y + 350, 32,
            Fade(RAYWHITE, 0.7f));
    std::istringstream lines(help);
    float hy = y + 430;
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

    Held script_held;
    Pad pad;
    bool original = false, paused = false;
    GameExit exit = GameExit::Quit;
    size_t next_event = 0;
    int shots = 0, dumps = 0;
    double acc = 0;
    std::vector<float> samples(4096);

    auto frame = [&] {
        spec->run_frame();
        view->after_frame(*spec);
        int n;
        while ((n = spec->audio(samples.data(), int(samples.size()))) > 0)
            if (audio) audio->push(samples.data(), n);
    };
    auto render = [&] {
        // game time, continuous: the frames run plus the time not yet run (scripted runs: whole
        // frames, repeatable shots); smoothing and effects go by it (and stop on pause)
        double now = double(spec->frames()) / Spectrum::FRAME_HZ + (test_dir.empty() ? acc : 0.0);
        view->prepare(*spec, original, now);
        BeginTextureMode(canvas);
        ClearBackground(BLACK);
        view->draw(*spec, original, now);
        if (paused) draw_pause(csize, info.title, view->help(), original, audio && audio->muted());
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
                } else if (e.arg == "orig") original = true;
                else if (e.arg == "hd") original = false;
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
        if (IsKeyPressed(KEY_TAB)) original = !original;
        if (paused) {
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || pad.pressed(Pad::START)) paused = false;
            if (IsKeyPressed(KEY_R)) original = !original;
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
            view->input(*spec, Controls{});  // let go of everything
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
    UnloadRenderTexture(canvas);
    SetWindowTitle("ZX Remakes");
    return exit;
}
