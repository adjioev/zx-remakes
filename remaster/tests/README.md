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
  remaster/build/CMakeFiles/zxremakes.dir/src/{games.cpp,gamefile.cpp,net.cpp,ballbreaker_game.cpp,ballbreaker_hd.cpp,spectrum.cpp,zxcore.c,music_remix.cpp}.o \
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
or changes emulator timing. Even keeps its existing interpolation away from the shared contact region below.

## Original-speed smoothing (BB1 only)

BB1 smooths Original speed by default: press O from the normal Even-speed start.
`BB_ORIGINAL_PREVIEW=0` disables the Original smoother and shared contact presentation in both speeds for comparison. Explicit
`BB_ORIGINAL_PREVIEW=1` retains the shortcut that starts in Original speed.
The preview observes physical RAM coordinates after each Z80 microstep at
`0x886B`. A disposable emulator clone predicts up to 160 ms with the currently
held input. It stops publishing predicted points at a direction change or
paddle contact. Only the live emulator produces sound and contact effects.
Input is not buffered. Presentation distributes straight-run microsteps over
time; it may correct or pause when an input change invalidates the forecast.
BB2 keeps its existing presentation: the extra microstep and lifecycle addresses
used by this smoother have not been independently verified for the sequel.

Build `original_preview.cpp` with the same object-file command above (replace
`original_motion.cpp` and the executable name). Run it with the BB1 snapshot,
then again with a trailing `juke` argument. Both runs compare baseline/preview
RAM and CPU ticks every emulator frame. They check confirmed contact placement,
one sound per contact, and unexpected outward motion near an approaching paddle.
The follow run also checks straight-line speed variance and rejects a frozen
ball. These numerical checks do not replace playing the preview.

Manual check: compare Original with and without `BB_ORIGINAL_PREVIEW=0`,
watch long straight runs and shallow wall bounces, move the paddle away at the
last instant, and check slowdown, lost balls, new serves and switching O/T.

Measured before the shared contact timeline, on the 30-second BB1 follow replay: straight-segment screen-X speed
standard deviation 117.118 -> 28.681 units/s; preview range -179.090..-76.616
(no stationary sample in that segment). Follow/juke recorded 4/2 confirmed
contacts with 4/2 sounds and no unexpected outward movement after fixing the
fall transition. New-serve animation is excluded from the approach-direction
check. Preview adapter+emulator cost p95 was about 7.9 ms on the test machine;
this does not include drawing and is not a full-frame FPS measurement.

## Paddle motion in the Original preview

BB1 Original speed smooths the paddle using only confirmed physical RAM positions. It
interpolates with a 20 ms buffer and averages a 40 ms window. Nominal visual lag
is 40 ms on a steady ramp; stopping settles within 60 ms of the last RAM change.
There is no input buffering or speculative paddle movement. Near the ball, the
shared contact timeline below replaces independent averaging in both speeds; Even retains its
existing interpolation.

The previous implementation could lead RAM by 4 units. On release its explicit
20 ms correction moved backwards. The new release regression failed with 12
backwards display samples before the fix and passes with zero afterwards.

Build `paddle_motion.cpp` with the object-file command above and run with the
BB1 snapshot, once with no additional argument and once with `even`. It compares
a baseline and preview emulator's RAM and CPU ticks throughout 20 seconds of
holds, stops, rails and fast reversals. It checks no backwards movement after
release, rail bounds, stop settling, and identical rendered positions in Even away from contact.
`original_preview.cpp` checks both objects against the collision-hook coordinates
at the delayed contact, together with audio and flash timing.

Before the shared contact timeline, measured original-speed velocity standard deviation: 54.178 -> 27.002 units/s
on shared straight-movement samples. All 500 sample checks passed; maximum
observed spatial lag was 5.5 units. Even produced identical positions away from contact. Manual
assessment is needed for the visual lag on release/reversal and the contact
anchor, which can still produce a forward correction.

## Incoming ball / paddle face (Original preview)

When the forecast emulator accepts a paddle collision, the incoming visual path
now ends on BAT_Z. Native microsteps already beyond that plane are removed from
that forecast tail. No outgoing movement or hit event is published early.
A miss is not clamped. If a changed input turns a predicted miss into a catch
after the picture has crossed the plane, only the confirmed live contact may
correct it; speculation must not pull the ball back.

