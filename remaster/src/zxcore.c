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
    uint64_t cpu_ticks, video_ticks;
    uint8_t* tape;  // the blocks, as in a .tap: [length word][flag, data..., checksum]
    uint16_t* pauses;  // ms of silence after each block
    size_t tape_size, tape_pos;  // tape_pos: the block being played / read next
    int block;  // its number
    // played as the signal on EAR, for loaders the ROM trap cannot take (see tape_tick)
    bool playing, ear, fast;  // fast: a copy of LD-BYTES is done at once too (see tape_tick)
    int phase, pulses, bit, half;
    uint32_t unread;  // T-states since port 0xFE was last read
    uint32_t t_left;
    size_t byte;
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
    if (zx) zx_discard(&zx->sys), free(zx->tape), free(zx->pauses);
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

// The ROM's LD-BYTES (0x0556 in the 48K BASIC ROM), done at once from the tape image: A = the
// flag byte wanted, IX = where, DE = how many, carry = load. Blocks with another flag are passed
// over, as the ROM does. Returns as LD-BYTES does (carry = success, interrupts on again) to the
// caller's return address.
static uint64_t rom_load(zxc_t* zx, bool ei) {
    zx_t* sys = &zx->sys;
    z80_t* c = &sys->cpu;
    bool ok = false;
    while (zx->tape_pos + 2 <= zx->tape_size) {
        size_t len = zx->tape[zx->tape_pos] | zx->tape[zx->tape_pos + 1] << 8;
        const uint8_t* b = zx->tape + zx->tape_pos + 2;
        zx->tape_pos += 2 + len, zx->block++;
        if (len < 2 || b[0] != c->a) continue;
        size_t n = len - 2 < c->de ? len - 2 : c->de;
        for (size_t i = 0; i < n; i++) mem_wr(&sys->mem, (uint16_t)(c->ix + i), b[1 + i]);
        ok = n == c->de;
        c->ix = (uint16_t)(c->ix + n), c->de = (uint16_t)(c->de - n);
        break;
    }
    c->f = ok ? (uint8_t)(c->f | Z80_CF) : (uint8_t)(c->f & ~Z80_CF);
    if (ei) c->iff1 = c->iff2 = true;  // as the ROM's own way out does (a copy's may not)
    uint16_t ret = (uint16_t)(mem_rd(&sys->mem, c->sp) | mem_rd(&sys->mem, (uint16_t)(c->sp + 1)) << 8);
    c->sp = (uint16_t)(c->sp + 2);
    return z80_prefetch(c, ret);  // drops this fetch, carries on at the caller
}

// The tape as a signal (standard ROM timing, in CPU T-states): pilot, two sync pulses, each bit
// as two pulses, a second of silence after each block.
enum { TAPE_PILOT, TAPE_SYNC1, TAPE_SYNC2, TAPE_DATA, TAPE_PAUSE };

static void tape_block(zxc_t* zx) {
    if (zx->tape_pos + 2 > zx->tape_size) {
        zx->playing = false;
        return;
    }
    const uint8_t* b = zx->tape + zx->tape_pos + 2;
    zx->phase = TAPE_PILOT;
    zx->pulses = b[0] < 0x80 ? 8063 : 3223;  // header / data
    zx->t_left = 2168;
}

static uint32_t tape_bit(zxc_t* zx) {
    const uint8_t* b = zx->tape + zx->tape_pos + 2;
    return (b[zx->byte] >> zx->bit) & 1 ? 1710 : 855;
}

static void tape_pulse(zxc_t* zx) {  // the current pulse is over
    size_t len = zx->phase == TAPE_PAUSE ? 0 : (size_t)(zx->tape[zx->tape_pos] | zx->tape[zx->tape_pos + 1] << 8);
    if (zx->phase != TAPE_PAUSE) zx->ear = !zx->ear;
    switch (zx->phase) {
    case TAPE_PILOT:
        if (--zx->pulses > 0) zx->t_left = 2168;
        else zx->phase = TAPE_SYNC1, zx->t_left = 667;
        break;
    case TAPE_SYNC1: zx->phase = TAPE_SYNC2, zx->t_left = 735; break;
    case TAPE_SYNC2:
        zx->phase = TAPE_DATA, zx->byte = 0, zx->bit = 7, zx->half = 0;
        zx->t_left = len ? tape_bit(zx) : 1;
        break;
    case TAPE_DATA:
        if (!zx->half) {
            zx->half = 1, zx->t_left = tape_bit(zx);
            break;
        }
        zx->half = 0;
        if (--zx->bit < 0) zx->bit = 7, zx->byte++;
        if (zx->byte < len) zx->t_left = tape_bit(zx);
        else {  // the block is read; the last edge stays (the loader needs it to end the last bit), then silence
            zx->phase = TAPE_PAUSE;
            zx->t_left = 3500u * (zx->pauses[zx->block] ? zx->pauses[zx->block] : 1);
            zx->tape_pos += 2 + len, zx->block++;  // (after the last block: plays on to its pause's end)
        }
        break;
    case TAPE_PAUSE:
        zx->ear = false;
        tape_block(zx);
        break;
    }
}

