#pragma once

#include <atomic>
#include <cstdint>

#include <raylib.h>

// The Spectrum's sound (beeper + AY, already mixed by the emulator) out through raylib: the
// game loop pushes what each emulated frame produced, raylib's audio thread plays it a few
// frames behind.
class SpectrumAudio {
public:
    explicit SpectrumAudio(int sample_rate);
    ~SpectrumAudio();
    SpectrumAudio(const SpectrumAudio&) = delete;
    SpectrumAudio& operator=(const SpectrumAudio&) = delete;

    void push(const float* samples, int n);
    void clear();  // e.g. on pause: nothing stale plays on resume
    void toggle_mute() { muted_ = !muted_; }
    bool muted() const { return muted_; }

private:
    static void callback(void* buffer, unsigned int frames);

    AudioStream stream_{};
    std::atomic<bool> muted_{false};
};
