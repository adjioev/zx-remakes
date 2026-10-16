#include "sfx.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float TWO_PI = 6.2831853f;
constexpr size_t MAX_VOICES = 24;
constexpr float LEVEL = 0.32f;

float lp_coef(float hz, float sr) { return std::exp(-TWO_PI * hz / sr); }
float lp(float& s, float x, float a) { return s = (1 - a) * x + a * s; }
float semis(int n) { return std::pow(2.0f, n / 12.0f); }

// how long each sound lasts (seconds)
float length(Sfx k) {
    switch (k) {
    case Sfx::BatHit: return 0.25f;
    case Sfx::Bounce: return 0.12f;
    case Sfx::Land: return 0.18f;
    case Sfx::Brick: return 0.45f;
    case Sfx::Bomb: return 1.1f;
    case Sfx::Missile: return 0.35f;
    case Sfx::LifeLost: return 0.9f;
    case Sfx::Bonus: return 0.6f;
    case Sfx::LevelStart: return 0.9f;
    }
    return 0.3f;
}

}  // namespace

float SfxBank::noise() {
    rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
    return float(rng_) / 2147483648.0f - 1;
}

void SfxBank::play(const SfxEvent& e) {
    if (voices_.size() >= MAX_VOICES) voices_.erase(voices_.begin());  // the oldest makes way
    voices_.push_back({e});
}

void SfxBank::mix(float* out, int n) {
    const float dt = 1.0f / sr_;
    for (Voice& v : voices_) {
        const float len = length(v.e.kind), up = semis(v.e.pitch);
        const float gl = std::sqrt(0.5f * (1 - v.e.pan)) * 1.41f, gr = std::sqrt(0.5f * (1 + v.e.pan)) * 1.41f;
        for (int i = 0; i < n && v.t < len; i++) {
            float t = v.t += dt, s = 0;
            float att = std::min(1.0f, t / 0.004f);  // no clicks
            switch (v.e.kind) {
            case Sfx::BatHit: {  // a springy "boing": A4 falling to A3, a little body
                float f = (220 + 220 * std::exp(-t / 0.04f)) * up;
                v.ph += f * dt;
                s = (std::sin(TWO_PI * v.ph) + 0.3f * std::sin(TWO_PI * v.ph * 2)) * std::exp(-t / 0.08f) * 0.8f;
                break;
            }
            case Sfx::Bounce: {  // a soft wooden "tok"
                v.ph += 330 * up * dt;
                s = std::sin(TWO_PI * v.ph) * std::exp(-t / 0.03f) * 0.35f;
                break;
            }
            case Sfx::Land: {  // a low thud
                v.ph += (70 + 60 * std::exp(-t / 0.03f)) * dt;
                s = std::sin(TWO_PI * v.ph) * std::exp(-t / 0.06f) * 0.5f;
                break;
            }
            case Sfx::Brick: {  // glassy crunch + a note (higher for more points)
                float nz = noise();
                nz = nz - lp(v.lp, nz, lp_coef(1800, sr_));
                nz = lp(v.lp2, nz, lp_coef(7000, sr_));
                v.ph += 880 * up * dt;
                s = nz * std::exp(-t / 0.07f) * 0.5f + std::sin(TWO_PI * v.ph) * std::exp(-t / 0.12f) * 0.35f;
                break;
            }
            case Sfx::Bomb: {  // a boom: noise through a falling filter, a sub drop
                float nz = lp(v.lp, noise(), lp_coef(2500 * std::exp(-t / 0.25f) + 120, sr_));
                v.ph += (55 + 50 * std::exp(-t / 0.08f)) * dt;
                s = nz * std::exp(-t / 0.35f) * 1.2f + std::sin(TWO_PI * v.ph) * std::exp(-t / 0.4f) * 0.9f;
                break;
            }
            case Sfx::Missile: {  // a "zap": a saw sliding down, filtered
                float f = (300 + 900 * std::exp(-t / 0.08f)) * up;
                v.ph += f * dt;
                float saw = 2 * (v.ph - std::floor(v.ph)) - 1;
                s = lp(v.lp, saw, lp_coef(3000, sr_)) * std::exp(-t / 0.12f) * 0.4f;
                break;
            }
            case Sfx::LifeLost: {  // A minor falling: E4 C4 A3 E3
                const float notes[4] = {329.6f, 261.6f, 220.0f, 164.8f};
                int k = std::min(3, int(t / 0.18f));
                float tk = t - k * 0.18f;
                v.ph += notes[k] * dt;
                float sq = std::sin(TWO_PI * v.ph) + 0.25f * std::sin(TWO_PI * v.ph * 3);
                s = sq * std::exp(-tk / 0.15f) * 0.45f;
                break;
            }
            case Sfx::Bonus:
            case Sfx::LevelStart: {  // rising chime: A4 C5 E5 A5
                const float notes[4] = {440.0f, 523.3f, 659.3f, 880.0f};
                float stepl = v.e.kind == Sfx::Bonus ? 0.08f : 0.14f;
                int k = std::min(3, int(t / stepl));
                float tk = t - k * stepl;
                v.ph += notes[k] * up * dt;
                s = (std::sin(TWO_PI * v.ph) + 0.2f * std::sin(TWO_PI * v.ph * 4)) * std::exp(-tk / 0.25f) * 0.35f;
                break;
            }
            }
            s *= att * v.e.gain * LEVEL;
            out[i * 2] += s * gl, out[i * 2 + 1] += s * gr;
        }
    }
    std::erase_if(voices_, [](const Voice& v) { return v.t >= length(v.e.kind); });
}
