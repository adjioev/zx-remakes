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

# Ball Breaker 1 instructions

Build `tests/instructions.cpp` using the same command and object files as the
paddle-contact test, then pass the user's BB1 `.z80` snapshot. It opens a hidden
raylib window, selects Instructions with normal controls, checks that the HD
screen remains Instructions throughout the original demonstration, and checks
its automatic return to the menu. It also checks Play, Controls, Keys and
Hi-Scores. The old code failed because the selected item changed from 4 to 0
when a page reused the menu's stack memory.

BB1's menu painter receives its selected item in register A at `0x9DA7`.
Capture it there, not from `0xFFF8` after execution has left the menu.
Instructions starts at `0xAE11`; retain that screen until the menu painter runs
again, including the demonstration's calls into the gameplay compositor.
The footer describes the automatic pages/return rather than promising key exit.

Manual browsing: LEFT/RIGHT selects the adjacent page (wrapping at either end)
and stops the six-second page timer for the current visit. A held direction
counts once until released or reversed. The integration test covers both
arrows, wraparound, holding a key, retaining the page across timer boundaries,
and resetting manual selection on return to the menu. BB1's original timed
return still runs; manual browsing does not pause that demonstration.

# Original-speed motion

`original_motion.cpp` checks that the ball continues moving after a known sample,
that prediction is bounded during a long gap, and that disabling prediction
holds the endpoint. A 30-second BB1 emulator run exercises both free-space
prediction and the conservative obstacle guard, sampling presentation between
50 Hz emulator frames. It does not replace a manual smoothness/contact playtest.

From the repository root:

```sh
c++ -std=c++20 -Iremaster/src $(pkg-config --cflags raylib) remaster/tests/original_motion.cpp remaster/build/CMakeFiles/zxremakes.dir/src/{games.cpp,gamefile.cpp,net.cpp,ballbreaker_game.cpp,ballbreaker_hd.cpp,spectrum.cpp,zxcore.c,music_remix.cpp}.o $(pkg-config --libs raylib) -o /tmp/zx-original-motion-test
/tmp/zx-original-motion-test 'original/ballbreaker/Ball Breaker (1987)(CRL Group)[128K].z80'
```

Original mode permits a decaying continuation of at most half a rendered step
for the ball alone. A conservative sphere envelope suppresses prediction near
room boundaries, bricks, and the paddle. It never predicts collision responses
or changes emulator timing. Even mode keeps its existing interpolation.

## RAM motion preview (BB1 only, opt-in)

Launch with `BB_ORIGINAL_PREVIEW=1 ZXREMAKES_GAME=ballbreaker remaster/build/zxremakes`
from the repository root. It starts in Original speed; O still toggles Even.
The preview observes physical RAM coordinates after each Z80 microstep at
`0x886B`. A disposable emulator clone predicts up to 160 ms with the currently
held input. It stops publishing predicted points at a direction change or
paddle contact. Only the live emulator produces sound and contact effects.
Input is not buffered. Presentation distributes straight-run microsteps over
time; it may correct or pause when an input change invalidates the forecast.
BB2 does not enable this experiment: its addresses have not been verified.

Build `original_preview.cpp` with the same object-file command above (replace
`original_motion.cpp` and the executable name). Run it with the BB1 snapshot,
then again with a trailing `juke` argument. Both runs compare baseline/preview
RAM and CPU ticks every emulator frame. They check confirmed contact placement,
one sound per contact, and unexpected outward motion near an approaching paddle.
The follow run also checks straight-line speed variance and rejects a frozen
ball. These numerical checks do not replace playing the preview.

Manual check: compare Original with and without the environment variable,
watch long straight runs and shallow wall bounces, move the paddle away at the
last instant, and check slowdown, lost balls, new serves and switching O/T.

Measured on the 30-second BB1 follow replay: straight-segment screen-X speed
standard deviation 117.118 -> 28.681 units/s; preview range -179.090..-76.616
(no stationary sample in that segment). Follow/juke recorded 4/2 confirmed
contacts with 4/2 sounds and no unexpected outward movement after fixing the
fall transition. New-serve animation is excluded from the approach-direction
check. Preview adapter+emulator cost p95 was about 7.9 ms on the test machine;
this does not include drawing and is not a full-frame FPS measurement.

## Paddle motion in the Original preview

The BB1 opt-in preview smooths only confirmed physical RAM positions. It
interpolates with a 20 ms buffer and averages a 40 ms window. Nominal visual lag
is 40 ms on a steady ramp; stopping settles within 60 ms of the last RAM change.
There is no input buffering or speculative paddle movement. A confirmed ball
contact anchors the paddle to RAM; Even retains its existing interpolation.

The previous implementation could lead RAM by 4 units. On release its explicit
20 ms correction moved backwards. The new release regression failed with 12
backwards display samples before the fix and passes with zero afterwards.

Build `paddle_motion.cpp` with the object-file command above and run with the
BB1 snapshot, once with no additional argument and once with `even`. It compares
a baseline and preview emulator's RAM and CPU ticks throughout 20 seconds of
holds, stops, rails and fast reversals. It checks no backwards movement after
release, rail bounds, stop settling, and identical rendered positions in Even.
`original_preview.cpp` checks paddle RAM alignment on confirmed hits.

Measured original-speed velocity standard deviation: 54.178 -> 27.002 units/s
on shared straight-movement samples. All 500 sample checks passed; maximum
observed spatial lag was 5.5 units. Even produced identical positions. Manual
assessment is needed for the visual lag on release/reversal and the contact
anchor, which can still produce a forward correction.
