// c++ -std=c++20 -O2 -Isrc tests/music_pitch.cpp src/music_remix.cpp -o /tmp/music_pitch
// /tmp/music_pitch
// Exercise the public AY-to-audio path, without a window or sound device.
#include "music_remix.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

constexpr int SR = 44100, BLOCK = SR / 50;
constexpr double PI = 3.141592653589793;

// Locate a sustained tone in the rendered waveform, allowing its intended
// vibrato, detuning and chorus sidebands. No access to oscillator internals.
double peak_hz(const std::vector<float>& audio, double expected) {
    double best = -1, peak = 0;
    for (double hz = expected - 20; hz <= expected + 20; hz += 0.25) {
        double coeff = 2 * std::cos(2 * PI * hz / SR), a = 0, b = 0;
        for (size_t i = 0; i < audio.size(); i++) {
            double window = 0.5 - 0.5 * std::cos(2 * PI * i / (audio.size() - 1));
            double next = audio[i] * window + coeff * a - b;
            b = a; a = next;
        }
        double power = a * a + b * b - coeff * a * b;
        if (power > best) { best = power; peak = hz; }
    }
    return peak;
}

int main() {
    bool ok = true;
    for (bool lead : {true, false}) {
        MusicRemix remix(SR);
        uint8_t regs[16] = {};
        regs[7] = 0x38;
        int channel = lead ? 2 : 1, period = lead ? 252 : 1008;
        regs[channel * 2] = period & 255;
        regs[channel * 2 + 1] = period >> 8;
        regs[8 + channel] = 15;
        std::vector<float> block(BLOCK * 2), second(SR);
        for (int frame = 0; frame < 180 * 50; frame++) {
            remix.frame(regs, block.data(), BLOCK);
            for (int i = 0; i < BLOCK; i++) {
                if (!std::isfinite(block[i * 2]) || !std::isfinite(block[i * 2 + 1])) {
                    std::fprintf(stderr, "FAIL: non-finite audio\n");
                    return 1;
                }
                second[(frame % 50) * BLOCK + i] = block[i * 2];
            }
            int sec = (frame + 1) / 50;
            if ((frame + 1) % 50 || (sec != 2 && sec != 20 && sec != 80 && sec != 180)) continue;
            // B at ~110 Hz generates an A-minor pad: A3, C4, E4.
            std::vector<double> expected = lead ? std::vector<double>{1773400.0 / (16 * period)}
                                               : std::vector<double>{220, 261.625565, 329.627557};
            for (double target : expected) {
                double actual = peak_hz(second, target);
                bool stable = std::abs(actual - target) < 4;
                std::printf("%s %3ds: expected %.2f Hz, measured %.2f Hz %s\n",
                            lead ? "lead" : "pad ", sec, target, actual, stable ? "OK" : "FAIL");
                ok &= stable;
            }
        }
    }
    return ok ? 0 : 1;
}
