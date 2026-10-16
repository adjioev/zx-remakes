#include "music_remix.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr float TWO_PI = 6.2831853f;
constexpr float AY_CLOCK = 1773400;      // 128K AY: 3.5469 MHz / 2
constexpr int STEP = 7;                  // frames: a note step (4 to a beat)
constexpr int LOOKAHEAD = 3;  // frames the arrangement plays behind the AY: a late note still lands on its step
constexpr int SILENCE = 25;               // frames of silence that end the tune (and mark its next start)
constexpr float MASTER = 1.9f;           // output level: about the original's loudness

// A minor (A B C D E F G from A3): the tune's key; chords are its triads on the bass note.
constexpr int A_MINOR[7] = {57, 59, 60, 62, 64, 65, 67};

float hz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12); }
float midi_of(float f) { return 69 + 12 * std::log2(std::max(f, 1.0f) / 440); }

void triad(float root_hz, float out[3]) {
    int pc = int(std::lround(midi_of(root_hz))) % 12;
    int deg = -1;
    for (int i = 0; i < 7; i++)
        if (A_MINOR[i] % 12 == pc) deg = i;
    int n[3];
    if (deg < 0) {
        int r = int(std::lround(midi_of(root_hz)));
        n[0] = r, n[1] = r + 4, n[2] = r + 7;
    } else
        for (int k = 0; k < 3; k++) n[k] = A_MINOR[(deg + 2 * k) % 7] + 12 * ((deg + 2 * k) / 7);
    while (std::min({n[0], n[1], n[2]}) < 57)
        for (int& m : n) m += 12;
    while (std::min({n[0], n[1], n[2]}) > 64)
        for (int& m : n) m -= 12;
    for (int k = 0; k < 3; k++) out[k] = float(n[k]);
}

// Keep phase within one cycle. Unbounded float phases lose the small per-sample
// increment within seconds/minutes, detuning each voice by a different amount.
void advance_phase(float& phase, float cycles) {
    phase += cycles;
    phase -= std::floor(phase);
}

// band-limited oscillators (PolyBLEP): no aliasing whistle
float blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1;
    }
    if (t > 1 - dt) {
        t = (t - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}
float saw(float ph, float dt) {
    float t = ph - std::floor(ph);
    return 2 * t - 1 - blep(t, dt);
}
float pwm(float ph, float dt, float width) { return 0.5f * (saw(ph, dt) - saw(ph + width, dt)); }
float square(float ph, float dt) { return pwm(ph, dt, 0.5f); }

float lp_coef(float hz, float sr) { return std::exp(-TWO_PI * hz / sr); }
float lp(float& s, float x, float a) { return s = (1 - a) * x + a * s; }
float hp(float& s, float x, float a) { return x - lp(s, x, a); }

}  // namespace

MusicRemix::MusicRemix(int sample_rate) : sr_(float(sample_rate)) {
    keys_dl_.assign(size_t(sr_ * 0.05f), 0);
    lead_dl_.assign(size_t(sr_ * 0.05f), 0);
    echo_dl_.assign(size_t(sr_ * 1.0f), 0);
    const int comb_len[4] = {1116, 1188, 1277, 1356}, ap_len[2] = {556, 441};
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 4; i++) combs_[c][i].buf.assign(size_t((comb_len[i] + 23 * c) * sr_ / 44100), 0);
        for (int i = 0; i < 2; i++) allpass_[c][i].buf.assign(size_t((ap_len[i] + 23 * c) * sr_ / 44100), 0);
    }
}

float MusicRemix::noise() {
    rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
    return float(rng_) / 2147483648.0f - 1;
}

bool MusicRemix::playing() const {
    // the tune: started after a silence, its notes since then on the 7-frame grid (a note off it,
    // e.g. the game's sound effects mode: the original's sound until 4 are on it again)
    return (started_ && on_grid_ >= 4) || restart_wait_ > 0;  // stay in remix mode while awaiting the first note
}

void MusicRemix::restart() {
    // Restart runs on the game thread. Recreate all state, including lookahead,
    // held chords and effect buffers, exactly as for a fresh player.
    const float left = last_output_[0], right = last_output_[1];
    *this = MusicRemix(int(sr_));
    silent_ = SILENCE;
    restart_wait_ = SILENCE;
    prime_after_restart_ = true;
    // Join the previous output continuously to the new intro over 20 ms.
    restart_tail_[0] = left;
    restart_tail_[1] = right;
    restart_gain_ = 0;
}

