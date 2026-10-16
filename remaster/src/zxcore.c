// The chips ZX Spectrum emulator (third_party/chips, zx.h), wrapped for zxcore.h.

#define CHIPS_IMPL
#include <stdlib.h>
#include <string.h>

#include "third_party/chips/chips_common.h"
#include "third_party/chips/z80.h"
#include "third_party/chips/beeper.h"
#include "third_party/chips/ay38910.h"
#include "third_party/chips/kbd.h"
#include "third_party/chips/clk.h"
#include "third_party/chips/mem.h"
#include "third_party/chips/zx.h"
#include "third_party/zx-roms.h"

#include "zxcore.h"

#define AUDIO_RING 16384  // samples; a power of two

struct zxc {
    zx_t sys;
    bool stopped, yielded;
    uint8_t hooked[65536];
    int hooks;
    zxc_exec_fn exec_fn;
    void* exec_user;
    float turbo, turbo_acc;
    bool jump;
    uint16_t jump_at, jump_to, jump_sp;
    uint64_t cpu_ticks;
    float ring[AUDIO_RING];
    unsigned ring_w, ring_r;  // only touched by the thread that runs the emulator
};

static void on_audio(const float* samples, int n, void* user) {
    zxc_t* zx = (zxc_t*)user;
    for (int i = 0; i < n; i++) {
        if (zx->ring_w - zx->ring_r >= AUDIO_RING) zx->ring_r++;  // full: drop the oldest
        zx->ring[zx->ring_w++ & (AUDIO_RING - 1)] = samples[i];
    }
}

static zxc_regs_t regs_of(const z80_t* c) {
    zxc_regs_t r = {c->af, c->bc, c->de, c->hl, c->ix, c->iy, c->sp, c->pc};
    return r;
}

static void on_tick(void* user, uint64_t pins) {
    zxc_t* zx = (zxc_t*)user;
    if ((pins & (Z80_M1 | Z80_MREQ | Z80_RD)) != (Z80_M1 | Z80_MREQ | Z80_RD)) return;
    uint16_t addr = Z80_GET_ADDR(pins);
    if (!zx->hooked[addr] || !zx->exec_fn) return;
    zxc_regs_t r = regs_of(&zx->sys.cpu);
    r.pc = addr;
    zx->exec_fn(zx->exec_user, addr, &r);
}

static void install_debug(zxc_t* zx) {
    // chips only checks for a debug callback per tick when one is set: keep it off
    // while nothing is hooked (full speed)
    zx->sys.debug.callback.func = zx->hooks ? on_tick : 0;
    zx->sys.debug.callback.user_data = zx;
    zx->sys.debug.stopped = &zx->stopped;
}

int zxc_z80_model(const uint8_t* d, size_t size) {
    if (size < 30) return 0;
    if (d[6] | d[7]) return 48;  // version 1: PC in the main header, always 48K
    if (size < 35) return 0;
    int ext = d[30] | d[31] << 8, hw = d[34];
    if (ext == 23) {  // version 2
        if (hw <= 1) return 48;
        if (hw == 3 || hw == 4) return 128;
    } else if (ext == 54 || ext == 55) {  // version 3
        if (hw <= 1) return 48;
        if (hw >= 4 && hw <= 6) return 128;
        if (hw == 12) return 128;  // +2: same memory as the 128
    }
    return 0;
}

zxc_t* zxc_create(int model, int sample_rate) {
    zxc_t* zx = (zxc_t*)calloc(1, sizeof(zxc_t));
    if (!zx) return 0;
    zx_desc_t d;
    memset(&d, 0, sizeof d);
    d.type = model == 128 ? ZX_TYPE_128 : ZX_TYPE_48K;
    d.joystick_type = ZX_JOYSTICKTYPE_KEMPSTON;
    d.audio.callback.func = on_audio;
    d.audio.callback.user_data = zx;
    d.audio.sample_rate = sample_rate;
    d.roms.zx48k = (chips_range_t){dump_amstrad_zx48k_bin, sizeof dump_amstrad_zx48k_bin};
    d.roms.zx128_0 = (chips_range_t){dump_amstrad_zx128k_0_bin, sizeof dump_amstrad_zx128k_0_bin};
    d.roms.zx128_1 = (chips_range_t){dump_amstrad_zx128k_1_bin, sizeof dump_amstrad_zx128k_1_bin};
    zx_init(&zx->sys, &d);
    zx->turbo = 1;
    install_debug(zx);
    return zx;
}

void zxc_destroy(zxc_t* zx) {
    if (zx) zx_discard(&zx->sys);
    free(zx);
}

int zxc_model(const zxc_t* zx) { return zx->sys.type == ZX_TYPE_128 ? 128 : 48; }

