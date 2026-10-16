"""Ball Breaker creatures: a painted sheet of the 5 alien frames, registered onto the game's sprites.

  python3 tools/bb_creatures.py <sheet.png> <ram.bin> [out.png]

sheet.png: the frames 0x11-0x15 in a row, on transparency, their glow painted in (e.g.
assets/ballbreaker/creatures/frames-neon-v1.png). ram.bin: a RAM dump from the remaster's test mode
(`@ram` in play), for the sprites' masks. Each painted frame is matched to its sprite by shape (the
grid of a generated sheet is not to be trusted): the scale and offset that best lay its body over
the sprite's solid pixels. Out (default assets/ballbreaker/creatures/frames.png): one cell per frame,
each the sprite's 24 x 20 box plus PAD sprite pixels all round, CELL pixels per sprite pixel, so the
game draws a cell over the sprite's box grown by PAD.
"""
import os, sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'remaster', 'assets', 'ballbreaker', 'creatures', 'frames.png')
SPRITES, FIRST, COUNT = 0xB9D5, 0x11, 5
W, H, PAD, CELL = 24, 20, 3, 16


def sprite_mask(ram, n):
    m = np.zeros((H, W), bool)
    for y in range(H):
        for x in range(W):
            m[y, x] = not ram[SPRITES + n * 120 + y * 6 + 3 + x // 8] & (0x80 >> (x % 8))
    return m


def frames(alpha):
    """The painted frames: runs of columns with something in them."""
    col = (alpha > 20).any(0)
    runs, s = [], None
    for x, v in enumerate(col):
        if v and s is None: s = x
        if not v and s is not None: runs.append((s, x)); s = None
    if s is not None: runs.append((s, len(col)))
    return [r for r in runs if r[1] - r[0] > 40]


def fit(body, mask, scale=None):
    """Scale and offset (sheet pixels per sprite pixel, top left of the sprite box) laying the
    sprite's mask best over the painted body (intersection over union, coarse to fine)."""
    ys, xs = np.where(body)
    my, mx = np.where(mask)
    s0 = ((xs.max() - xs.min() + 1) / (mx.max() - mx.min() + 1) + (ys.max() - ys.min() + 1) / (my.max() - my.min() + 1)) / 2
    if scale: s0 = scale  # given: only the offset is fitted
    best = (-1, s0, xs.min() - mx.min() * s0, ys.min() - my.min() * s0)
    hh, ww = body.shape

    def iou(s, ox, oy):
        gy = ((np.arange(hh) - oy) / s).astype(int)
        gx = ((np.arange(ww) - ox) / s).astype(int)
        inside = (gy[:, None] >= 0) & (gy[:, None] < H) & (gx[None, :] >= 0) & (gx[None, :] < W)
        m = np.zeros_like(body)
        m[inside] = mask[np.clip(gy, 0, H - 1)[:, None].repeat(ww, 1)[inside], np.clip(gx, 0, W - 1)[None, :].repeat(hh, 0)[inside]]
        return (m & body).sum() / max(1, (m | body).sum())

    for step_s, step_o, span in ((0.04, 4.0, 3), (0.015, 1.5, 3), (0.005, 0.5, 3)):
        _, s1, ox1, oy1 = best
        for ds in (np.arange(-span, span + 1) * step_s * s0 if not scale else [0.0]):
            for dx in np.arange(-span, span + 1) * step_o:
                for dy in np.arange(-span, span + 1) * step_o:
                    v = iou(s1 + ds, ox1 + dx, oy1 + dy)
                    if v > best[0]: best = (v, s1 + ds, ox1 + dx, oy1 + dy)
    return best


def main():
    sheet = Image.open(sys.argv[1]).convert('RGBA')
    ram = open(sys.argv[2], 'rb').read()
    out = sys.argv[3] if len(sys.argv) > 3 else OUT
    rgba = np.array(sheet)
    alpha = rgba[..., 3]
    runs = frames(alpha)
    if len(runs) != COUNT: sys.exit(f'{len(runs)} frames in the sheet, want {COUNT}')
    cw, ch = (W + 2 * PAD) * CELL, (H + 2 * PAD) * CELL
    atlas = Image.new('RGBA', (cw * COUNT, ch), (0, 0, 0, 0))
    # the animation's frames (the head, 0x11-0x14) share one scale: their median, so it does not
    # grow and shrink as it moves; the last (0x15) is fitted on its own
    free = [fit(alpha[:, x0:x1] >= 235, sprite_mask(ram, FIRST + k)) for k, (x0, x1) in enumerate(runs[:4])]
    shared = float(np.median([f[1] for f in free]))
    for k, (x0, x1) in enumerate(runs):
        mask = sprite_mask(ram, FIRST + k)
        crop = alpha[:, x0:x1]
        best = None
        for thr in (235, 245, 252):  # the body: its glow is painted into the alpha too, fainter
            f = fit(crop >= thr, mask, shared if k < 4 else None)
            if best is None or f[0] > best[0]: best, used = f, thr
        iou, s, ox, oy = best
        print(f'frame {FIRST + k:02x}: iou {iou:.3f} (alpha >= {used}), {s:.2f} px / sprite px, box at {x0 + ox:.1f}, {oy:.1f}')
        # the sprite's box grown by PAD, from the sheet, to CELL px per sprite px
        bx, by = x0 + ox - PAD * s, oy - PAD * s
        own = np.zeros_like(rgba)  # this frame alone: its neighbours' glow stays out of its cell
        own[:, x0:x1] = rgba[:, x0:x1]
        cell = Image.fromarray(own).transform((cw, ch), Image.AFFINE, (s / CELL, 0, bx, 0, s / CELL, by), resample=Image.BICUBIC)
        atlas.paste(cell, (k * cw, 0))
    atlas.save(out)
    print('->', out)


if __name__ == '__main__':
    main()