// Every CPU T-state with a tape in: a load through the ROM (LD-BYTES called with carry set, the
// 48K BASIC ROM paged in) is done at once. Anything else that reaches LD-BYTES (a verify), or a
// copy of it elsewhere (a game's own loader: it starts as the ROM's does, INC D / EX AF,AF' /
// DEC D / DI), starts the tape playing from the next block, and from then on port 0xFE reads
// see it on EAR (bit 6) until it ends.
static uint64_t tape_tick(zxc_t* zx, uint64_t pins) {
    zx_t* sys = &zx->sys;
    if (!zx->playing && (pins & (Z80_M1 | Z80_MREQ | Z80_RD)) == (Z80_M1 | Z80_MREQ | Z80_RD)) {
        uint16_t pc = Z80_GET_ADDR(pins);
        bool rom = !(sys->type == ZX_TYPE_128 && !(sys->last_mem_config & 0x10));
        if (pc == 0x0556 && rom && (sys->cpu.f & Z80_CF)) return rom_load(zx, true);
        bool copy = pc >= 0x4000 && mem_rd(&sys->mem, pc) == 0x14 && mem_rd(&sys->mem, (uint16_t)(pc + 1)) == 0x08 &&
                    mem_rd(&sys->mem, (uint16_t)(pc + 2)) == 0x15 && mem_rd(&sys->mem, (uint16_t)(pc + 3)) == 0xF3;
        if (copy && zx->fast) return rom_load(zx, false);  // a guess that it loads as the ROM does
        if ((pc == 0x0556 && rom) || copy) {
            zx->playing = true;
            tape_block(zx);
        }
    }
    if (!zx->playing) return pins;
    // the tape moves while a loader polls the port (every few dozen T-states), and stops while
    // the program does something else (the keyboard is read only once a frame): no block runs
    // past a program that is busy between two loads
    bool read = (pins & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD) && !(pins & Z80_A0);
    if (read) zx->unread = 0;
    else if (zx->unread < 100000) zx->unread++;
    if (zx->unread < 4096 && --zx->t_left == 0) tape_pulse(zx);
    if (read) {
        uint8_t data = Z80_GET_DATA(pins);
        data = zx->ear ? (uint8_t)(data | 0x40) : (uint8_t)(data & ~0x40);
        Z80_SET_DATA(pins, data);
    }
    return pins;
}

static bool insert(zxc_t* zx, uint8_t* tape, size_t size, uint16_t* pauses) {
    free(zx->tape), free(zx->pauses);
    zx->tape = tape, zx->pauses = pauses;
    zx->tape_size = size, zx->tape_pos = 0, zx->block = 0, zx->playing = false, zx->unread = 0;
    return true;
}

bool zxc_insert_tap(zxc_t* zx, const uint8_t* data, size_t size) {
    int blocks = 0;
    for (size_t pos = 0; pos < size; blocks++) {  // whole blocks only
        if (pos + 2 > size) return false;
        pos += 2 + (data[pos] | data[pos + 1] << 8);
        if (pos > size) return false;
    }
    uint8_t* tape = (uint8_t*)malloc(size ? size : 1);
    uint16_t* pauses = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)(blocks + 1));
    if (!tape || !pauses) return free(tape), free(pauses), false;
    memcpy(tape, data, size);
    for (int i = 0; i <= blocks; i++) pauses[i] = 1000;
    return insert(zx, tape, size, pauses);
}

