# Third-party code

- `chips/` - floooh/chips (https://github.com/floooh/chips), zlib license (`chips/LICENSE`),
  commit 9e88298ce56319953ac7a43213a1120359f7a3a6 (2026-09-26), unmodified (zxcore.c adds turbo T-states with its own copy of the port handling): `chips_common.h`,
  `z80.h`, `ay38910.h`, `beeper.h`, `kbd.h`, `clk.h`, `mem.h` from `chips/`, `zx.h` from `systems/`.
- `zx-roms.h` - ZX Spectrum 48K and 128K ROMs as C arrays, from floooh/chips-test
  (`examples/roms/zx-roms.h`). The ROMs are (c) Amstrad, who allow their distribution with
  emulators.
- `stb_image.h` - nothings/stb (https://github.com/nothings/stb), public domain (or MIT),
  v2.30, commit 013ac3beddff3dbffafd5177e7972067cd2b5083, unmodified. Only its JPEG decoder is
  used (`src/jpeg.c`): raylib's image loader is built without JPEG.
