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

# Long-running remix pitch

Restart isolation check (menu music must not leak into the new game):

```sh
c++ -std=c++20 -O2 -Isrc tests/music_restart.cpp src/music_remix.cpp -o /tmp/music_restart
/tmp/music_restart
```

Compares a previously playing, restarted renderer with a fresh restarted renderer,
both into silence and into new notes. Allows the 20 ms output transition, then
requires matching audio for three seconds. The incomplete reset failed both cases.

```sh
c++ -std=c++20 -O2 -Isrc tests/music_pitch.cpp src/music_remix.cpp -o /tmp/music_pitch
/tmp/music_pitch
```

This exercises `MusicRemix::frame` with synthetic AY registers and analyzes the
actual output waveform, without a window or sound device. It renders three
minutes each of a sustained lead and an A-minor accompaniment. Spectral checks
at 2, 20, 80 and 180 seconds allow the intended chorus/vibrato/detuning, but
reject the large pitch changes caused by unbounded float phase accumulation.
Every output sample must also be finite.

The old renderer failed these checks after 20 seconds. Oscillator phases now
wrap each cycle, and modulation time comes from an integer sample counter with
double-precision time calculations. Note envelopes still use short local times;
there is no change to the arrangement or gameplay clock.

# Paddle contact

From the repository root, after building `remaster/build` (macOS/raylib):

```sh
c++ -std=c++20 -Iremaster/src $(pkg-config --cflags raylib) \
  remaster/tests/paddle_contact.cpp \
  remaster/build/CMakeFiles/zxremakes.dir/src/{games.cpp,ballbreaker_game.cpp,ballbreaker_hd.cpp,spectrum.cpp,zxcore.c,music_remix.cpp}.o \
  $(pkg-config --libs raylib) -o /tmp/zx-paddle-contact
/tmp/zx-paddle-contact 'original/ballbreaker/Ball Breaker (1987)(CRL Group)[128K].z80'
```

Requires the user's original snapshot and a graphical session (opens a hidden
raylib window). No game data is bundled. Replays the first serve with a short
left movement, observes the original collision routine, and checks physical
paddle coordinates and placement inside the room, exactly one delayed flash/sound, and no
retrigger on rerenders or theme changes. Also checks three paddle widths at
rail endpoints, event delivery at 50/60/120/144 Hz, and expiry while HD is hidden.
The pre-fix renderer failed the geometry and shallow-angle flash checks.

Manual check: play in both themes, including hits near either paddle end and
while moving the paddle. Confirm the visual contact and sound feel aligned.

Paddle occlusion regression: the same replay checks draw order at 8.66–8.72 s
in both themes. The old sprite-anchor sort flipped the sphere over the paddle
end at 8.68 s. Ordering now uses the rendered sphere and paddle bounds, including
bounce height. Separate cases cover a sphere in front of either visible side,
a sphere above the paddle, and an absent paddle. Screenshot replay also confirms
that the end face continues to mask the ball smoothly after contact.

Room-placement regression: on the real contact frame the paddle's play-side
corner must project to the original sprite X, its face must stay at BAT_Z, and
its whole base must remain at Z >= 0. Subtracting the ball radius failed all
three checks. This corrects presentation only; the original collision rule is
unchanged. Side-collision work is deferred pending the placement playtest.