bool zxc_insert_tzx(zxc_t* zx, const uint8_t* d, size_t size) {
    if (size < 10 || memcmp(d, "ZXTape!\x1a", 8) != 0) return false;
    uint8_t* tape = (uint8_t*)malloc(size);
    uint16_t* pauses = (uint16_t*)malloc(sizeof(uint16_t) * (size / 2 + 1));
    if (!tape || !pauses) return free(tape), free(pauses), false;
    size_t out = 0, i = 10;
    int blocks = 0;
    bool ok = true;
    while (ok && i < size) {
        uint8_t id = d[i++];
        size_t left = size - i;
        switch (id) {
        case 0x10: {  // standard speed data: [pause ms][length][data]
            if (left < 4) { ok = false; break; }
            size_t len = d[i + 2] | d[i + 3] << 8;
            if (left < 4 + len) { ok = false; break; }
            tape[out] = (uint8_t)len, tape[out + 1] = (uint8_t)(len >> 8);
            memcpy(tape + out + 2, d + i + 4, len);
            out += 2 + len;
            pauses[blocks++] = (uint16_t)(d[i] | d[i + 1] << 8);
            i += 4 + len;
            break;
        }
        case 0x20:  // a pause after the last block
            if (left < 2) { ok = false; break; }
            if (blocks) pauses[blocks - 1] = (uint16_t)(pauses[blocks - 1] + (d[i] | d[i + 1] << 8));
            i += 2;
            break;
        case 0x21: if (left < 1) ok = false; else i += 1 + d[i]; break;  // group start
        case 0x22: break;                                                 // group end
        case 0x30: if (left < 1) ok = false; else i += 1 + d[i]; break;  // text
        case 0x32: if (left < 2) ok = false; else i += 2 + (d[i] | d[i + 1] << 8); break;  // archive info
        default: ok = false;  // turbo and other signal blocks: not yet
        }
    }
    if (!ok || !blocks) return free(tape), free(pauses), false;
    pauses[blocks] = 1000;
    return insert(zx, tape, out, pauses);
}

void zxc_set_fast_load(zxc_t* zx, bool on) { zx->fast = on; }

size_t zxc_tape_left(const zxc_t* zx) { return zx->tape ? zx->tape_size - zx->tape_pos : 0; }

bool zxc_tape_playing(const zxc_t* zx) { return zx->playing; }

size_t zxc_state_size(void) { return sizeof(zx_t); }

uint32_t zxc_save_state(zxc_t* zx, void* out) { return zx_save_snapshot(&zx->sys, (zx_t*)out); }

bool zxc_load_state(zxc_t* zx, uint32_t version, const void* state) {
    zx_t* copy = (zx_t*)malloc(sizeof(zx_t));  // zx_load_snapshot takes it non-const
    if (!copy) return false;
    memcpy(copy, state, sizeof(zx_t));
    bool ok = zx_load_snapshot(&zx->sys, version, copy);
    free(copy);
    if (ok) install_debug(zx);
    return ok;
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
        zx->video_ticks++;
        if (zx->yielded) {
            pins = idle_tick(sys, pins);
            continue;
        }
        pins = _zx_tick(sys, pins);
        zx->cpu_ticks++;
        if (hooked) on_tick(zx, pins);
        if (zx->jump) pins = maybe_jump(zx, pins);
        if (zx->tape) pins = tape_tick(zx, pins);
        if (zx->turbo > 1 && !zx->yielded) {
            zx->turbo_acc += zx->turbo - 1;
            while (zx->turbo_acc >= 1 && !zx->yielded) {
                zx->turbo_acc -= 1;
                pins = cpu_only_tick(sys, pins);
                zx->cpu_ticks++;
                if (hooked) on_tick(zx, pins);
                if (zx->jump) pins = maybe_jump(zx, pins);
                if (zx->tape) pins = tape_tick(zx, pins);
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
uint64_t zxc_video_ticks(const zxc_t* zx) { return zx->video_ticks; }

void zxc_ay_regs(const zxc_t* zx, uint8_t out[16]) {
    for (int i = 0; i < 16; i++) out[i] = zx->sys.type == ZX_TYPE_128 ? zx->sys.ay.reg[i] : 0;
}

void zxc_key(zxc_t* zx, int code, bool down) {
    // straight to the matrix: zx_key_down would turn space and the cursor keys into the
    // Kempston joystick. chips has CAPS SHIFT only as a modifier: 0x0E is it as a key of its own
    // (column 0, line 0), registered here as a restored machine state brings its own key table
    kbd_register_key(&zx->sys.kbd, 0x0E, 0, 0, 0);
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