void MusicRemix::step(int k, int in_beat) {
    // Bass on eighths; stabs and piano answer each other on alternate off-steps.
    if (held_bass_ <= 0) return;
    float notes[3];
    triad(held_bass_, notes);
    for (int i = 0; i < 3; i++) pad_notes_[i] = hz(notes[i]);
    if (in_beat % 2 == 0) {
        int eighth = k / 2;  // root, octave, root, octave
        float base = held_bass_ / (held_bass_ < 70 ? 1 : 2);
        bass_.on = true, bass_.t = 0;
        bass_.freq = base * (eighth % 2 ? 2 : 1);
        bass_.notes[0] = base;  // the sub stays on the root
    } else if (in_beat == 1) {
        stab_.on = true, stab_.t = 0;
    } else {
        ep_.on = true, ep_.t = 0;
        for (int i = 0; i < 3; i++) ep_.notes[i] = hz(notes[i] + 12);
    }
    // A short fill on the last beat of each bar leaves room for the melody.
    if (k % 16 >= 12 && in_beat % 2 == 0) {
        float arp_notes[4] = {notes[0], notes[1], notes[2], notes[0] + 12};
        arp_.on = true, arp_.t = 0;
        arp_.freq = hz(arp_notes[in_beat] + 12);
        arp_.kind = in_beat;  // its place in the stereo
    }
}

void MusicRemix::frame(const uint8_t regs[16], float* out, int n) {
    // the tune as the AY plays it now; the arrangement follows it LOOKAHEAD frames behind
    Snap& in = hist_[in_ % HIST];
    for (int c = 0; c < 3; c++) {
        int per = regs[2 * c] | (regs[2 * c + 1] & 15) << 8;
        in.freq[c] = per > 0 ? AY_CLOCK / (16.0f * per) : 0;
        in.vol[c] = regs[8 + c] & 15;
        in.attack[c] = !prime_after_restart_ && in.vol[c] > prev_vol_[c] + 2;
        prev_vol_[c] = in.vol[c];
    }
    prime_after_restart_ = false;
    in_++;
    if (in_ > LOOKAHEAD) follow(in_ - 1 - LOOKAHEAD);
    if (!out) return;
    render(out, n);
}

