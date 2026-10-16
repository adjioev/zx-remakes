#include "music_remix.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Exercise the public renderer: a restarted tune must not inherit the menu's
// notes, lookahead, instruments, delay lines or modulation state.
int main() {
    constexpr int N = 882;
    bool ok = true;
    {
        MusicRemix remix(44100);
        uint8_t stale[16] = {};
        stale[4] = 252; stale[10] = 15;
        std::vector<float> block(N * 2);
        remix.restart();
        for (int f = 0; f < 30; ++f) remix.frame(stale, block.data(), N);
        // A register value left over from the menu is not a new note attack.
        bool pass = !remix.playing();
        std::printf("stale AY after restart: %s\n", pass ? "OK" : "FAIL");
        ok &= pass;
    }
    for (bool silent : {true, false}) {
        MusicRemix used(44100), fresh(44100);
        std::vector<float> a(N * 2), b(N * 2);
        uint8_t menu[16] = {};
        menu[2] = 240; menu[3] = 3; menu[9] = 15;
        menu[4] = 252; menu[10] = 15;
        for (int f = 0; f < 200; ++f) used.frame(menu, a.data(), N);
        used.restart();
        fresh.restart();
        float error = 0, peak = 0;
        for (int f = 0; f < 150; ++f) {
            uint8_t regs[16] = {};
            if (!silent) {
                regs[2] = 240; regs[3] = 3;
                regs[4] = 126;
                regs[9] = regs[10] = (f % 7 == 0 ? 15 : 10);
            }
            used.frame(regs, a.data(), N);
            fresh.frame(regs, b.data(), N);
            for (int i = 0; i < N * 2; ++i) {
                if (!std::isfinite(a[i])) return 1;
                // Allow the short transition from the previous output sample.
                if (f >= 2) error = std::max(error, std::abs(a[i] - b[i]));
                peak = std::max(peak, std::abs(b[i]));
            }
        }
        bool pass = error < 1e-6f && (silent || peak > 0.01f);
        std::printf("restart into %s: max difference %.8f %s\n",
                    silent ? "silence" : "new tune", error, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    return ok ? 0 : 1;
}
