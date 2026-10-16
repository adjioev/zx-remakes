#pragma once

// The Spectrum the original game runs on (zxcore over floooh/chips), for the C++ side:
// keys by name, RAM reads, opcode hooks, the picture as RGBA.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "zxcore.h"

class Spectrum {
public:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr double FRAME_HZ = 50.0;
    static constexpr int SCREEN_W = 256, SCREEN_H = 192;  // without the border
    static constexpr int FB_W = ZXC_FB_W, FB_H = ZXC_FB_H, BORDER = 32;

    // A machine of the model the snapshot needs, with it loaded; nothing if it can't be.
    static std::unique_ptr<Spectrum> from_z80(const std::vector<uint8_t>& snapshot);
    // A 128K loading a .tap through its Tape Loader, at once; returned waiting on the program's
    // first PAUSE after the tape (the loading screen). nullptr if nothing was loaded.
    static std::unique_ptr<Spectrum> from_tap(const std::vector<uint8_t>& tape);
    ~Spectrum();
    Spectrum(const Spectrum&) = delete;
    Spectrum& operator=(const Spectrum&) = delete;

    int model() const { return zxc_model(zx_); }
    void run_frame();
    uint64_t frames() const { return frames_; }
    // CPU speed relative to the machine (1 normal; see zxc_set_turbo) and T-states run so far.
    void set_turbo(float factor) { zxc_set_turbo(zx_, factor); }
    void yield_frame() { zxc_yield_frame(zx_); }
    uint64_t cpu_ticks() const { return zxc_cpu_ticks(zx_); }
    void ay_regs(uint8_t out[16]) const { zxc_ay_regs(zx_, out); }
    // When the CPU next gets to at_pc, continue at to_pc with SP = sp (see zxc_jump_at).
    void jump_at(uint16_t at_pc, uint16_t to_pc, uint16_t sp) { zxc_jump_at(zx_, at_pc, to_pc, sp); }

    // Script / PC key names: "space", "enter", "caps", "sym", "a".."z", "0".."9".
    static std::optional<int> key_code(const std::string& name);
    void key(int code, bool down) { zxc_key(zx_, code, down); }
    void joystick(uint8_t mask) { zxc_joystick(zx_, mask); }

    uint8_t peek(uint16_t addr) const { return zxc_peek(zx_, addr); }
    void poke(uint16_t addr, uint8_t value) { zxc_poke(zx_, addr, value); }
    void poke16(uint16_t addr, uint16_t value) { poke(addr, uint8_t(value)), poke(uint16_t(addr + 1), uint8_t(value >> 8)); }
    uint16_t peek16(uint16_t addr) const { return uint16_t(peek(addr) | peek(uint16_t(addr + 1)) << 8); }
    const uint8_t* bank(int n) const { return zxc_bank(zx_, n); }
    // 6912 bytes: bitmap (interleaved rows) then attributes, of the bank on display.
    const uint8_t* screen() const { return zxc_screen(zx_); }
    uint8_t border() const { return zxc_border(zx_); }
    zxc_regs_t regs() const { return zxc_regs(zx_); }
    // The 64 KB address space as paged now, then all 8 banks (128K) or the 3 of a 48K.
    std::vector<uint8_t> dump() const;

    // The emulator's picture with border, FB_W x FB_H RGBA.
    void picture_rgba(uint32_t* out) const;
    // A 256 x 192 screen image (6912 bytes) as RGBA, for covers and the like.
    static void screen_rgba(const uint8_t* scr, uint32_t* out, bool flash_phase = false);

    // Run fn whenever the CPU is about to execute the instruction at addr (e.g. a sprite
    // routine: what it draws and where). Several per address are fine.
    using ExecFn = std::function<void(const zxc_regs_t&)>;
    void on_exec(uint16_t addr, ExecFn fn);

    // Produced sound, mono -1..1; moves up to max samples into out.
    int audio(float* out, int max) { return zxc_audio(zx_, out, max); }

private:
    explicit Spectrum(zxc_t* zx);
    static void exec_trampoline(void* user, uint16_t pc, const zxc_regs_t* regs);

    zxc_t* zx_;
    uint64_t frames_ = 0;
    std::map<uint16_t, std::vector<ExecFn>> hooks_;
};