`original_preview.cpp` now checks the previous incoming prediction at the real
contact frame: the follow replay failed with 4 penetrations before this change.
The juke replay requires actual misses, discards the forecast on falling, and
checks no false outward turns or speculative hit sounds. Both still compare
RAM and CPU ticks with a control emulator. The shared contact timeline below also removes the paddle's independent reset.

The three 60-second centre/edge replays retained the same 41 original contacts
(times and physical contact coordinates). Their ball correction at contact
changed from 1–3 units to zero. Final juke replay: 5 misses, 2 real contacts,
2 hit sounds, zero false outward turns. Late changes from a displayed miss to a
real catch can still require a correction at the confirmed contact.


## Shared contact presentation and asymmetric edges (BB1)

In both BB1 speeds, ball and bat use paired confirmed RAM samples near the bat,
with a fixed 40 ms presentation buffer. Samples are captured after movement and
at the native collision test. Actual hits anchor both objects together; hit sound
and flash use the same delayed timestamp. Between Z=40 and Z=24 the renderer blends
from the existing motion filters to this paired history. Input and original
collision decisions are untouched. Near contact there is less smoothing, in
exchange for preserving the relationship between the two objects. The old hard
paddle reset on a live hit is removed.

BB1's visible left paddle edge is inset by 3 units, with its right edge unchanged.
A radius-6 ball then matches the original asymmetric lateral test (+3 / -6) for
normal paddle widths. This applies to both themes and speeds. Very narrow eaten
paddles retain at least a one-unit visible body; exact edge equivalence cannot be
represented with that sphere when native width is below 4. BB2 is unchanged.
The original still checks the ball's centre against BAT_Z and has no separate
side-face collision; this change does not add modern sphere/box physics.

Build `miss_presentation.cpp` with the object-file command above. Run with the
snapshot and a trailing `8`, `100`, or `0`: stationary edge misses, moving away
right at the last instant, and rapid reversals. For final misses it samples the
actual renderer at each rejected collision's delayed time, checks that ball and
bat match the observed pair, and rejects visual lateral overlap. All three runs
compare RAM and CPU ticks against a control emulator every frame. In 60 seconds
per scenario: 16/10/15 final misses, 48/30/45 rejected-contact samples, zero
presentation mismatches. These are sampled collision moments, not a proof for
every intervening display frame or every possible level.

After pairing, the follow replay's straight ball speed deviation is 34.242
versus 117.118 without preview. The paddle replay's overall speed deviation is
36.116 versus 54.178 without preview (previous independent averaging: 27.002).
The regression now checks exact preservation of the old paddle filter away from
contact, overall improvement, release monotonicity, and unchanged Even positions away from contact.
Manually check catches near both ends, late movement away, stopping, and new
serves in both themes. The 40 ms buffer is visual; controls still reach the
original immediately, but the ball's picture is now delayed as well.

### Default scope and extended validation (2026-10-17)

Build `smoothing_scope.cpp` with the same object-file command. Run with the BB1
snapshot followed by `bb1`, or an extracted original BB2 `.tzx` followed by `bb2`.
It tests unset/0/1 `BB_ORIGINAL_PREVIEW`, verifies the initial speed, toggles O
both ways, and compares RAM and CPU ticks against a control every frame.
BB1 enables smoothing by default while still starting in Even; explicit `1`
starts Original, and `0` disables smoothing. BB2 remains disabled in all three
cases. Each 30-second BB2 replay produced 8 confirmed hits and 8 sounds; the BB1
unset/0/1 replays produced 9/9, 9/9 and 7/7. Gameplay state and CPU ticks matched.
The sequel's existing behavior was tested separately; its forecast hook addresses
were not mapped, and the BB1 smoother was not enabled there.

`original_preview <snapshot> follow <level> <width>` adds synthetic fixtures:
a jump via the game's level-start routine, forced RAM paddle width after frame
550, native slowdown word set to 180 at frame 700, and an O round trip at frames
950/1050. These validate handling of the real game's states; they do not claim
that those levels or power-ups were reached naturally. Widths 4/6/8/16 were
replayed on levels 9/50/15/1 respectively. All four preserved authoritative RAM
and CPU ticks, matching hit/sound counts (6/6, 6/6, 4/4, 5/5), and exact delayed
contact anchors in Original mode. Geometry tests also cover widths 4/6/8/12/16.

