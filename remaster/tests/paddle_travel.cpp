// Integration regression: real snapshot, game adapter and presentation sampling.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>
// Inspect presentation state without adding test-only accessors to the game API.
#define private public
#include "ballbreaker_game.h"
#undef private


#include <cmath>
#include <chrono>
#include <numeric>
#include <cstdlib>

// Hold real inputs all the way to both ends; never inject endpoint coordinates.
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    int failures = 0, checks = 0;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 180, "Paddle travel");
    for (bool original : {false, true}) for (bool mouse : {false, true}) {
        auto s = Spectrum::from_z80(data);
        if (!s) return 2;
        BallBreakerView view;
        view.start(*s, "remaster/assets/ballbreaker");
        view.original_speed_ = false;
        view.unlimited_lives_ = true;
        std::vector<uint8_t> tune;
        for (int frame = 0; frame < 1200; ++frame) {
            // Reach Even's odd-coordinate endpoint before switching. Original
            // must reach both corners on either parity of its two-unit steps.
            if (original && frame == 350) view.on_key(*s, KEY_O);
            Controls c;
            if (frame >= 10 && frame < 20) c.ok = c.fire = true, c.keys.insert(' ');
            if (frame >= 50 && frame < 60) c.ok = true, c.keys.insert(13);
            if (frame >= 200 && frame < 350) c.dirs = ZXC_JOY_RIGHT;
            if (frame >= 350 && frame < 700) c.dirs = ZXC_JOY_LEFT;
            if (frame >= 750) c.dirs = ZXC_JOY_RIGHT;
            if (mouse && c.dirs) {
                c.mouse = true;
                c.pointer = c.dirs == ZXC_JOY_LEFT ? Vector2{900, 1000} : Vector2{2000, 480};
                c.dirs = 0;
            }
            view.input(*s, c);
            s->run_frame();
            view.after_frame(*s);
            if (frame == 300)
                for (int a = 0x5e96; a < 0x6300; ++a) tune.push_back(s->peek(a));
            if (frame == 1199)
                for (int a = 0x5e96; a < 0x6300; ++a)
                    if (s->peek(a) != tune[a - 0x5e96]) ++failures;
            if (frame == 699 || frame == 1199) {
                auto state = view.latest_;
                view.smooth_apply(state, view.game_t_);
                for (const auto& o : state.objects) if (o.slot == 1) {
                    ++checks;
                    auto body = bb_visible_paddle_body(o.x, o.bat_width, o.z, o.bat_inset);
                    printf("original%d mouse%d frame%d native%d width%d shown %.2f..%.2f\n",
                           original, mouse, frame, s->peek(0x8061), s->peek(0x8063), body.x0, body.x1);
                    if (std::abs(frame == 699 ? body.x0 : body.x1 - 96) > .01f) ++failures;
                }
            }
        }
    }
    CloseWindow();
    printf("checks %d failures %d (including tune bytes)\n", checks, failures);
    return failures || checks != 8;
}
