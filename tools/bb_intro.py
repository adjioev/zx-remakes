"""Ball Breaker intro: the loading screen (from the user's snapshot) repainted as HD key art.

  python3 tools/bb_intro.py guide          the loading screen, scaled up (raw/intro_orig.png)
  python3 tools/bb_intro.py gen [n]        n Gemini variants (raw/intro_<k>.png), ~$0.13 each
  python3 tools/bb_intro.py pick <k>       variant k -> assets/ballbreaker/intro.png (1920 x 1080)

The game's title is not painted (models mangle lettering): the remake draws it over the picture.
"""
import os, subprocess, sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'remaster', 'build', 'zxremakes')
ASSETS = os.path.join(ROOT, 'remaster', 'assets', 'ballbreaker')
RAW = os.path.join(ASSETS, 'raw')
PALETTE = [(0, 0, 0), (0, 0, 215), (215, 0, 0), (215, 0, 215), (0, 215, 0), (0, 215, 215), (215, 215, 0), (215, 215, 215),
           (0, 0, 0), (0, 0, 255), (255, 0, 0), (255, 0, 255), (0, 255, 0), (0, 255, 255), (255, 255, 0), (255, 255, 255)]


def loading_screen():
    out = os.path.join(RAW, 'intro_dump')
    os.makedirs(out, exist_ok=True)
    subprocess.run([BIN, '--test', out, '--game', 'ballbreaker', '0.02@ram 0.04@quit'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ram = open(os.path.join(out, 'ram_000.bin'), 'rb').read()
    scr = ram[0x10000 + 5 * 0x4000:0x10000 + 5 * 0x4000 + 6912]  # bank 5: the screen
    im = Image.new('RGB', (256, 192))
    px = im.load()
    for y in range(192):
        row = (y & 0xC0) << 5 | (y & 7) << 8 | (y & 0x38) << 2
        for x in range(256):
            attr = scr[0x1800 + (y >> 3) * 32 + (x >> 3)]
            ink, paper, bright = attr & 7, attr >> 3 & 7, 8 if attr & 0x40 else 0
            px[x, y] = PALETTE[(ink if scr[row | x >> 3] & (0x80 >> (x & 7)) else paper) + bright]
    return im


def guide():
    os.makedirs(RAW, exist_ok=True)
    loading_screen().resize((2048, 1536), Image.NEAREST).save(os.path.join(RAW, 'intro_orig.png'))
    print('raw/intro_orig.png')


PROMPT = """Image 1 is the loading screen of "Ball Breaker" (ZX Spectrum, 1987) at its original low resolution.
Repaint it as the HD title key art of a modern remake, wide 16:9, in the remake's style: a TRON-like
digital world, deep black glossy void, everything drawn in glowing PURPLE / VIOLET neon light with
a little magenta and white-hot highlights; clean, sharp, premium.

Keep the original's composition and its elements, in the same places (stretched to 16:9):
- top left: a dark storm cloud with a jagged lightning bolt coming down from it, in neon;
- a thin bright laser line running diagonally from the lower left up to the right edge;
- on the right: a big glossy ball (glass and chrome, violet reflections) being smashed where the
  line hits it: fragments, sparks and small glowing spheres flying off;
- lower left: sloping hills of the original's sunset, as a neon wireframe terrain.
Leave the bottom quarter calm and dark (a title goes there later).
No text, no letters, no logo, no numbers, no UI. Do not copy the low-resolution pixels."""


def gen(n=2):
    import gemini
    os.makedirs(RAW, exist_ok=True)
    guide_im = loading_screen().resize((2048, 1536), Image.NEAREST)
    for _ in range(n):
        k = 0
        while os.path.exists(os.path.join(RAW, f'intro_{k}.png')):
            k += 1
        im = gemini.generate(PROMPT, [guide_im], aspect='16:9', size='2K', label=f'bb intro {k}')
        im.save(os.path.join(RAW, f'intro_{k}.png'))
        print('saved', f'intro_{k}.png', im.size)


def pick(k):
    im = Image.open(os.path.join(RAW, f'intro_{k}.png')).convert('RGB').resize((1920, 1080), Image.LANCZOS)
    im.save(os.path.join(ASSETS, 'intro.png'))
    print('assets/ballbreaker/intro.png')


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'guide': guide()
    elif cmd == 'gen': gen(int(sys.argv[2]) if len(sys.argv) > 2 else 2)
    elif cmd == 'pick': pick(int(sys.argv[2]))
    else: sys.exit(__doc__)