The extended incoming-motion diagnostic now covers Z below 61, including the
whole 24..40 blend region and 40..56, excluding spawn/death/countdown and 200 ms
after an authoritative Z direction change. It exposes a remaining limitation:
the level-15/width-8 fixture has four outward correction samples around Z=54..56,
maximum 0.605 native units per 60 Hz display sample; level-1/width-16 has three
around Z=31..34, maximum 1.134 units. These two extended commands deliberately
return failure for that diagnostic. Levels 9/50 had no such samples. The existing
blend boundary was retained: moving it only relocates corrections. Conditional
forecasts and confirmed samples can still differ in phase as input/workload
changes; this is not a claim of perfect monotone motion in every configuration.

Normal follow/juke replays pass with 4/4 and 2/2 hits/sounds, no detected outward
corrections, no incoming penetrations, and unchanged RAM/ticks. The follow
straight-run speed deviation remains 117.118 -> 34.242 units/s. The original
miss regressions still pass: 48/30/45 rejected samples from 16/10/15 final misses,
zero presentation mismatches. No human end-to-end verification is implied by
these automated results.

### Even contact presentation (2026-10-17)

The paired confirmed contact timeline now also applies in BB1 Even. It uses the
same 40 ms buffer and 24..40 blend region. Even uses uniform frame-boundary
physical samples, preserving fine microsteps only within four native Z units
of BAT_Z or at a confirmed hit; replaying all turbo bursts farther away raised
paddle speed deviation to 31.335, versus 26.498 with this bounded sampling.
Even never runs a forecast clone;
its CPU headroom, pass parking, 25 Hz ball clock and native collision decisions
are unchanged. BB2 remains excluded. Outside this region both ball and paddle
retain the previous Even interpolation exactly (774 checked display samples).

CPU T-states are not a presentation clock in Even: turbo adds extra CPU-only
steps, then parked time advances peripherals without advancing the CPU counter.
`Spectrum::video_ticks()` observes outer machine ticks, including parked time
and excluding turbo-only ticks. Contact samples use that clock in Even; Original
retains its existing CPU timestamp mapping. Samples within the same machine tick
share its approximately 0.3 microsecond resolution. O clears the history before
switching clock domains. The frame-pacing test checks equal machine time for
normal/turbo/parked CPUs through 1,000 frames and after releasing the parked CPU.

Build `miss_presentation.cpp` as above, then append `even` after each mode:
`miss_presentation <snapshot> 8 even` (also 100 and 0). Before the renderer fix,
mode 8 produced 3 paired-position mismatches among 57 rejected contact samples.
Afterwards all three pass: 57/60/57 rejected samples from 19/20/19 final misses,
zero paired-position or lateral-overlap mismatches, identical RAM and CPU ticks.

`original_preview <snapshot> even` checks delayed confirmed contact placement,
flash and sound, one sound per hit, incoming-direction diagnostics, RAM/tick
identity, and that no clone path is built. The 30-second replay passes with
7 hits/7 sounds and no unexpected incoming reversal. Existing contact geometry
and flash deduplication checks also pass with Even as the default speed.

This is a contact-consistency fix, not a claim of improved smoothness everywhere:
in the paddle replay, Even's overall speed deviation increases from 14.093 to
26.498 units/s because the paired history replaces the independent filter near
the ball. Maximum observed paddle lag is 4 native units; no backwards release samples
were detected. In the Z=8..16 contact-window diagnostic, maximum paddle step
is 1.667 -> 3.333 units per 60 Hz display sample, and maximum change of step
is 2.000 -> 3.333 units. Exact collision-time pair anchors constrain how much
those very-near-contact bursts can be spread. Original's separately
documented forecast-correction limitations remain. These checks sample collision
moments and scripted replays; a human should still check both speeds in play.


## Predictable new-ball direction in Even (BB1)

Even now sets the new ball's X increment to -1 at `0x9CBB`, after the original
initialises its spawn coordinates (42,20) and Z increment (-1), before the drop
animation starts. Thus every serve uses the same diagonal, regardless of paddle
position at creation. This is an intentional gameplay adjustment, not rendering
interpolation. It does not steer balls in flight or change subsequent wall,
brick, or paddle responses. Original retains its native centre-dependent rule;
BB2 is unchanged because this entry point has only been verified for BB1.
Switching speed after creation does not redirect the current serve; the next
new ball uses the newly selected mode.

