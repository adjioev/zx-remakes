#pragma once

#include <atomic>
#include <cstdint>

#include <raylib.h>

// The sound out through raylib, in stereo: the game loop pushes what each emulated frame made
// (the Spectrum's own sound, or the remake's music, plus its sound effects), raylib's audio
// thread plays it a few frames behind. Nothing is made on the audio thread.
class SpectrumAudio {
public:
    explicit SpectrumAudio(int sample_rate);
    ~SpectrumAudio();
    SpectrumAudio(const SpectrumAudio&) = delete;
    SpectrumAudio& operator=(const SpectrumAudio&) = delete;

    void push(const float* stereo, int frames);  // interleaved L R
    void clear();  // e.g. on pause: nothing stale plays on resume
    void toggle_mute() { muted_ = !muted_; }
    bool muted() const { return muted_; }

private:
    static void callback(void* buffer, unsigned int frames);

    AudioStream stream_{};
    std::atomic<bool> muted_{false};
};
