// cc -O2 -Isrc tests/frame_pacing.c src/zxcore.c -o /tmp/frame_pacing && /tmp/frame_pacing
#include "zxcore.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int boundaries, interrupts, normal_interrupts;
static void normal_irq(void* user, uint16_t pc, const zxc_regs_t* regs) {
    (void)user; (void)pc; (void)regs;
    normal_interrupts++;
}
static void boundary(void* user, uint16_t pc, const zxc_regs_t* regs) {
    if (pc == 0x9393) { interrupts++; return; }
    (void)regs;
    boundaries++;
    zxc_yield_frame(user);
}

static zxc_t* machine(void) {
    // An uncompressed v1 snapshot: increment one byte per loop, with variable work.
    static uint8_t snapshot[30 + 49152];
    memset(snapshot, 0, sizeof snapshot);
    snapshot[7] = 0x80;  // PC = 8000
    snapshot[9] = 0xff;  // SP = ff00
    snapshot[10] = 0x91; // I: interrupt vector table at 9100
    snapshot[27] = snapshot[28] = 1;
    snapshot[29] = 2;    // IM 2
    memset(snapshot + 30 + 0x5100, 0x93, 257);
    snapshot[30 + 0x5393] = 0xfb; // EI
    snapshot[30 + 0x5394] = 0xed; // RETI
    snapshot[30 + 0x5395] = 0x4d;
    const uint8_t program[] = {
        0x21, 0x00, 0x90,  // LD HL,9000
        0x34,              // INC (HL) -- frame boundary
        0x46,              // LD B,(HL)
        0x10, 0xfe,        // DJNZ self (1..256 iterations)
        0xc3, 0x03, 0x80   // JP 8003
    };
    memcpy(snapshot + 30 + 0x4000, program, sizeof program);
    zxc_t* zx = zxc_create(48, 44100);
    assert(zxc_load_z80(zx, snapshot, sizeof snapshot));
    return zx;
}

int main(void) {
    zxc_t* paced = machine();
    zxc_t* normal = machine();
    zxc_on_exec(paced, 0x8003, true);
    zxc_on_exec(paced, 0x9393, true);
    zxc_set_exec_fn(paced, boundary, paced);
    zxc_set_turbo(paced, 16);
    zxc_on_exec(normal, 0x9393, true);
    zxc_set_exec_fn(normal, normal_irq, NULL);
    unsigned paced_audio = 0, normal_audio = 0;
    float samples[4096];
    for (int frame = 0; frame < 1000; frame++) {
        zxc_run_frame(paced);
        zxc_run_frame(normal);
        assert(boundaries == frame + 1);
        assert(zxc_peek(paced, 0x9000) == (uint8_t)frame);
        paced_audio += zxc_audio(paced, samples, 4096);
        normal_audio += zxc_audio(normal, samples, 4096);
    }
    // The final video interrupt may still be pending at the parked CPU.
    assert(normal_interrupts > 990);
    assert(interrupts >= normal_interrupts - 1 && interrupts <= normal_interrupts);
    assert(paced_audio == normal_audio);
    assert(paced_audio > 880000 && paced_audio < 883000);
    assert(zxc_cpu_ticks(paced) < zxc_cpu_ticks(normal));
    // Removing the hook restores unrestricted execution on the very next frame.
    zxc_on_exec(paced, 0x8003, false);
    uint64_t before = zxc_cpu_ticks(paced);
    zxc_run_frame(paced);
    assert(zxc_cpu_ticks(paced) - before == 16 * 70000);
    zxc_destroy(paced);
    zxc_destroy(normal);
    puts("PASS: one pass/frame, CPU resumes correctly, peripheral/audio clocks continue, pacing releases");
}