Build `even_serve.cpp` with the object-file command above and run with the BB1
snapshot. It uses test-only paddle positions 30/42/60 before native serve setup,
alternates Even/Original over six real spawn/loss cycles, checks selected direction
and the first physical step after the drop, and verifies O itself does not mutate
RAM. Before the fix the two off-centre Even cases failed both direction and first
step assertions. Original and centre cases already passed.

Previous Even replay counts elsewhere in this file predate this intentional
serve rule change; identical user inputs can now produce different later paths.
Baseline/renderer comparisons still use the same serve policy on both machines.


## Geometric paddle collision in Even (BB1)

Even now replaces the native centre-plane test at `0x8A0A` with a swept
radius-6 sphere against the same decoded paddle body used by the renderer.
Front, side and rounded-corner contacts are tested along every native ball
microstep. A separate sweep after native paddle movement (`0x88AD`) catches a
paddle moving into the ball, without applying its whole movement repeatedly to
all ball microsteps. This is the native engine's ordered, split-step simulation.

The front face retains position-controlled aiming: -2, -1, 0, +1 or +2 X
increments, independent of incoming X. Side/corner response reflects against
the contact normal and selects a separating legal native direction. Integer RAM
positions round out of the body. A sphere trapped between paddle and side wall
is moved towards the playfield rather than allowed to wrap an unsigned coordinate.
Only a drain flag set during the current step can be recovered; an already
falling ball is never resurrected. The original drain boundary remains in use.

The exact impact centre and paddle snapshot feed the paired presentation buffer;
geometric contacts retain their actual video-clock timestamp within the frame,
and later microsteps in that frame remain in the history. Flash and sound follow
the same 40 ms delay. Sustained side pushes count as one
contact episode until separation. The flash appears on the contacted surface.
Physics also runs when `BB_ORIGINAL_PREVIEW=0` disables smooth presentation.
Original and BB2 keep their native collision/response routines.

`paddle_sweep.cpp` is a standalone geometry regression:

```sh
c++ -std=c++20 -O2 -Iremaster/src remaster/tests/paddle_sweep.cpp -o /tmp/paddle-sweep
/tmp/paddle-sweep
```

It compares 10,252 initially separated paths against independent dense distance
sampling, including moving paddles, fast crossings and rounded corners: zero
failures. `even_paddle_aim.cpp` uses real Z80 hooks for geometric contacts and
native Original acceptance/response fixtures, including widths 1/2/3: 684 cases
pass, plus 14 moving-paddle contact episodes.
Build snapshot tests with the object-file command above; set `ZXREMAKES_TEST=1`
to avoid writing persistent user high scores.

`original_preview.cpp` checks exact geometric anchors in Even and native anchors
in Original, plus delayed effects/sound and identical authoritative RAM with
presentation enabled/disabled. The normal replay passed with 9 Even hits / 9
sounds and 4 Original hits / 4 sounds, with no false incoming turns.
`paddle_contact.cpp` checks both themes and effect timing at 50/60/120/144 Hz;
`even_serve.cpp` still passes all six mode/paddle-position serve cases.

Limitations: the engine still uses five discrete X increments and angle-dependent
movement batches. This is not a continuous-angle, constant-speed physics rewrite.
`miss_presentation.cpp` checks final misses and samples visible sphere/paddle
clearance at 400 Hz. Even may retime intermediate samples, so it checks geometry
rather than requiring the old raw microstep position. Original retains exact raw
pair checks. The recorder runs before adapter hooks to avoid observing temporary
compensated positions. These checks do not replace the side/corner acceptance tests.
Manual verification should focus on front edges, moving side catches, narrow
paddles, side walls and ordinary misses, in both themes.

## Even contact smoothing and paddle rail

Even's paired trajectory now simplifies confirmed motion within the existing
40 ms buffer, retaining exact impacts, reversals and the paddle track's velocity
change times. Paddle samples use the actual native movement clock. Moving-paddle
impact times use the sweep fraction, bounded by the preceding ball step and
contact. Previously displayed history is frozen. A continuous sphere/box distance
check rejects shortcuts that would cross the paddle, including the actual height
of a falling ball. This avoids the false floor-level overlap that held the paddle
back during a loss, followed by a catch-up jump.

