"""Ballbreaker II intro: its loading screen repainted as HD key art (the game list's cover, the site).

  python3 -I tools/bb2_intro.py guide <BallbreakerII.tzx>   the loading screen, scaled (raw/intro_orig.png)
  python3 -I tools/bb2_intro.py gen [n]                     n Gemini variants (raw/intro_<k>.png), ~$0.13 each
  python3 -I tools/bb2_intro.py classic [n] <shot.png>      n variants in the Classic HD look (raw/classic_<k>.png),
                                                            a Classic HD screenshot as the style reference
  python3 -I tools/bb2_intro.py pick <k>                    variant k -> assets/ballbreaker2/intro.png (1920 x 1080)

The loading screen is in the tape's second block (loaded to 0x8000): attributes first (0x300 bytes),
then the bitmap packed from 0x8300: 0x0A n = n bytes 0xFF, 0x12 = the end, anything else as it is.
The title is not painted (models mangle lettering): the remake draws it over the picture.
"""
import os, sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'remaster', 'assets', 'ballbreaker2')
RAW = os.path.join(ASSETS, 'raw')
STYLE = os.path.join(ROOT, 'remaster', 'assets', 'ballbreaker', 'intro.png')
PALETTE = [(0, 0, 0), (0, 0, 215), (215, 0, 0), (215, 0, 215), (0, 215, 0), (0, 215, 215), (215, 215, 0), (215, 215, 215),
           (0, 0, 0), (0, 0, 255), (255, 0, 0), (255, 0, 255), (0, 255, 0), (0, 255, 255), (255, 255, 0), (255, 255, 255)]


def tzx_blocks(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'ZXTape!\x1a', 'not a .tzx'
    i, blocks = 10, []
    while i < len(d):
        t = d[i]
        i += 1
        if t == 0x10:
            n = d[i + 2] | d[i + 3] << 8
            blocks.append(d[i + 4:i + 4 + n])
            i += 4 + n
        elif t == 0x30:
            i += 1 + d[i]
        else:
            raise ValueError('block %02x' % t)
    return blocks


def loading_screen(tzx):
    data = tzx_blocks(tzx)[2][1:-1]  # header, BASIC, then this one (flag and checksum off)
    attrs, packed = data[:0x300], data[0x300:]
    bitmap, i = bytearray(), 0
    while packed[i] != 0x12:
        if packed[i] == 0x0A:
            bitmap += b'\xff' * packed[i + 1]
            i += 2
        else:
            bitmap.append(packed[i])
            i += 1
    scr = bytes(bitmap[:6144]).ljust(6144, b'\0') + attrs
    im = Image.new('RGB', (256, 192))
    px = im.load()
    for y in range(192):
        row = (y & 0xC0) << 5 | (y & 7) << 8 | (y & 0x38) << 2
        for x in range(256):
            attr = scr[0x1800 + (y >> 3) * 32 + (x >> 3)]
            ink, paper, bright = attr & 7, attr >> 3 & 7, 8 if attr & 0x40 else 0
            px[x, y] = PALETTE[(ink if scr[row | x >> 3] & (0x80 >> (x & 7)) else paper) + bright]
    return im


def guide(tzx):
    os.makedirs(RAW, exist_ok=True)
    loading_screen(tzx).resize((2048, 1536), Image.NEAREST).save(os.path.join(RAW, 'intro_orig.png'))
    print('raw/intro_orig.png')


PROMPT = """Image 1 is the loading screen of "Ballbreaker II" (ZX Spectrum, 1988) at its original low
resolution. Image 2 is the approved HD title art of the first game's remake: use it as the STYLE
reference only (a TRON-like digital world, deep black glossy void, glowing PURPLE / VIOLET neon light
with a little magenta and white-hot highlights; clean, sharp, premium).

Repaint image 1 as the HD title key art of the sequel's remake, wide 16:9, keeping its composition and
its elements in the same places (stretched to 16:9):
- a white-hot burst of light in the upper middle, its comet trail curving down to the lower left;
- a stack of chunky isometric bricks (the original's green blocks) filling the lower middle, glossy
  glass-and-neon cubes, some cracking apart with shards and sparks;
- on the right: a big glossy ball (glass and chrome, cyan and violet reflections) inside a bright ring.
The bricks and the ball are the remake's: neon-edged dark glass, as in image 2's style.
Leave the top fifth and the bottom quarter calm and dark (titles go there later).
No text, no letters, no logo, no numbers, no UI. Do not copy the low-resolution pixels."""


def gen(n):
    from gemini import generate
    orig = Image.open(os.path.join(RAW, 'intro_orig.png'))
    style = Image.open(STYLE)
    for k in range(n):
        im = generate(PROMPT, [orig, style], aspect='16:9', size='2K', label='bb2 intro %d' % k)
        im.save(os.path.join(RAW, 'intro_%d.png' % k))
        print('raw/intro_%d.png' % k)


CLASSIC = """Image 1 is the loading screen of "Ballbreaker II" (ZX Spectrum, 1988) at its original low
resolution. Image 2 is a screenshot of the sequel's remake in its "Classic HD" look: use it as the STYLE
reference only - bright, solid, saturated colours (cyan, green, yellow, magenta, violet), clean
isometric 3D bricks with soft shading and crisp edges, a glossy silver ball, a calm deep navy
background; friendly and premium, like a polished modern puzzle game. NOT neon, NOT glowing lines,
NOT dark synthwave.

Repaint image 1 as the HD title key art of the remake, wide 16:9, keeping its composition and its
elements in the same places (stretched to 16:9):
- a white-hot burst of light in the upper middle, its comet trail curving down to the lower left;
- a wall of chunky isometric bricks (the original's green blocks) filling the lower middle, solid
  coloured bricks as in image 2, some breaking apart with chunks and sparks;
- on the right: a big glossy silver ball inside a bright ring.
Leave the top fifth and the bottom quarter calm (titles go there later).
No text, no letters, no logo, no numbers, no UI. Do not copy the low-resolution pixels."""


def classic(n, shot):
    from gemini import generate
    orig = Image.open(os.path.join(RAW, 'intro_orig.png'))
    style = Image.open(shot)
    for k in range(n):
        im = generate(CLASSIC, [orig, style], aspect='16:9', size='2K', label='bb2 classic %d' % k)
        im.save(os.path.join(RAW, 'classic_%d.png' % k))
        print('raw/classic_%d.png' % k)


def pick(k, name='intro'):
    im = Image.open(os.path.join(RAW, '%s_%d.png' % (name, k))).convert('RGB').resize((1920, 1080), Image.LANCZOS)
    os.makedirs(ASSETS, exist_ok=True)
    im.save(os.path.join(ASSETS, 'intro.png'))
    print('assets/ballbreaker2/intro.png')


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'guide':
        guide(sys.argv[2])
    elif cmd == 'gen':
        gen(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    elif cmd == 'classic':
        classic(int(sys.argv[2]), sys.argv[3])
    elif cmd == 'pick':
        pick(int(sys.argv[2]), sys.argv[3] if len(sys.argv) > 3 else 'intro')
    else:
        print(__doc__)
