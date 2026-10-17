#pragma once

// A ZX Spectrum (48K or 128K) behind a small C interface, so the chips headers (C, with
// anonymous unions C++ does not allow) stay inside zxcore.c.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zxc zxc_t;

typedef struct {
    uint16_t af, bc, de, hl, ix, iy, sp, pc;
} zxc_regs_t;

// Called when the CPU fetches the opcode at a hooked address (before it runs).
typedef void (*zxc_exec_fn)(void* user, uint16_t pc, const zxc_regs_t* regs);

#define ZXC_JOY_RIGHT (1 << 0)
#define ZXC_JOY_LEFT (1 << 1)
#define ZXC_JOY_DOWN (1 << 2)
#define ZXC_JOY_UP (1 << 3)
#define ZXC_JOY_FIRE (1 << 4)

// Model a .z80 snapshot needs: 48 or 128, 0 if it is not one we can load.
int zxc_z80_model(const uint8_t* data, size_t size);

zxc_t* zxc_create(int model, int sample_rate);
void zxc_destroy(zxc_t* zx);
int zxc_model(const zxc_t* zx);
bool zxc_load_z80(zxc_t* zx, const uint8_t* data, size_t size);

// Put a .tap image in the tape deck: what the program loads through the ROM (LD-BYTES, 0x0556
// of the 48K BASIC ROM) comes from it at once, block by block; other loaders get the tape played
// on EAR in real time (it starts the first time LD-BYTES is reached for anything but a load).
// false: not a .tap.
bool zxc_insert_tap(zxc_t* zx, const uint8_t* data, size_t size);
// The same for a .tzx made of standard-speed blocks (and text / info / group / pause blocks).
// false: not a .tzx, or one with blocks not supported yet (turbo, pure tones...).
bool zxc_insert_tzx(zxc_t* zx, const uint8_t* data, size_t size);
// Bytes of the tape not read (or played) yet (0: none inserted, or all of it).
size_t zxc_tape_left(const zxc_t* zx);
// The tape is playing on EAR now.
bool zxc_tape_playing(const zxc_t* zx);
// A game's own copy of LD-BYTES done at once too (A = flag, IX = where, DE = how many; always a
// load), instead of playing the tape to it: much faster, but only right for a loader that works
// as the ROM's does (Spectrum::from_tape checks the program started, else plays the tape).
void zxc_set_fast_load(zxc_t* zx, bool on);

// The whole machine's state (CPU, memory, ULA, AY...), to keep and restore later: a buffer of
// zxc_state_size() bytes; zxc_save_state returns its version, zxc_load_state wants it back (false: a
// state from another version). Hooks, turbo and the tape are not part of it.
size_t zxc_state_size(void);
uint32_t zxc_save_state(zxc_t* zx, void* out);
bool zxc_load_state(zxc_t* zx, uint32_t version, const void* state);

// Run one 50 Hz frame (20 ms of emulated time).
void zxc_run_frame(zxc_t* zx);
// Turbo: the CPU runs this many times as fast as the rest of the machine (1 = normal, up to 16);
// screen, interrupts and sound keep their speed, the program gets more done per frame.
void zxc_set_turbo(zxc_t* zx, float factor);
// From an execution hook: hold the CPU until the next frame. Video and audio keep
// running; a video interrupt is retained for the CPU to service when it resumes.
void zxc_yield_frame(zxc_t* zx);
// The next time the CPU fetches the opcode at at_pc, carry on at to_pc with SP = sp instead (a
// jump the program did not make: e.g. into the game's own level start).
void zxc_jump_at(zxc_t* zx, uint16_t at_pc, uint16_t to_pc, uint16_t sp);
// The AY's 16 registers as they are now (128K; zeros on a 48K).
void zxc_ay_regs(const zxc_t* zx, uint8_t out[16]);
// CPU T-states run so far (turbo included).
uint64_t zxc_cpu_ticks(const zxc_t* zx);

// Keyboard matrix key, by the chips code: ' ' space, 0x0D enter, 'a'-'z', '0'-'9',
// 0x0E caps shift, 0x0F symbol shift.
void zxc_key(zxc_t* zx, int code, bool down);
// Kempston joystick (port 0x1F), ZXC_JOY_* bits.
void zxc_joystick(zxc_t* zx, uint8_t mask);

// Memory as the CPU sees it now (current 128K paging).
uint8_t zxc_peek(const zxc_t* zx, uint16_t addr);
// Write as the CPU would (ROM stays as it is).
void zxc_poke(zxc_t* zx, uint16_t addr, uint8_t value);
// RAM bank 0-7 (48K: banks 0-2 = 0x4000, 0x8000, 0xC000), 16 KB each.
const uint8_t* zxc_bank(const zxc_t* zx, int n);
// The bank the ULA shows (5, or 7 when the 128K shadow screen is on; 48K: 0).
const uint8_t* zxc_screen(const zxc_t* zx);
uint8_t zxc_border(const zxc_t* zx);
uint8_t zxc_last_7ffd(const zxc_t* zx);
zxc_regs_t zxc_regs(const zxc_t* zx);

// The emulator's picture: 320 x 256 (32-pixel border around 256 x 192), one palette index
// (0-15) per byte, row stride ZXC_FB_STRIDE.
#define ZXC_FB_W 320
#define ZXC_FB_H 256
#define ZXC_FB_STRIDE 512
const uint8_t* zxc_framebuffer(const zxc_t* zx);
// 16 colours, 0xAABBGGRR.
const uint32_t* zxc_palette(void);

// Move up to max produced audio samples (mono, -1..1) into out; returns how many.
int zxc_audio(zxc_t* zx, float* out, int max);

// Hook: fn runs whenever the CPU fetches an opcode at addr (any bank paged there).
void zxc_on_exec(zxc_t* zx, uint16_t addr, bool on);
void zxc_set_exec_fn(zxc_t* zx, zxc_exec_fn fn, void* user);

#ifdef __cplusplus
}
#endif