`contact_smoothing.cpp` tests uniform movement at 60/120/144 Hz, stops, reversals,
contact anchors, bootstrap continuity, native velocity knots and spatial clearance.
`paddle_contact_clock.cpp` exercises a moving side hit through the real 0x88AD
flow: exact impact time/position, delayed sound/flash and sound deduplication.
Build both with the object-file command above (the pure test needs no snapshot).

The final `paddle_motion.cpp ... even` replay reports speed SD 2.640 for both
normal and paired paths, maximum contact step 1.669 and maximum step across play
1.673 at 60 Hz. It checks both maxima against 2 units, release without backsteps,
unchanged authoritative RAM and 758 exact comparisons outside the contact zone.
The previous contact path measured a 3-unit maximum and SD 27.7. There is no
additional presentation delay; the calibrated buffer remains 40 ms.

Even limits the physical paddle body to X=0..92, shared with the HD room width.
Its normal left inset remains 3; for a nearly eaten paddle it is capped at half
the native width so reaching X=0 never requires a negative unsigned RAM centre.
Both rendering and collision geometry use that inset. Original movement and
geometry remain native. `paddle_bounds.cpp` runs 140 real movement cases covering
both modes, seven widths, both rail endpoints and accelerated mouse movement.

### Native movement and music preservation

Even keeps the native centre at least half the RAM width. The previous minimum
was half-width minus the HD inset: width 12 could reach centre 3. At 0x88BA the
native projector subtracts half-width in an unsigned byte, producing 253 instead
of -3. Its dirty-rectangle blitter then writes outside screen memory. Tracing
writes at 0x8207/09/0B/0D demonstrated graphics bytes overwriting the tune at
0x5EB2, 0x5F12 and neighbouring rows. This reproduced channel B period 0x0A6B
instead of 0x046B. Removing the movement jump alone did not fix that underflow.

The fixed native range is half-width .. 92-width+half-width+inset (6..89 for
width 12). HD and collision anchors both subtract the inset from native left,
keeping the visible body exactly at X=0..92 without unsafe native coordinates.
The native movement routine executes fully; its desired centre is applied at
0x88AD before projection and contact observation. Original is unchanged.

`paddle_music.cpp` uses the previously failing timing: Enter at 1 second, right
at 8..9 seconds, left at 12..13, right at 18..19. It checks 1,500 frames, 150
native movement/address-load pairs, increment/decrement destinations, immutable
tune bytes after loading and AY registers against native movement. Restoring
only the unsafe rail mapping in a temporary build reproduces corruption and
fails the test. The fixed build changes no tune bytes, has zero pitch-register
differences and one amplitude-only frame difference.

The full runner was also compared with the committed build using the same input:
all 147 consecutive tone changes match; the incorrect 0x0A6B is gone. There are
two one-frame pitch timing differences and other amplitude timing differences,
so this is not a claim of byte-identical complete audio. Ordinary and accelerated
mouse replays reach native centres 89 and 6 repeatedly with zero changed tune
bytes. Both-theme contacts, 140 rail cases, 684 collision cases, 14 moving-paddle
episodes and the quantitative smoothness replay pass. Human listening remains
the final check.

## Full paddle travel (2026-10-17)

`paddle_travel.cpp` holds left/right through the real keyboard and mouse paths
in both speed modes, then checks the settled presentation at each corner. It
also checks that the music bytes remain unchanged after travelling both rails.
The previous commit fails four of the eight endpoint checks: Even stops at X=92
although the brick-aligned HD wall is at X=96, and Original leaves X=0..3 empty.
The fixed version reaches 0 and 96 in both modes, including switching to
Original from an odd-coordinate Even endpoint. Original maps its shorter
painted paddle across the native travel range without writing game RAM;
Even uses the same room width for its physical collision body and rail clamp.

Compile like `paddle_music.cpp`, linking the game/view/renderer/emulator objects,
and run with `ZXREMAKES_TEST=1` and the BB1 128K snapshot path. The sustained
travel test supplements `paddle_bounds.cpp`, whose injected positions alone
cannot prove that normal input can reach a corner.
