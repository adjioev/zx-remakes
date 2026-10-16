#include "audio.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {

constexpr unsigned RING = 1 << 15;     // samples; a power of two
constexpr unsigned MAX_QUEUED = 6000;  // ~0.14 s: more means the game ran ahead, drop it
constexpr float VOLUME = 0.8f;
constexpr float DC_BLOCK = 0.995f;     // the beeper and AY output sit off zero

// One ring, written by the game loop, read by the audio thread.
float g_ring[RING];
std::atomic<unsigned> g_w{0}, g_r{0};
std::atomic<bool> g_muted{false};
float g_last = 0, g_dc_in = 0, g_dc_out = 0;  // audio thread only

}  // namespace

SpectrumAudio::SpectrumAudio(int sample_rate) {
    if (!IsAudioDeviceReady()) InitAudioDevice();
    SetAudioStreamBufferSizeDefault(512);
    stream_ = LoadAudioStream(unsigned(sample_rate), 32, 1);
    SetAudioStreamCallback(stream_, callback);
    PlayAudioStream(stream_);
}

SpectrumAudio::~SpectrumAudio() {
    UnloadAudioStream(stream_);
    CloseAudioDevice();
}

void SpectrumAudio::push(const float* samples, int n) {
    g_muted = muted_.load();
    unsigned w = g_w.load(std::memory_order_relaxed);
    for (int i = 0; i < n; i++) g_ring[w++ & (RING - 1)] = samples[i];
    g_w.store(w, std::memory_order_release);
    unsigned r = g_r.load(std::memory_order_acquire);
    if (w - r > MAX_QUEUED) g_r.compare_exchange_strong(r, w - MAX_QUEUED / 2);
}

void SpectrumAudio::clear() { g_r.store(g_w.load()); }

void SpectrumAudio::callback(void* buffer, unsigned int frames) {
    float* out = static_cast<float*>(buffer);
    unsigned r = g_r.load(std::memory_order_relaxed), w = g_w.load(std::memory_order_acquire);
    for (unsigned i = 0; i < frames; i++) {
        float s;
        if (r != w) s = g_last = g_ring[r++ & (RING - 1)];
        else s = g_last *= 0.98f;  // ran dry: fade instead of a click
        // DC blocker
        g_dc_out = s - g_dc_in + DC_BLOCK * g_dc_out;
        g_dc_in = s;
        out[i] = g_muted ? 0.0f : std::clamp(g_dc_out * VOLUME, -1.0f, 1.0f);
    }
    g_r.store(r, std::memory_order_release);
}
