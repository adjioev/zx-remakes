#include <algorithm>
#include <cstdint>
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
#define private public
#include "ballbreaker_game.h"
#undef private
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 180, "Instructions regression");
    int failures = 0;
    for (bool manual : {false, true}) {
        auto s = Spectrum::from_z80(data);
        if (!s) return 2;
        BallBreakerView v;
        v.start(*s, "remaster/assets/ballbreaker");
        bool entered = false, returned = false, wrong = false;
        s->on_exec(0xAE11, [&](const zxc_regs_t&) { entered = true; });
        s->on_exec(0x9DA7, [&](const zxc_regs_t&) { if (entered) returned = true; });
        for (int frame = 0; frame < 5000 && !returned; ++frame) {
            Controls c;
            if (frame >= 10 && frame < 20) c.ok = c.fire = true, c.keys.insert(' ');
            for (int at : {75, 95, 115, 135, 155})
                if (frame >= at && frame < at+5) c.dirs = ZXC_JOY_DOWN;
            if (frame >= 185 && frame < 195) c.ok = true, c.keys.insert(13);
            if (manual && frame >= 250 && frame < 400) c.dirs = ZXC_JOY_RIGHT;
            if (manual && (frame == 450 || frame == 500)) c.dirs = ZXC_JOY_LEFT;
            v.input(*s, c); s->run_frame(); v.after_frame(*s);
            if (manual && (frame == 350 || frame == 450 || frame == 1000)) {
                BBMenu menu; v.read_menu(*s, menu);
                int pages = int((menu.items.size()+BBMenu::INSTRUCTIONS_PER_PAGE-1)/BBMenu::INSTRUCTIONS_PER_PAGE);
                int expected = frame == 350 ? 1 : frame == 450 ? 0 : pages-1;
                if (menu.instructions_page != expected || menu.screen != BBMenu::Screen::Instructions) {
                    ++failures; std::fprintf(stderr, "FAIL: manual page at frame %d\n", frame);
                }
            }
            if (entered && !returned && v.screen_ != BBMenu::Screen::Instructions) wrong = true;
        }
        if (!entered || !returned || wrong || v.screen_ != BBMenu::Screen::Main) {
            std::fprintf(stderr, "FAIL: entered=%d returned=%d wrong_screen=%d final_screen=%d\n",
                         entered, returned, wrong, int(v.screen_)); ++failures;
        }
        BBMenu menu; v.read_menu(*s, menu);
        if (menu.instructions_page != -1) { ++failures; std::puts("FAIL: manual mode not reset on exit"); }
        if (menu.items.empty() || menu.items.size() >= 64) { ++failures; std::puts("FAIL: instruction items"); }
    }
    for (int item = 0; item < 4; ++item) {
        auto s = Spectrum::from_z80(data);
        BallBreakerView v; v.start(*s, "remaster/assets/ballbreaker");
        const BBMenu::Screen expected[] = {BBMenu::Screen::LevelCard, BBMenu::Screen::Controls,
                                          BBMenu::Screen::Keys, BBMenu::Screen::HiScores};
        int select = -1;
        bool reached = false;
        for (int frame = 0; frame < 700; ++frame) {
            Controls c;
            if (frame >= 10 && frame < 20) c.ok = c.fire = true, c.keys.insert(' ');
            if (frame >= 75 && select < 0) {
                if (v.cursor_ == item) select = frame + 25;
                else if ((frame-75)%40 < 20) c.dirs = ZXC_JOY_DOWN;
            }
            if (select >= 0 && frame >= select && frame < select+10)
                c.ok = true, c.keys.insert(13);
            v.input(*s, c); s->run_frame(); v.after_frame(*s);
            if (select >= 0 && frame > select && v.screen_ == expected[item]) reached = true;
            if (select >= 0 && frame >= select+100) break;
        }
        if (!reached) { ++failures; std::fprintf(stderr, "FAIL: neighbouring menu item %d\n", item); }
    }
    CloseWindow();
    if (!failures) std::puts("PASS: Instructions stays visible through the original demonstration and returns to menu");
    return failures ? 1 : 0;
}