void MusicRemix::follow(uint64_t cur) {
    // The game's player misses a frame now and then (the game busy, its interrupt late): its notes
    // come a frame or three late, and early again. The arrangement keeps its own steady grid and
    // sees the notes ahead, so a late one still lands on its step; the grid itself only moves when
    // the tune has drifted for good (a phase lock).
    const Snap& s = hist_[cur % HIST];
    bool attack = s.attack[0] || s.attack[1] || s.attack[2];
    // the tune starts with the first note after a silence: the grid counts from there (bars,
    // beats, the snare on 2 and 4), so the arrangement starts from the top with it
    if (s.vol[0] + s.vol[1] + s.vol[2] == 0) {
        if (++silent_ >= SILENCE) started_ = false;
    } else
        silent_ = 0;
    if (restart_wait_ > 0) restart_wait_--;
    if (attack && !started_)
        restart_wait_ = 0, started_ = true, start_ = cur, step_k_ = 0, held_bass_ = 0, on_grid_ = 4, drift_ = 0;
    else if (attack && started_) {
        int off = int((cur - start_) % STEP);
        if (off > STEP / 2) off -= STEP;  // -3..3: early / late
        if (std::abs(off) <= 2) {
            on_grid_ = std::min(on_grid_ + 1, 8);
            drift_ = 0.7f * drift_ + 0.3f * float(off);
            if (drift_ >= 0.7f && cur > start_) start_++, drift_ -= 1;  // the tune lags for good: a step a frame longer
            else if (drift_ <= -0.7f) start_--, drift_ += 1;
        } else
            on_grid_ = std::max(0, on_grid_ - 2);  // (the sound effects mode: no grid at all)
    }
    if (s.vol[1] > 0 && s.freq[1] > 0) held_bass_ = s.freq[1];
    frame_++;

    // the grid: a step every 7 frames, a beat every 4, from the tune's start. The drums play from
    // the top (the original's intro is its percussion alone), the bass and chords come in with it.
    // Steps are counted, so a grid moved back does not play one twice.
    if (started_ && cur >= start_ && (cur - start_) % STEP == 0 && (cur - start_) / STEP >= uint64_t(step_k_)) {
        // the notes due at this step, though a frame or three late: their pitch now
        lead_shift_ = 0;
        for (int d = 0; d <= LOOKAHEAD; d++) {
            const Snap& f = hist_[(cur + uint64_t(d)) % HIST];
            if (f.attack[1] && f.freq[1] > 0) {
                held_bass_ = f.freq[1];
                break;
            }
        }
        for (int d = 0; d <= LOOKAHEAD; d++)
            if (hist_[(cur + uint64_t(d)) % HIST].attack[2]) {
                lead_shift_ = d;
                break;
            }
        int k = step_k_++, in_beat = k % 4;
        static const bool log = std::getenv("ZX_MUSIC_LOG") != nullptr;
        if (log) std::fprintf(stderr, "step %d frame %llu drift %.2f\n", k, (unsigned long long)cur, drift_);
        step(k, in_beat);
        if (in_beat == 0) {
            kick_.on = true, kick_.t = 0, kick_.phase = 0;
            if ((k / 4) % 2 == 1) snare_.on = true, snare_.t = 0, snare_.phase = 0;  // 2 and 4
        }
        if (in_beat == 2) shaker_.on = true, shaker_.t = 0;
    }
    // the lead (the tune's own line), as the AY plays it from where its note is due
    const Snap& ls = hist_[(cur + uint64_t(lead_shift_)) % HIST];
    lead_target_ = ls.vol[2] > 0 ? ls.freq[2] : lead_target_;
    lead_vol_target_ = ls.vol[2] / 15.0f;
}

