"""Ball Breaker rooms: extract the 7 clean backgrounds from the running game, repaint them in HD.

  python3 tools/bb_rooms.py dump                 RAM at the start of one level per room (the game
                                                 runs in the remaster's test mode)
  python3 tools/bb_rooms.py guides               the original backgrounds, scaled up, as PNG
  python3 tools/bb_rooms.py style <name> [room]  one style test for a room (default A): a Gemini call

The room background is the game's clean playfield buffer (0x7000, 256 x 128, 32 bytes a row, set bit =
ink), without bricks, ball or bat. All 7 rooms share one geometry (notes/ballbreaker.md). Output in
remaster/assets/ballbreaker/raw/ (git-ignored).
"""
import os, subprocess, sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'remaster', 'build', 'zxremakes')
RAW = os.path.join(ROOT, 'remaster', 'assets', 'ballbreaker', 'raw')
DUMPS = os.path.join(RAW, 'dumps')

# room -> a level (1-based) that uses it
ROOMS = {'A': 1, 'B': 2, 'C': 3, 'D': 6, 'E': 5, 'F': 9, 'G': 15}
LEVEL_TABLE = 0xD5F5
ZX_COLOURS = [(0, 0, 0), (0, 0, 215), (215, 0, 0), (215, 0, 215), (0, 215, 0), (0, 215, 215), (215, 215, 0),
              (215, 215, 215)]


def dump_path(level):
    return os.path.join(DUMPS, f'level_{level:02d}', 'ram_000.bin')