bool zxc_load_z80(zxc_t* zx, const uint8_t* data, size_t size) {
    if (zxc_z80_model(data, size) != zxc_model(zx)) return false;
    // zx_quickload writes through a non-const pointer but does not change the data
    return zx_quickload(&zx->sys, (chips_range_t){(void*)data, size});
}

// A CPU T-state without the rest of the machine (turbo): memory and I/O as in _zx_tick, the INT
// pin held for its 32 CPU T-states; no video, AY or beeper time passes.
static uint64_t cpu_only_tick(zx_t* sys, uint64_t pins) {
    pins = z80_tick(&sys->cpu, pins);
    if (pins & Z80_INT) {
        if (--sys->int_counter < 0) pins &= ~Z80_INT;
    }
    if (pins & Z80_MREQ) {
        const uint16_t addr = Z80_GET_ADDR(pins);
        if (pins & Z80_RD) {
            Z80_SET_DATA(pins, mem_rd(&sys->mem, addr));
        } else if (pins & Z80_WR) {
            mem_wr(&sys->mem, addr, Z80_GET_DATA(pins));
        }
    } else if (pins & Z80_IORQ) {
        // the ports, as in _zx_tick
        if ((pins & Z80_A0) == 0) {
            if (pins & Z80_RD) {
                uint8_t data = (1 << 7) | (1 << 5);
                if (sys->last_fe_out & (1 << 3 | 1 << 4)) data |= (1 << 6);
                uint16_t column_mask = (~(Z80_GET_ADDR(pins) >> 8)) & 0x00FF;
                const uint16_t kbd_lines = kbd_test_lines(&sys->kbd, column_mask);
                data |= (~kbd_lines) & 0x1F;
                Z80_SET_DATA(pins, data);
            } else if (pins & Z80_WR) {
                const uint8_t data = Z80_GET_DATA(pins);
                sys->border_color = data & 7;
                sys->last_fe_out = data;
                beeper_set(&sys->beeper, 0 != (data & (1 << 4)));
            }
        } else if (((pins & (Z80_WR | Z80_A15 | Z80_A1)) == Z80_WR) && (sys->type == ZX_TYPE_128)) {
            _zx_update_memory_map_zx128(sys, Z80_GET_DATA(pins));
        } else if (((pins & (Z80_A15 | Z80_A1)) == Z80_A15) && (sys->type == ZX_TYPE_128)) {
            if (pins & Z80_A14) pins |= AY38910_BC1;
            if (pins & Z80_WR) pins |= AY38910_BDIR;
            pins = ay38910_iorq(&sys->ay, pins) & Z80_PIN_MASK;
        } else if ((pins & (Z80_RD | Z80_A7 | Z80_A6 | Z80_A5)) == Z80_RD) {
            Z80_SET_DATA(pins, sys->kbd_joymask | sys->joy_joymask);
        }
    }
    return pins;
}

// Clock peripherals while parked at a game-loop boundary. Do not repeat the CPU's
// last bus transaction. Retain INT until the CPU resumes, including its pulse width.
static uint64_t idle_tick(zx_t* sys, uint64_t pins) {
    if (--sys->scanline_counter <= 0) {
        sys->scanline_counter += sys->scanline_period;
        if (_zx_decode_scanline(sys)) {
            pins |= Z80_INT;
            sys->int_counter = 32;
        }
    }
    if (++sys->tick_count & 1) ay38910_tick(&sys->ay);
    if (beeper_tick(&sys->beeper)) {
        sys->audio.sample_buffer[sys->audio.sample_pos++] = sys->beeper.sample + sys->ay.sample;
        if (sys->audio.sample_pos == sys->audio.num_samples) {
            if (sys->audio.callback.func)
                sys->audio.callback.func(sys->audio.sample_buffer, sys->audio.num_samples,
                                         sys->audio.callback.user_data);
            sys->audio.sample_pos = 0;
        }
    }
    return pins;
}

void zxc_yield_frame(zxc_t* zx) { zx->yielded = true; }

static uint64_t maybe_jump(zxc_t* zx, uint64_t pins) {
    if (zx->jump && (pins & (Z80_M1 | Z80_MREQ | Z80_RD)) == (Z80_M1 | Z80_MREQ | Z80_RD) &&
        Z80_GET_ADDR(pins) == zx->jump_at) {
        zx->jump = false;
        pins = z80_prefetch(&zx->sys.cpu, zx->jump_to);  // drops this fetch, starts one at jump_to
        zx->sys.cpu.sp = zx->jump_sp;
    }
    return pins;
}

