#pragma once

// A game's AY tune re-arranged live, from the AY registers the game's own player writes every
// frame: its notes and their attacks drive new instruments, so the arrangement is always in step
// with the game. Made on the game loop's thread, one emulated frame at a time.
//
// Ball Breaker's tune (notes/ballbreaker.md): channel B the bass, C the tune, A the original's
// percussion (not used: its noisy ticks); a note every 7 frames, 4 to a beat, in A minor. The
// arrangement is synthpop: an eighth-note bass with octaves and a sub, Juno-style PWM chords with
// light chorus alternating stabs with DX7-ish electric piano, sparse arpeggio fills, a PWM lead with
// glide, chorus and echo; kick on every beat, a soft snare on 2 and 4, a shaker on the off-beats.
// The grid counts from the tune's first note (after a silence), so it starts from the top.

#include <array>
#include <cstdint>
#include <vector>

class MusicRemix {
public:
    explicit MusicRemix(int sample_rate);

    // One emulated frame: the AY registers after it; n stereo frames (interleaved) to make, or
    // nullptr to only follow the tune (when the original sound is being played).
    void frame(const uint8_t regs[16], float* out, int n);
    // The AY is playing the tune (its bass on the 7-frame grid lately); otherwise (e.g. the
    // game's sound effects mode) the original sound should be played.
    bool playing() const;
    // The game starts its tune from the top: the next note is the start (the grid counts from it).
    void restart();

private:
    struct Voice {  // a note with an envelope: bass, arpeggio, stab, piano, drums
        bool on = false;
        float freq = 0, t = 0, phase = 0, phase2 = 0, lp = 0, lp2 = 0, hp = 0;
        float notes[3] = {0, 0, 0};
        float nph[3] = {0, 0, 0};
        int kind = 0;
    };
    void step(int k, int in_beat);  // k: steps so far; in_beat: 0-3, its place in the beat
    void follow(uint64_t frame);    // the tune at this frame (the next ones known): the grid, the notes
    void render(float* out, int n);
    struct Snap {  // a frame of the AY: per channel
        float freq[3] = {0, 0, 0};
        uint8_t vol[3] = {0, 0, 0};
        bool attack[3] = {false, false, false};
    };
    static constexpr size_t HIST = 8;
    std::array<Snap, HIST> hist_{};
    uint64_t in_ = 0;      // frames seen
    int lead_shift_ = 0;   // the lead's notes this step: this many frames late
    float drift_ = 0;      // the tune against the grid (frames, smoothed)
    float noise();

    float sr_;
    uint64_t frame_ = 0;
    // the tune, as the AY plays it
    uint8_t prev_vol_[3] = {0, 0, 0};
    bool prime_after_restart_ = false;  // AY may still hold the last menu note
    float held_bass_ = 0, lead_freq_ = 0, lead_target_ = 0, lead_vol_ = 0, lead_vol_target_ = 0;

    bool started_ = false;  // the tune is on: its first note came at start_ (after a silence)
    uint64_t start_ = 0;
    int silent_ = 0, on_grid_ = 0, restart_wait_ = 0;  // silent frames in a row; notes on the grid in a row
    // instruments
    Voice bass_, arp_, stab_, ep_, kick_, snare_, shaker_;
    float pad_ph_[3] = {0, 0, 0}, pad_notes_[3] = {0, 0, 0}, pad_amp_ = 0, pad_lp_ = 0, pad_hp_ = 0;
    float stab_lp_ = 0, stab_hp_ = 0;
    float lead_ph_ = 0, lead_ph2_ = 0, lead_lp_ = 0, lead_hp_ = 0;
    uint64_t rendered_samples_ = 0;  // exact clock for chorus and vibrato
    float last_output_[2] = {}, restart_tail_[2] = {};
    float restart_gain_ = 1;
    // effects: chorus / echo delay lines, a small reverb
    std::vector<float> keys_dl_, lead_dl_, echo_dl_;
    size_t dl_pos_ = 0;
    struct Comb {
        std::vector<float> buf;
        size_t pos = 0;
        float store = 0;
    };
    struct Allpass {
        std::vector<float> buf;
        size_t pos = 0;
    };
    Comb combs_[2][4];
    Allpass allpass_[2][2];
    float rev_hp_[2] = {0, 0};
    uint32_t rng_ = 22222;
    int step_k_ = 0;
};
