# Frame pacing

Run from `remaster/` (no original game file or window required):

```sh
cc -O2 -Isrc tests/frame_pacing.c src/zxcore.c -o /tmp/frame_pacing
/tmp/frame_pacing
```

The synthetic Z80 program varies its work per pass. Over 1,000 frames the test
checks one pass per frame, correct CPU resumption and memory writes, interrupt
delivery, unchanged audio sample count, and release of the frame wait.

Ball Breaker parks at `0x8F57`, the start of a complete game pass. CPU headroom
lets expensive passes finish within 20 ms; parking prevents cheap passes from
running ahead. `0x8BD3` restores normal CPU speed on leaving play. The countdown
at `0x8083 > 1` uses HALT and keeps its original timing. The interrupt handler
at `0x8617` requests ball movement every other interrupt (about 25 Hz). During
paced play, the read at `0x9030` instead gets a flag derived from simulation-frame
parity: exactly 25 Hz. This prevents rare 60 ms ball intervals caused by drift
between the emulator's video clock and our 50 Hz simulation. Collision handling,
speed bonuses, interrupt-driven audio and countdowns are unchanged.
HD positions are interpolated 40 ms behind simulation.

For manual checks, play through brick explosions, losing a life and restarting;
check paddle response and sound as well as ball motion. `BB_NO_TURBO=1` disables
pacing for comparison. `BB_TURBO_LOG=1` logs frame/pass counts and game state.

Validation on 2026-10-16: two-minute emulator stress runs starting at levels
1, 9, 15 and 50, with test-only replenished lives/missiles and a sweeping paddle,
had no missed or extra passes in consecutive paced frames (15,793 transitions).
The runs also reached levels 2, 10, 11 and 16. A separate normal-controls windowed
run exercised brick destruction, life loss and return to the title screen.
These automated checks do not replace a human check of responsiveness and sound.

Ball-clock follow-up: repeating the four two-minute runs with the simulation
clock override produced 5,436 short consecutive ball-update intervals, all two
frames (40 ms), with no one- or three-frame intervals. Longer serve/death pauses
were excluded. The production build also completed the 30-second windowed
scenario with brick destruction and life loss. This measures simulation timing,
not the display's achieved FPS; interactive rendering requests VSync and is
independent of the 50 Hz simulation.