void zxc_jump_at(zxc_t* zx, uint16_t at_pc, uint16_t to_pc, uint16_t sp) {
    zx->jump = true;
    zx->jump_at = at_pc, zx->jump_to = to_pc, zx->jump_sp = sp;
}

void zxc_run_frame(zxc_t* zx) {
    // zx_exec, with the turbo T-states in between
    zx_t* sys = &zx->sys;
    const uint32_t num_ticks = clk_us_to_ticks(sys->freq_hz, 20000);
    uint64_t pins = sys->pins;
    const bool hooked = sys->debug.callback.func != 0;
    zx->yielded = false;
    for (uint32_t tick = 0; tick < num_ticks; tick++) {
        if (zx->yielded) {
            pins = idle_tick(sys, pins);
            continue;
        }
        pins = _zx_tick(sys, pins);
        zx->cpu_ticks++;
        if (hooked) on_tick(zx, pins);
        if (zx->jump) pins = maybe_jump(zx, pins);
        if (zx->turbo > 1 && !zx->yielded) {
            zx->turbo_acc += zx->turbo - 1;
            while (zx->turbo_acc >= 1 && !zx->yielded) {
                zx->turbo_acc -= 1;
                pins = cpu_only_tick(sys, pins);
                zx->cpu_ticks++;
                if (hooked) on_tick(zx, pins);
                if (zx->jump) pins = maybe_jump(zx, pins);
            }
        }
    }
    if (zx->yielded) zx->turbo_acc = 0;
    sys->pins = pins;
    kbd_update(&sys->kbd, 20000);
}

void zxc_set_turbo(zxc_t* zx, float factor) {
    zx->turbo = factor < 1 ? 1 : factor > 16 ? 16 : factor;
}

uint64_t zxc_cpu_ticks(const zxc_t* zx) { return zx->cpu_ticks; }

void zxc_ay_regs(const zxc_t* zx, uint8_t out[16]) {
    for (int i = 0; i < 16; i++) out[i] = zx->sys.type == ZX_TYPE_128 ? zx->sys.ay.reg[i] : 0;
}

void zxc_key(zxc_t* zx, int code, bool down) {
    // straight to the matrix: zx_key_down would turn space and the cursor keys into the
    // Kempston joystick
    if (down) kbd_key_down(&zx->sys.kbd, code);
    else kbd_key_up(&zx->sys.kbd, code);
}

void zxc_joystick(zxc_t* zx, uint8_t mask) { zx_joystick(&zx->sys, mask); }

uint8_t zxc_peek(const zxc_t* zx, uint16_t addr) { return mem_rd((mem_t*)&zx->sys.mem, addr); }

void zxc_poke(zxc_t* zx, uint16_t addr, uint8_t value) { mem_wr(&zx->sys.mem, addr, value); }

const uint8_t* zxc_bank(const zxc_t* zx, int n) { return zx->sys.ram[n & 7]; }

const uint8_t* zxc_screen(const zxc_t* zx) { return zx->sys.ram[zx->sys.display_ram_bank]; }

uint8_t zxc_border(const zxc_t* zx) { return zx->sys.border_color; }

uint8_t zxc_last_7ffd(const zxc_t* zx) { return zx->sys.last_mem_config; }

zxc_regs_t zxc_regs(const zxc_t* zx) { return regs_of(&zx->sys.cpu); }

const uint8_t* zxc_framebuffer(const zxc_t* zx) { return zx->sys.fb; }

const uint32_t* zxc_palette(void) {
    static uint32_t pal[16];
    static bool done = false;
    if (!done) {  // the same colours chips uses (zx_display_info)
        static const uint32_t p[16] = {
            0xFF000000, 0xFFD70000, 0xFF0000D7, 0xFFD700D7, 0xFF00D700, 0xFFD7D700, 0xFF00D7D7, 0xFFD7D7D7,
            0xFF000000, 0xFFFF0000, 0xFF0000FF, 0xFFFF00FF, 0xFF00FF00, 0xFFFFFF00, 0xFF00FFFF, 0xFFFFFFFF,
        };
        memcpy(pal, p, sizeof p);
        done = true;
    }
    return pal;
}

int zxc_audio(zxc_t* zx, float* out, int max) {
    int n = 0;
    while (n < max && zx->ring_r != zx->ring_w) out[n++] = zx->ring[zx->ring_r++ & (AUDIO_RING - 1)];
    return n;
}

void zxc_on_exec(zxc_t* zx, uint16_t addr, bool on) {
    if (zx->hooked[addr] == on) return;
    zx->hooked[addr] = on;
    zx->hooks += on ? 1 : -1;
    install_debug(zx);
}

void zxc_set_exec_fn(zxc_t* zx, zxc_exec_fn fn, void* user) {
    zx->exec_fn = fn;
    zx->exec_user = user;
}
