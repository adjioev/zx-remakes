#include "spectrum.h"

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
