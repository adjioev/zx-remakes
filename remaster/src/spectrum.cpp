#include "spectrum.h"

#include <cstring>
#include <utility>

Spectrum::Spectrum(zxc_t* zx) : zx_(zx) { zxc_set_exec_fn(zx_, exec_trampoline, this); }

Spectrum::~Spectrum() { zxc_destroy(zx_); }

std::unique_ptr<Spectrum> Spectrum::from_z80(const std::vector<uint8_t>& snapshot) {
    int model = zxc_z80_model(snapshot.data(), snapshot.size());
    if (!model) return nullptr;
    zxc_t* zx = zxc_create(model, SAMPLE_RATE);
    if (!zx) return nullptr;
    std::unique_ptr<Spectrum> s(new Spectrum(zx));
    if (!zxc_load_z80(zx, snapshot.data(), snapshot.size())) return nullptr;
    return s;
}

std::unique_ptr<Spectrum> Spectrum::from_tape(const std::vector<uint8_t>& tape, bool tzx, const TapeProgress& progress) {
    // the 128's menu, Tape Loader (Enter), and the tape: through the ROM at once, a game's own
    // loader at once too (fast) or by the signal (the CPU 16 times as fast meanwhile); then on until
    // the program waits in a PAUSE (the loading screen, where a .z80 of it is taken) or, a game
    // started by its loader, two seconds; at most a minute and a half. A fast load that did not
    // start the program (it never ran from RAM afterwards) is done again by the signal.
    auto attempt = [&](bool fast) -> std::unique_ptr<Spectrum> {
        zxc_t* zx = zxc_create(128, SAMPLE_RATE);
        if (!zx) return nullptr;
        std::unique_ptr<Spectrum> s(new Spectrum(zx));
        if (!(tzx ? zxc_insert_tzx : zxc_insert_tap)(zx, tape.data(), tape.size())) return nullptr;
        zxc_set_fast_load(zx, fast);
        const size_t whole = zxc_tape_left(zx);
        int done = -1;
        bool started = false;  // after the tape: the program running (from RAM, or in a ROM PAUSE)
        for (int f = 0; f < 4500; f++) {
            if (zxc_tape_left(zx) == whole && !zxc_tape_playing(zx) && f % 100 == 60) zxc_key(zx, 0x0D, true);
            if (f % 100 == 64) zxc_key(zx, 0x0D, false);
            zxc_set_turbo(zx, zxc_tape_playing(zx) ? 16 : 1);
            zxc_run_frame(zx);
            if (progress && f % 5 == 0) progress(zxc_screen(zx), 1.0f - float(zxc_tape_left(zx)) / float(whole ? whole : 1));
            if (done < 0 && zxc_tape_left(zx) == 0) done = f;
            uint16_t pc = zxc_regs(zx).pc;
            if (done >= 0 && pc >= 0x4000) started = true;
            if (done >= 0 && f - done >= 10 && pc >= 0x1F3A && pc <= 0x1F54) return s;  // ROM PAUSE (0x1F3A)
            if (done >= 0 && f - done >= 100) break;
        }
        zxc_set_turbo(zx, 1);
        zxc_set_fast_load(zx, false);
        return done >= 0 && started ? std::move(s) : nullptr;
    };
    std::unique_ptr<Spectrum> s = attempt(true);
    if (!s) s = attempt(false);
    if (!s) return nullptr;
    zxc_set_turbo(s->zx_, 1);
    zxc_set_fast_load(s->zx_, false);
    float drop[1024];
    while (zxc_audio(s->zx_, drop, 1024) > 0) {}  // the boot's sound is not the game's
    return s;
}

namespace {
constexpr char STATE_MAGIC[4] = {'Z', 'X', 'R', 'S'};
}

// [ZXRS][version][size][model][the machine]
std::vector<uint8_t> Spectrum::save_state() const {
    size_t n = zxc_state_size();
    std::vector<uint8_t> out(16 + n);
    uint32_t version = zxc_save_state(zx_, out.data() + 16);
    uint32_t head[3] = {version, uint32_t(n), uint32_t(model())};
    std::memcpy(out.data(), STATE_MAGIC, 4);
    std::memcpy(out.data() + 4, head, sizeof head);
    return out;
}

std::unique_ptr<Spectrum> Spectrum::from_state(const std::vector<uint8_t>& state) {
    if (state.size() < 16 || std::memcmp(state.data(), STATE_MAGIC, 4) != 0) return nullptr;
    uint32_t head[3];
    std::memcpy(head, state.data() + 4, sizeof head);
    if (head[1] != zxc_state_size() || state.size() != 16 + size_t(head[1])) return nullptr;
    zxc_t* zx = zxc_create(int(head[2]), SAMPLE_RATE);
    if (!zx) return nullptr;
    std::unique_ptr<Spectrum> s(new Spectrum(zx));
    if (!zxc_load_state(zx, head[0], state.data() + 16)) return nullptr;
    return s;
}

void Spectrum::run_frame() {
    zxc_run_frame(zx_);
    frames_++;
}

std::optional<int> Spectrum::key_code(const std::string& name) {
    if (name == "space") return ' ';
    if (name == "enter") return 0x0D;
    if (name == "caps") return 0x0E;
    if (name == "sym") return 0x0F;
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return c;
    }
    return std::nullopt;
}

std::vector<uint8_t> Spectrum::dump() const {
    std::vector<uint8_t> out(0x10000);
    for (int a = 0; a < 0x10000; a++) out[size_t(a)] = peek(uint16_t(a));
    int banks = model() == 128 ? 8 : 3;
    for (int b = 0; b < banks; b++) out.insert(out.end(), bank(b), bank(b) + 0x4000);
    return out;
}

void Spectrum::picture_rgba(uint32_t* out) const {
    const uint8_t* fb = zxc_framebuffer(zx_);
    const uint32_t* pal = zxc_palette();
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) out[y * FB_W + x] = pal[fb[y * ZXC_FB_STRIDE + x] & 15];
}

void Spectrum::screen_rgba(const uint8_t* scr, uint32_t* out, bool flash_phase) {
    const uint32_t* pal = zxc_palette();
    for (int y = 0; y < SCREEN_H; y++) {
        // bitmap address: 010 y7 y6 y2 y1 y0 | y5 y4 y3 x4..x0
        int row = (y & 0xC0) << 5 | (y & 7) << 8 | (y & 0x38) << 2;
        for (int cx = 0; cx < 32; cx++) {
            uint8_t pix = scr[row | cx], attr = scr[0x1800 + (y >> 3) * 32 + cx];
            int ink = attr & 7, paper = attr >> 3 & 7, bright = attr & 0x40 ? 8 : 0;
            if ((attr & 0x80) && flash_phase) std::swap(ink, paper);
            for (int b = 0; b < 8; b++)
                out[y * SCREEN_W + cx * 8 + b] = pal[(pix & (0x80 >> b) ? ink : paper) + bright];
        }
    }
}

void Spectrum::on_exec(uint16_t addr, ExecFn fn) {
    hooks_[addr].push_back(std::move(fn));
    zxc_on_exec(zx_, addr, true);
}

void Spectrum::exec_trampoline(void* user, uint16_t pc, const zxc_regs_t* regs) {
    auto* self = static_cast<Spectrum*>(user);
    auto it = self->hooks_.find(pc);
    if (it == self->hooks_.end()) return;
    for (auto& fn : it->second) fn(*regs);
}