void MusicRemix::render(float* out, int n) {
    const float dt = 1.0f / sr_;
    const float a_pad = lp_coef(2200, sr_), a_pad_hp = lp_coef(180, sr_), a_stab = lp_coef(3800, sr_),
                a_stab_hp = lp_coef(220, sr_), a_ep_hp = lp_coef(250, sr_), a_arp = lp_coef(2600, sr_),
                a_arp_hp = lp_coef(400, sr_), a_lead_hp = lp_coef(180, sr_), a_glide = lp_coef(25, sr_),
                a_lvol = lp_coef(30, sr_), a_rev_hp = lp_coef(250, sr_), a_sn_lp = lp_coef(3500, sr_),
                a_sn_hp = lp_coef(900, sr_), a_sh_lp = lp_coef(9000, sr_), a_sh_hp = lp_coef(4500, sr_);
    const size_t keys_n = keys_dl_.size(), echo_n = echo_dl_.size();
    for (int i = 0; i < n; i++) {
        // Derive modulation time from an exact sample count, not repeated float
        // additions. Use double through the sine arguments so long sessions keep
        // the intended chorus/vibrato rates.
        const double lfo_t = double(++rendered_samples_) / double(sr_);
        float L = 0, R = 0;
        // bass: pulse + saw with a plucked filter, a sub on the root
        if (bass_.on) {
            float t = bass_.t += dt;
            float f = bass_.freq;
            advance_phase(bass_.phase, f * dt);
            advance_phase(bass_.phase2, f * 1.003f * dt);
            advance_phase(bass_.lp2, bass_.notes[0] * dt);  // sub oscillator phase
            float osc = 0.5f * square(bass_.phase, f * dt) + 0.5f * saw(bass_.phase2, f * 1.003f * dt);
            osc = lp(bass_.lp, osc, lp_coef(180 + 1500 * std::exp(-t / 0.06f), sr_));
            float env = std::min(1.0f, t / 0.003f) * std::exp(-t / 0.25f);
            float b = (osc * 0.55f + 0.85f * std::sin(TWO_PI * bass_.lp2)) * env * 0.6f;
            L += b, R += b;
            if (t > 0.8f) bass_.on = false;
        }
        // keys: a Juno-style PWM chord: the pad, and stabs of it on the off-steps
        float width = 0.5f + 0.35f * std::sin(TWO_PI * 0.7f * lfo_t);
        float chord = 0;
        for (int v = 0; v < 3; v++) {
            advance_phase(pad_ph_[v], pad_notes_[v] * dt);
            chord += pwm(pad_ph_[v], pad_notes_[v] * dt, width);
        }
        chord /= 3;
        pad_amp_ += ((held_bass_ > 0 ? 1.0f : 0.0f) - pad_amp_) * 0.0005f;
        float pad = hp(pad_hp_, lp(pad_lp_, chord, a_pad), a_pad_hp) * 0.10f * pad_amp_;
        float stab = 0;
        if (stab_.on) {
            float t = stab_.t += dt;
            stab = hp(stab_hp_, lp(stab_lp_, chord * std::min(1.0f, t / 0.004f) * std::exp(-t / 0.11f), a_stab), a_stab_hp) * 0.40f;
            if (t > 0.6f) stab_.on = false;
        }
        float keys = pad + stab;
        keys_dl_[dl_pos_ % keys_n] = keys;
        auto tap = [&](const std::vector<float>& dl, float ms) {
            float d = ms * sr_ / 1000;
            size_t len = dl.size();
            float pos = float(dl_pos_ % len) - d + float(len);
            size_t i0 = size_t(pos) % len, i1 = (i0 + 1) % len;
            float fr = pos - std::floor(pos);
            return dl[i0] * (1 - fr) + dl[i1] * fr;
        };
        float kl = tap(keys_dl_, 7 + 2.5f * std::sin(TWO_PI * 0.6f * lfo_t));
        float kr = tap(keys_dl_, 7 + 2.5f * std::sin(TWO_PI * 0.6f * lfo_t + 3.14159f));
        L += kl * 0.2f + keys * 0.8f, R += kr * 0.2f + keys * 0.8f;
        // DX7-ish electric piano answering the stabs: a falling FM index, a faint bell
        float ep = 0;
        if (ep_.on) {
            float t = ep_.t += dt;
            for (int v = 0; v < 3; v++) {
                float f = ep_.notes[v];
                ep += std::sin(TWO_PI * f * t + 1.6f * std::exp(-t / 0.05f) * std::sin(TWO_PI * f * t)) * std::exp(-t / 0.25f);
                ep += 0.15f * std::sin(TWO_PI * f * 7 * t) * std::exp(-t / 0.02f);
            }
            ep = hp(ep_.hp, ep / 3, a_ep_hp) * 0.22f;
            if (t > 1.2f) ep_.on = false;
        }
        L += ep * 1.1f, R += ep * 0.9f;
        // arpeggio: a soft pulse pluck, wandering across the stereo
        float arp = 0;
        if (arp_.on) {
            float t = arp_.t += dt;
            advance_phase(arp_.phase, arp_.freq * dt);
            float o = pwm(arp_.phase, arp_.freq * dt, 0.3f) * std::min(1.0f, t / 0.004f) * std::exp(-t / 0.06f);
            arp = hp(arp_.hp, lp(arp_.lp, o, a_arp), a_arp_hp) * 0.08f;
            float pan = arp_.kind / 1.5f - 1;
            L += arp * (1 - 0.7f * pan), R += arp * (1 + 0.7f * pan);
            if (t > 0.4f) arp_.on = false;
        }
        // lead: the tune, PWM + saw, a little glide and vibrato, chorus and echo
        lead_freq_ = lead_freq_ <= 0 ? lead_target_ : lp(lead_freq_, lead_target_, a_glide);
        lead_vol_ = lp(lead_vol_, lead_vol_target_, a_lvol);
        float lead = 0;
        if (lead_freq_ > 0) {
            float f = lead_freq_ * (1 + 0.0015f * std::sin(TWO_PI * 5 * lfo_t));
            float lw = 0.35f + 0.08f * std::sin(TWO_PI * 0.4f * lfo_t);
            advance_phase(lead_ph_, f * dt);
            advance_phase(lead_ph2_, lead_freq_ * 1.002f * dt);
            float o = 0.6f * pwm(lead_ph_, f * dt, lw) + 0.4f * saw(lead_ph2_, lead_freq_ * 1.002f * dt);
            o = lp(lead_lp_, o, lp_coef(1400 + 3000 * lead_vol_, sr_));
            lead = hp(lead_hp_, o, a_lead_hp) * std::pow(std::max(lead_vol_, 0.0f), 1.2f) * 0.85f;
        }
        lead_dl_[dl_pos_ % lead_dl_.size()] = lead;
        float ll = tap(lead_dl_, 6 + 1.5f * std::sin(TWO_PI * 0.8f * lfo_t));
        float lr = tap(lead_dl_, 6 + 1.5f * std::sin(TWO_PI * 0.8f * lfo_t + 3.14159f));
        echo_dl_[dl_pos_ % echo_n] = lead;
        float echo = 0.14f * tap(echo_dl_, 420) + 0.0196f * tap(echo_dl_, 840);
        L += ll * 0.24f + lead * 0.96f + echo * 0.8f, R += lr * 0.24f + lead * 0.96f + echo * 1.0f;
        // drums: kick, a soft body snare, a smooth shaker
        float drums = 0, snare = 0;
        if (kick_.on) {
            float t = kick_.t += dt;
            advance_phase(kick_.phase, (48 + 120 * std::exp(-t / 0.025f)) * dt);
            drums += std::sin(TWO_PI * kick_.phase) * std::exp(-t / 0.18f) * 0.9f;
            if (t > 0.5f) kick_.on = false;
        }
        if (snare_.on) {
            float t = snare_.t += dt;
            advance_phase(snare_.phase, (185 + 40 * std::exp(-t / 0.02f)) * dt);
            float body = std::sin(TWO_PI * snare_.phase) * std::exp(-t / 0.06f);
            float nz = hp(snare_.hp, lp(snare_.lp, noise(), a_sn_lp), a_sn_hp) * std::exp(-t / 0.07f);
            snare = (0.7f * body + 0.6f * nz) * std::min(1.0f, t / 0.003f) * 0.42f;
            if (t > 0.4f) snare_.on = false;
        }
        if (shaker_.on) {
            float t = shaker_.t += dt;
            float nz = hp(shaker_.hp, lp(shaker_.lp, noise(), a_sh_lp), a_sh_hp);
            float s = nz * std::min(1.0f, t / 0.004f) * std::exp(-t / 0.025f) * 0.10f;
            L += s * 1.1f, R += s * 0.9f;
            if (t > 0.15f) shaker_.on = false;
        }
        L += drums + snare, R += drums + snare;
        // a small room on the music
        float send = (lead + echo * 0.8f + keys * 0.6f + ep * 0.8f + arp + snare * 0.4f) * 0.015f;
        for (int c = 0; c < 2; c++) {
            float acc = 0;
            for (Comb& cb : combs_[c]) {
                float o = cb.buf[cb.pos];
                cb.store = o * 0.7f + cb.store * 0.3f;
                cb.buf[cb.pos] = send + cb.store * 0.8f;
                cb.pos = (cb.pos + 1) % cb.buf.size();
                acc += o;
            }
            for (Allpass& ap : allpass_[c]) {
                float o = ap.buf[ap.pos];
                ap.buf[ap.pos] = acc + o * 0.5f;
                ap.pos = (ap.pos + 1) % ap.buf.size();
                acc = o - acc;
            }
            float rv = hp(rev_hp_[c], acc, a_rev_hp) * 0.17f * 4;
            (c == 0 ? L : R) += rv;
        }
        dl_pos_++;
        // out: the level, a soft knee on the peaks only
        for (float* v : {&L, &R}) {
            float x = *v * MASTER * 0.25f, a = std::fabs(x);
            if (a > 0.75f) x = std::copysign(0.75f + 0.2f * std::tanh((a - 0.75f) / 0.2f), x);
            *v = x;
        }
        last_output_[0] = L * restart_gain_ + restart_tail_[0] * (1 - restart_gain_);
        last_output_[1] = R * restart_gain_ + restart_tail_[1] * (1 - restart_gain_);
        out[i * 2] = last_output_[0], out[i * 2 + 1] = last_output_[1];
        restart_gain_ = std::min(1.0f, restart_gain_ + 1.0f / (sr_ * 0.02f));
    }
}
