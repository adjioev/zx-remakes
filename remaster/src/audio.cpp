#include "audio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr unsigned RING = 1 << 15;     // stereo frames; a power of two
// The game makes sound by the display's clock, the sound card plays it by its own: the two drift.
// Playback runs a hair faster or slower (at most MAX_ADJUST) to keep TARGET queued, so nothing is
// ever dropped or skipped; only a big jump (the game stalled) resets the queue.
constexpr float TARGET = 2600;         // stereo frames queued: ~60 ms
constexpr float MAX_ADJUST = 0.005f;   // playback rate: +-0.5 % (a few cents, not heard)
constexpr unsigned MAX_QUEUED = 16000; // ~0.36 s: the game ran far ahead
constexpr float VOLUME = 0.8f;
constexpr float DC_BLOCK = 0.995f;     // the beeper and AY output sit off zero

// One ring of stereo frames, written by the game loop, read by the audio thread.
float g_ring[RING * 2];
std::atomic<unsigned> g_w{0}, g_r{0};
std::atomic<bool> g_muted{false};
// debug (ZX_AUDIO_STATS): the queue's level, frames dropped (the game ran ahead), starved (behind)
std::atomic<unsigned> g_dropped{0}, g_starved{0}, g_min{~0u}, g_max{0};
float g_last[2] = {0, 0}, g_dc_in[2] = {0, 0}, g_dc_out[2] = {0, 0};  // audio thread only
double g_frac = 0;     // where playback is between two queued frames
float g_rate = 1;      // playback speed, eased

}  // namespace

SpectrumAudio::SpectrumAudio(int sample_rate) {
    if (!IsAudioDeviceReady()) InitAudioDevice();
    SetAudioStreamBufferSizeDefault(512);
    stream_ = LoadAudioStream(unsigned(sample_rate), 32, 2);
    SetAudioStreamCallback(stream_, callback);
    PlayAudioStream(stream_);
}

SpectrumAudio::~SpectrumAudio() {
    UnloadAudioStream(stream_);
    CloseAudioDevice();
}

void SpectrumAudio::push(const float* stereo, int frames) {
    g_muted = muted_.load();
    unsigned w = g_w.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; i++, w++) {
        g_ring[(w & (RING - 1)) * 2] = stereo[i * 2];
        g_ring[(w & (RING - 1)) * 2 + 1] = stereo[i * 2 + 1];
    }
    g_w.store(w, std::memory_order_release);
    unsigned r = g_r.load(std::memory_order_acquire);
    if (w - r > MAX_QUEUED && g_r.compare_exchange_strong(r, w - unsigned(TARGET))) g_dropped += w - unsigned(TARGET) - r;
    static const bool stats = std::getenv("ZX_AUDIO_STATS") != nullptr;
    if (stats) {
        g_muted = true;
        static double last = GetTime();
        if (GetTime() - last > 2) {
            last = GetTime();
            std::fprintf(stderr, "audio queue %u..%u dropped %u starved %u\n", g_min.load(), g_max.load(), g_dropped.load(), g_starved.load());
            g_min = ~0u, g_max = 0, g_dropped = 0, g_starved = 0;
        }
    }
}

void SpectrumAudio::clear() { g_r.store(g_w.load()); }

void SpectrumAudio::callback(void* buffer, unsigned int frames) {
    float* out = static_cast<float*>(buffer);
    unsigned r = g_r.load(std::memory_order_relaxed), w = g_w.load(std::memory_order_acquire);
    g_min = std::min(g_min.load(), w - r), g_max = std::max(g_max.load(), w - r);
    // faster when more than TARGET is queued, slower when less; eased over ~a second
    float fill = float(w - r);
    float want = 1 + std::clamp((fill - TARGET) / TARGET, -1.0f, 1.0f) * MAX_ADJUST;
    g_rate += (want - g_rate) * 0.02f;
    auto at = [](unsigned i, int c) { return g_ring[(i & (RING - 1)) * 2 + c]; };
    for (unsigned i = 0; i < frames; i++) {
        bool have = w - r >= 2;
        if (!have) g_starved++;
        for (int c = 0; c < 2; c++) {
            float s = have ? (g_last[c] = at(r, c) + (at(r + 1, c) - at(r, c)) * float(g_frac)) : (g_last[c] *= 0.98f);  // dry: fade
            g_dc_out[c] = s - g_dc_in[c] + DC_BLOCK * g_dc_out[c];  // DC blocker
            g_dc_in[c] = s;
            out[i * 2 + c] = g_muted ? 0.0f : std::clamp(g_dc_out[c] * VOLUME, -1.0f, 1.0f);
        }
        if (have) {
            g_frac += g_rate;
            while (g_frac >= 1) g_frac -= 1, r++;
        }
    }
    g_r.store(r, std::memory_order_release);
}