def dump():
    for room, level in ROOMS.items():
        out = os.path.dirname(dump_path(level))
        os.makedirs(out, exist_ok=True)
        script = (f'0.2+space 0.4-space 1@pokeat8b78:8070={level - 1:02x} 2+enter 2.12-enter '
                  '12.5@ram 12.5@shot 12.6@quit')
        subprocess.run([BIN, '--test', out, '--game', 'ballbreaker', script], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print('room', room, 'level', level, '->', out)


def level_attr(ram, level):
    a = LEVEL_TABLE
    for _ in range(level - 1):
        a += 2 + (ram[a] | ram[a + 1] << 8)
    return ram[a + 2]


def background(room):
    """The room as the game shows it on its level: ink / paper of the level's attribute."""
    level = ROOMS[room]
    ram = open(dump_path(level), 'rb').read()
    attr = level_attr(ram, level)
    bright = 40 if attr & 0x40 else 0
    ink = tuple(min(255, c + bright) if c else 0 for c in ZX_COLOURS[attr & 7])
    paper = tuple(min(255, c + bright) if c else 0 for c in ZX_COLOURS[attr >> 3 & 7])
    im = Image.new('RGB', (256, 128))
    px = im.load()
    for y in range(128):
        for x in range(256):
            px[x, y] = ink if ram[0x7000 + y * 32 + x // 8] & (0x80 >> (x % 8)) else paper
    return im


def guides():
    os.makedirs(RAW, exist_ok=True)
    for room in ROOMS:
        background(room).resize((2048, 1024), Image.NEAREST).save(os.path.join(RAW, f'room_{room}_orig.png'))
        print('room', room)


SCENE = """Image 1 is the LAYOUT: one screen of "Ball Breaker" (1987), a breakout game in an isometric 3D
room, at its original low resolution (two colours, dithered). It shows an empty room seen from the
front right and a little above:
- top left: a slanted CEILING band (diagonal stripes) over the left side, and a thin top strip along the
  whole top edge;
- under it the LEFT WALL (fine checker dither), running diagonally from the top middle down to the
  bottom left;
- right half: the BACK WALL, covered with a regular grid of small star / cross ornaments;
- bottom: the FLOOR, rows of tiles in perspective, running from the back wall to the front;
- along the right and bottom right: the striped front LIP of the floor (vertical stripes), where the
  player's bat slides;
- black: the void outside the room (bottom left below the floor, right below the lip)."""

RULES = """Repaint this exact room as modern HD game art:
- Keep the geometry EXACTLY: every edge, corner, wall, the floor outline and the striped lip stay
  exactly where they are, same size, same perspective. No zoom, no crop, no shift, no tilt.
- The back wall keeps a regular grid of small ornaments in the same places, the floor keeps its rows of
  tiles in the same perspective, the lip keeps its stripes as a rail, the ceiling its diagonal ribs.
- Do not copy the low-resolution dithered pixels: repaint them with real materials, shading and light.
  The two flat colours of image 1 are only placeholders, not the palette.
- The room is EMPTY: no bricks, blocks, balls, bats, characters, text, numbers or UI.
- The void outside the room (bottom left below the floor, right below the lip) is plain flat black."""

STYLES = {
    'neon': """Style: a sleek futuristic arena, like a premium modern 3D breakout game. Dark graphite and
gunmetal panels, the back wall's ornaments are small glowing studs set in a grid of panels, the floor is glossy
dark tiles with soft reflections, the ceiling has diagonal light ribs, the lip is a polished chrome rail. Accent colour: warm golden yellow (this
level's colour) in thin glowing seams and the lamps. Soft global illumination, crisp details.""",
    'toy': """Style: a bright, friendly stylised 3D toy room, like a modern casual console game. Smooth
rounded ceramic and plastic materials, the back wall's ornaments are small embossed star tiles, the floor is
glazed tiles, the ceiling has diagonal rounded ribs, the lip is a rounded plastic rail. Main colour: warm sunny yellow (this level's
colour) with cream and soft orange, gentle studio lighting with soft shadows.""",
    'temple': """Style: an ancient golden temple chamber, like a modern fantasy puzzle game. Carved sandstone
walls, the back wall's ornaments are small carved star rosettes, the floor is polished stone tiles with
thin gold inlays, the ceiling has diagonal carved beams, the lip is a carved stone ledge with a gold trim. Warm golden light from the top left,
rich but clean details.""",
    'tron': """Style: a TRON-like digital arena, like a premium modern neon game. Every surface is deep black
glossy glass with faint reflections; all edges, the room's corners and the tile seams are thin glowing
PURPLE / violet neon lines (light purple core, soft violet glow). The back wall's ornaments are small
glowing violet nodes in a grid of dark panels, the floor is a reflective black grid of tiles outlined in
violet light, the ceiling has diagonal violet light strips, the lip is a glowing violet energy rail.
Mostly black, the light is all violet neon. Clean, sharp, minimal.""",
    'tron_accent': """Style: a TRON-like digital arena, like a premium modern neon game. Every surface is deep black
glossy glass with faint reflections; all edges, the room's corners and the tile seams are thin glowing
PURPLE / violet neon lines (light purple core, soft violet glow). The back wall's ornaments are small
glowing nodes in a grid of dark panels, the floor is a reflective black grid of tiles outlined in violet
light, the ceiling has diagonal light strips, the lip is a glowing energy rail. A second, smaller accent
colour: warm golden yellow (this level's colour) in the back wall's nodes and the rail's core only;
everything else violet. Mostly black. Clean, sharp, minimal.""",
}


def style(name, room='A'):
    import gemini
    os.makedirs(RAW, exist_ok=True)
    guide = background(room).resize((2048, 1024), Image.NEAREST)
    # Gemini has no 2:1: pad to 16:9 with void, crop it off again
    canvas = Image.new('RGB', (2048, 1152))
    canvas.paste(guide, (0, 64))
    prompt = '\n\n'.join([SCENE, RULES, STYLES[name]])
    raw = gemini.generate(prompt, [canvas], aspect='16:9', size='2K', label=f'bb room {room} {name}')
    n = 0
    while os.path.exists(os.path.join(RAW, f'style_{name}_{room}_{n}.png')):
        n += 1
    raw.save(os.path.join(RAW, f'style_{name}_{room}_{n}_full.png'))
    w, h = raw.size
    pad = round(h * 64 / 1152)
    hd = raw.crop((0, pad, w, h - pad)).resize((1920, 960), Image.LANCZOS)
    hd.save(os.path.join(RAW, f'style_{name}_{room}_{n}.png'))
    print('saved', f'style_{name}_{room}_{n}.png', raw.size)


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'dump': dump()
    elif cmd == 'guides': guides()
    elif cmd == 'style': style(sys.argv[2], *(sys.argv[3:4]))
    else: sys.exit(__doc__)
