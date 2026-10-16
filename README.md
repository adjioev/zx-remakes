# ZX Remakes

HD remakes of ZX Spectrum games: the original runs unmodified in a Z80 / ZX Spectrum
emulator, and the remake reads its state from memory and draws new art on top.

The original games are not included: `original/` is local only.

## Games

- **Ball Breaker** (CRL Group, 1987, 128K) - playable (beta).

Website and downloads: https://adjioev.github.io/zx-remakes/ (Windows and Linux builds are made by
`.github/workflows/build.yml` for a `v*` tag). Drop your own copy onto the window: `BallBreaker.tap.zip`
(Spectrum Computing / World of Spectrum; the tape is loaded at once through the ROM) or TOSEC's
`Ball Breaker (1987)(CRL Group)[128K].z80`, or the zip either came in.

## Ball Breaker graphics

The HD renderer has two themes: **Neon / Tron** (the default) and **Classic HD**
(brighter solid surfaces, restrained effects, and a cyan status panel).
Press **T** during play, or **Esc**, then **T**, to switch themes. The pause
screen shows the current selection. The theme lasts for the current game session.
**Tab** (or **R** during play) still switches to the original graphics and sound.

Classic HD currently restyles the game scene and instruction thumbnails; title
art and menu typography are shared with Neon.
