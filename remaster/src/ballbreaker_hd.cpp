#include "ballbreaker_hd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <rlgl.h>

namespace {

constexpr int SPRITES = 60, SPRITE_W = 24, SPRITE_H = 20, SPRITE_BYTES = 120;
constexpr float CUBE = 12;
constexpr float FLOOR_X = 92, FLOOR_Z = 148, WALL_H = 64, LIP = 8;

// Neon palette.
constexpr Color NEON{190, 120, 255, 255};     // line core
constexpr Color NEON_DIM{120, 60, 200, 255};
constexpr Color GLASS{10, 5, 22, 255};          // surfaces
constexpr Color GLASS_FLOOR{14, 7, 30, 255};
constexpr Color VOID_FILL{3, 1, 8, 255};

// What each sprite is (the bricks in the game's instructions), drawn as that shape in that colour.
enum class Shape { Cube, Framed, LBlock, Pyramid, Diamond, Lying, Barrel, Ring, Wire, Prism, Ramp, Slabs };

struct Kind {
    Shape shape;
    Color colour;
};

Kind kind_of(int sprite) {
    const Color plain{170, 110, 255, 255};
    switch (sprite) {
    case -1: return {Shape::Cube, {240, 210, 255, 255}};                 // the bat
    case 0x01: return {Shape::Lying, {80, 220, 255, 255}};               // 5 extra missiles
    case 0x02: return {Shape::Ring, {90, 255, 150, 255}};                // bonus life
    case 0x03: return {Shape::Wire, {200, 240, 255, 255}};               // changes bat size
    case 0x04: case 0x31: case 0x32: case 0x33: case 0x3a:
        return {Shape::Pyramid, {255, 90, 220, 255}};                    // 200 points
    case 0x3b: return {Shape::Diamond, {255, 140, 240, 255}};
    case 0x05: return {Shape::LBlock, {255, 150, 70, 255}};              // 400 points
    case 0x06: return {Shape::Barrel, {255, 70, 90, 255}};               // bomb
    case 0x07: case 0x22: return {Shape::Prism, {255, 220, 90, 255}};    // 500 points
    case 0x0d: case 0x23: return {Shape::Ramp, {140, 200, 255, 255}};
    case 0x08: return {Shape::Framed, {255, 200, 60, 255}};              // 1000 bonus points
    case 0x10: return {Shape::Cube, {150, 150, 180, 255}};               // only a bomb breaks it
    case 0x0e: case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30:
        return {Shape::Slabs, {255, 120, 200, 255}};
    case 0x20: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
        return {Shape::Framed, {120, 160, 255, 255}};
    case 0x00: return {Shape::Cube, {110, 80, 240, 255}};
    case 0x21: return {Shape::Cube, {120, 170, 255, 255}};
    case 0x25: return {Shape::Cube, {200, 110, 255, 255}};
    case 0x29: return {Shape::Cube, {240, 100, 230, 255}};
    default: return {Shape::Cube, plain};
    }
}

// The 7 rooms, after the original's wall and floor patterns (notes/ballbreaker.md).
enum class Back { Crosses, Bricks, Studs, Squares, Plain };
enum class Floor { Grid, Checker, Dots, Dark };
struct RoomStyle {
    Back back;
    Floor floor;
    bool door;
};
const RoomStyle ROOM_STYLES[7] = {
    {Back::Crosses, Floor::Grid, false},     // A
    {Back::Crosses, Floor::Checker, true},   // B
    {Back::Bricks, Floor::Dark, false},      // C
    {Back::Crosses, Floor::Dots, false},     // D
    {Back::Plain, Floor::Dark, false},       // E
    {Back::Studs, Floor::Checker, false},    // F
    {Back::Squares, Floor::Checker, false},  // G
};

// Game 3D point -> canvas.
Vector2 P(float x, float y, float z) {
    return {(160 + x - z) * BBRenderer::SCALE, (118 - x / 2 - y) * BBRenderer::SCALE};
}

void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    // raylib wants counter-clockwise on screen (y down): flip if needed
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}

void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
}

void line(Vector2 a, Vector2 b, float w, Color c) { DrawLineEx(a, b, w, c); }

Color scale(Color c, float k) {
    return {uint8_t(std::min(255.f, c.r * k)), uint8_t(std::min(255.f, c.g * k)), uint8_t(std::min(255.f, c.b * k)), c.a};
}

const char* BLUR_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 dir;
void main() {
    float w[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540540, 0.0162162);
    vec4 c = texture(texture0, fragTexCoord) * w[0];
    for (int i = 1; i < 5; i++) {
        c += texture(texture0, fragTexCoord + dir * float(i)) * w[i];
        c += texture(texture0, fragTexCoord - dir * float(i)) * w[i];
    }
    finalColor = c;
}
)";

}  // namespace

BBRenderer::BBRenderer() {
    glow_ = LoadRenderTexture(W, H);
    half_a_ = LoadRenderTexture(W / 4, H / 4);
    half_b_ = LoadRenderTexture(W / 4, H / 4);
    for (auto* t : {&glow_, &half_a_, &half_b_}) SetTextureFilter(t->texture, TEXTURE_FILTER_BILINEAR);
    blur_ = LoadShaderFromMemory(nullptr, BLUR_FS);
    blur_dir_loc_ = GetShaderLocation(blur_, "dir");
}

BBRenderer::~BBRenderer() {
    UnloadRenderTexture(glow_);
    UnloadRenderTexture(half_a_);
    UnloadRenderTexture(half_b_);
    UnloadShader(blur_);
    if (sprites_.id) UnloadTexture(sprites_);
}

void BBRenderer::load_sprites(const uint8_t* data) {
    // atlas: one row of 60 sprites. Each row: 3 bytes graphics (1 = ink, dark), 3 bytes mask (1 = the
    // background shows); light (paper) white, dark black
    Image img = GenImageColor(SPRITES * SPRITE_W, SPRITE_H, BLANK);
    auto* px = static_cast<Color*>(img.data);
    for (int n = 0; n < SPRITES; n++)
        for (int y = 0; y < SPRITE_H; y++)
            for (int x = 0; x < SPRITE_W; x++) {
                const uint8_t* row = data + n * SPRITE_BYTES + y * 6;
                bool gfx = row[x / 8] & (0x80 >> (x % 8)), mask = row[3 + x / 8] & (0x80 >> (x % 8));
                Color c = mask ? BLANK : gfx ? Color{0, 0, 0, 255} : WHITE;
                px[y * img.width + n * SPRITE_W + x] = c;
            }
    if (sprites_.id) UnloadTexture(sprites_);
    sprites_ = LoadTextureFromImage(img);
    SetTextureFilter(sprites_, TEXTURE_FILTER_POINT);
    UnloadImage(img);
}

void BBRenderer::room(int room, double t, bool glow) {
    // the void around the room, with the ceiling's diagonal ribs
    if (!glow) {
        DrawRectangle(0, 0, W, FIELD_H, VOID_FILL);
        // ceiling ribs over the left wall (the walls, drawn next, cover the rest)
        for (int i = -10; i < 60; i++) {
            float x0 = float(i) * 48;
            line({x0, 0}, {x0 - 520, 260}, 3, Color{36, 16, 72, 255});
        }
    }
    Vector2 f0 = P(0, 0, 0), f1 = P(FLOOR_X, 0, 0), f2 = P(FLOOR_X, 0, FLOOR_Z), f3 = P(0, 0, FLOOR_Z);
    Vector2 w1 = P(FLOOR_X, WALL_H, 0), w2 = P(FLOOR_X, WALL_H, FLOOR_Z), w3 = P(0, WALL_H, FLOOR_Z);
    float pulse = 0.85f + 0.15f * float(std::sin(t * 2.0));
    Color core = scale(NEON, pulse), dim = scale(NEON_DIM, pulse);
    if (!glow) {
        quad(f0, f1, f2, f3, GLASS_FLOOR);                    // floor
        quad(f1, w1, w2, f2, GLASS);                          // back wall (X = 92)
        quad(f2, w2, w3, f3, Color{8, 4, 18, 255});           // left wall (Z = 148)
        // the lip: front face (X = 0) and right face (Z = 0), below the floor
        Vector2 d{0, LIP * SCALE};
        quad(f3, f0, {f0.x + d.x, f0.y + d.y}, {f3.x + d.x, f3.y + d.y}, Color{20, 8, 40, 255});
        quad(f0, f1, {f1.x, f1.y + d.y}, {f0.x, f0.y + d.y}, Color{26, 10, 52, 255});
    }
    float thin = glow ? 3 : 1.5f, edge = glow ? 7 : 3;
    const RoomStyle& st = ROOM_STYLES[std::clamp(room, 0, 6)];
    // floor
    if (st.floor == Floor::Checker && !glow)
        for (int i = 0; i * CUBE < FLOOR_X; i++)
            for (int j = 0; j * CUBE < FLOOR_Z; j++)
                if ((i + j) % 2 == 0) {
                    float xa = i * CUBE, xb = std::min(FLOOR_X, xa + CUBE), za = j * CUBE, zb = std::min(FLOOR_Z, za + CUBE);
                    quad(P(xa, 0, za), P(xb, 0, za), P(xb, 0, zb), P(xa, 0, zb), Color{26, 12, 54, 255});
                }
    if (st.floor == Floor::Grid || st.floor == Floor::Checker) {
        for (float x = CUBE; x < FLOOR_X; x += CUBE) line(P(x, 0, 0), P(x, 0, FLOOR_Z), thin, dim);
        for (float z = CUBE; z < FLOOR_Z; z += CUBE) line(P(0, 0, z), P(FLOOR_X, 0, z), thin, dim);
    } else if (st.floor == Floor::Dots) {
        for (float x = CUBE / 2; x < FLOOR_X; x += CUBE)
            for (float z = CUBE / 2; z < FLOOR_Z; z += CUBE) DrawCircleV(P(x, 0, z), glow ? 7 : 3.5f, dim);
    } else if (!glow) {  // dark glass: a soft sheen across it
        for (int i = 0; i < 6; i++) {
            float x = FLOOR_X * (0.15f + 0.14f * i);
            line(P(x, 0, 0), P(x, 0, FLOOR_Z), 1, Color{30, 14, 60, 255});
        }
    }
    // back wall: panels and the room's pattern on them
    auto wall_grid = [&](float step_z, float step_y, Color c) {
        for (float z = step_z; z < FLOOR_Z; z += step_z) line(P(FLOOR_X, 0, z), P(FLOOR_X, WALL_H, z), thin, c);
        for (float y = step_y; y < WALL_H; y += step_y) line(P(FLOOR_X, y, 0), P(FLOOR_X, y, FLOOR_Z), thin, c);
    };
    auto twinkle = [&](float z, float y) { return 0.6f + 0.4f * float(std::sin(t * 1.5 + z * 0.08 + y * 0.11)); };
    switch (st.back) {
    case Back::Crosses:
        wall_grid(CUBE, CUBE, scale(dim, 0.6f));
        for (float z = CUBE / 2; z < FLOOR_Z; z += CUBE)
            for (float y = CUBE / 2; y < WALL_H; y += CUBE) {
                Vector2 c = P(FLOOR_X, y, z);
                float r = SCALE * 1.6f;
                Color k = scale(core, twinkle(z, y));
                line({c.x - r, c.y}, {c.x + r, c.y}, thin, k);
                line({c.x, c.y - r}, {c.x, c.y + r}, thin, k);
            }
        break;
    case Back::Bricks:  // brickwork: rows 6 high, joints staggered
        for (float y = 6; y < WALL_H; y += 6) line(P(FLOOR_X, y, 0), P(FLOOR_X, y, FLOOR_Z), thin, dim);
        for (int row = 0; row * 6 < WALL_H; row++)
            for (float z = (row % 2) ? 6 : 0; z < FLOOR_Z; z += 12)
                if (z > 0) line(P(FLOOR_X, row * 6.f, z), P(FLOOR_X, std::min(WALL_H, row * 6.f + 6), z), thin, dim);
        break;
    case Back::Studs:
        wall_grid(CUBE, CUBE, scale(dim, 0.5f));
        for (float z = CUBE / 2; z < FLOOR_Z; z += CUBE / 2)
            for (float y = CUBE / 2; y < WALL_H; y += CUBE) {
                Vector2 c = P(FLOOR_X, y, z);
                float r = SCALE * 1.8f;
                Color k = scale(core, twinkle(z, y));
                line({c.x, c.y - r * 1.4f}, {c.x + r * 0.7f, c.y}, thin, k);
                line({c.x + r * 0.7f, c.y}, {c.x, c.y + r * 1.4f}, thin, k);
                line({c.x, c.y + r * 1.4f}, {c.x - r * 0.7f, c.y}, thin, k);
                line({c.x - r * 0.7f, c.y}, {c.x, c.y - r * 1.4f}, thin, k);
            }
        break;
    case Back::Squares:
        wall_grid(CUBE / 2, CUBE / 2, scale(dim, 0.4f));
        for (float z = 0; z < FLOOR_Z - 1; z += CUBE / 2)
            for (float y = 0; y < WALL_H - 1; y += CUBE / 2) {
                Vector2 a = P(FLOOR_X, y + 1.5f, z + 1.5f), b = P(FLOOR_X, y + 4.5f, z + 4.5f);
                DrawRectangleLinesEx({b.x, b.y, a.x - b.x, a.y - b.y}, thin, scale(core, 0.5f * twinkle(z, y)));
            }
        break;
    case Back::Plain: break;
    }
    // left wall grid
    if (st.back != Back::Plain) {
        for (float x = CUBE; x < FLOOR_X; x += CUBE) line(P(x, 0, FLOOR_Z), P(x, WALL_H, FLOOR_Z), thin, scale(dim, 0.5f));
        for (float y = CUBE; y < WALL_H; y += CUBE) line(P(0, y, FLOOR_Z), P(FLOOR_X, y, FLOOR_Z), thin, scale(dim, 0.5f));
    }
    if (st.door) {  // a doorway in the left wall: X 12..68, Y 12..56
        Vector2 a = P(12, 12, FLOOR_Z), b = P(68, 12, FLOOR_Z), c = P(68, 56, FLOOR_Z), d = P(12, 56, FLOOR_Z);
        quad(a, b, c, d, glow ? BLACK : VOID_FILL);
        line(a, b, edge, core);
        line(b, c, edge, core);
        line(c, d, edge, core);
        line(d, a, edge, core);
    }
    // the room's edges, bright
    line(f1, f2, edge, core);
    line(f2, f3, edge, core);
    line(f1, w1, edge, core);
    line(f2, w2, edge, core);
    line(w1, w2, edge, core);
    line(w2, w3, edge, core);
    line(f3, w3, edge, core);
    // the lip: a glowing rail along the right edge, its front edge
    Color rail = scale(Color{230, 160, 255, 255}, pulse);
    line(f0, f1, edge * 1.4f, rail);
    line(f0, f3, edge, core);
    line({f0.x, f0.y + LIP * SCALE}, {f1.x, f1.y + LIP * SCALE}, thin, dim);
    line({f0.x, f0.y + LIP * SCALE}, {f3.x, f3.y + LIP * SCALE}, thin, dim);
    // a light pulse running along the floor grid
    float run = float(std::fmod(t * 0.35, 1.0));
    float zr = FLOOR_Z * run;
    line(P(0, 0, zr), P(FLOOR_X, 0, zr), edge, scale(core, glow ? 0.9f : 0.5f));
}

void BBRenderer::brick(const BBObject& o, double t, bool glow) {
    Kind k = kind_of(o.sprite);
    Color c = k.colour;
    if (k.shape == Shape::Slabs)  // these shimmer in the original: pulse
        c = scale(c, 0.75f + 0.25f * float(std::sin(t * 8 + o.x * 0.3)));
    float x0 = float(o.x), y0 = float(o.y), z1 = float(o.z), z0 = z1 - CUBE, x1 = x0 + CUBE, y1 = y0 + CUBE;
    float xm = (x0 + x1) / 2, ym = (y0 + y1) / 2, zm = (z0 + z1) / 2;
    float w = glow ? 4 : 2.5f;
    auto fill = [&](float k9) { return glow ? BLACK : Color{uint8_t(c.r / k9), uint8_t(c.g / k9), uint8_t(c.b / k9), 255}; };
    auto edges = [&](std::initializer_list<std::pair<Vector2, Vector2>> es, Color col) {
        for (auto [p, q] : es) line(p, q, w, col);
    };
    // a box between two corners, its three visible faces: front (X = a), right (Z = b), top (Y = top)
    auto box = [&](float xa, float xb, float ya, float yb, float za, float zb, Color col, bool faces = true) {
        Vector2 a = P(xa, ya, zb), b = P(xa, ya, za), cc = P(xa, yb, za), d = P(xa, yb, zb);
        Vector2 e = P(xb, ya, za), f = P(xb, yb, za), g = P(xb, yb, zb);
        if (faces) {
            quad(a, b, cc, d, fill(9));
            quad(b, e, f, cc, fill(6));
            quad(d, cc, f, g, fill(4));
        }
        edges({{a, b}, {b, cc}, {cc, d}, {d, a}, {b, e}, {e, f}, {f, cc}, {f, g}, {g, d}}, col);
    };
    switch (k.shape) {
    case Shape::Cube: box(x0, x1, y0, y1, z0, z1, c); break;
    case Shape::Framed: {
        box(x0, x1, y0, y1, z0, z1, c);
        float m = 2.5f;  // an inner frame on the front and the right face
        Vector2 a = P(x0, y0 + m, z1 - m), b = P(x0, y0 + m, z0 + m), cc = P(x0, y1 - m, z0 + m), d = P(x0, y1 - m, z1 - m);
        Vector2 e = P(x1 - m, y0 + m, z0), f = P(x1 - m, y1 - m, z0), g = P(x0 + m, y1 - m, z0), h = P(x0 + m, y0 + m, z0);
        edges({{a, b}, {b, cc}, {cc, d}, {d, a}, {h, e}, {e, f}, {f, g}, {g, h}}, scale(c, 0.8f));
        break;
    }
    case Shape::LBlock:
        box(x0, x1, y0, ym, z0, z1, c);
        box(xm, x1, ym, y1, z0, z1, c);
        break;
    case Shape::Slabs:
        box(x0, x1, y0, y0 + 4, z0, z1, c);
        box(x0, x1, y0 + 7, y0 + 11, z0, z1, c);
        break;
    case Shape::Wire: box(x0, x1, y0, y1, z0, z1, c, false); break;
    case Shape::Pyramid: {
        Vector2 a = P(x0, y0, z1), b = P(x0, y0, z0), e = P(x1, y0, z0), apex = P(xm, y1, zm);
        tri(a, b, apex, fill(7));
        tri(b, e, apex, fill(4));
        edges({{a, b}, {b, e}, {a, apex}, {b, apex}, {e, apex}}, c);
        break;
    }
    case Shape::Diamond: {
        Vector2 a = P(x0, ym, z1), b = P(x0, ym, z0), e = P(x1, ym, z0), top = P(xm, y1, zm), bot = P(xm, y0, zm);
        tri(a, b, top, fill(6));
        tri(b, e, top, fill(4));
        tri(a, b, bot, fill(9));
        tri(b, e, bot, fill(7));
        edges({{a, b}, {b, e}, {a, top}, {b, top}, {e, top}, {a, bot}, {b, bot}, {e, bot}}, c);
        break;
    }
    case Shape::Prism: {  // a triangular bar along X: triangle on the front face
        Vector2 a = P(x0, y0, z1), b = P(x0, y0, z0), apex = P(x0, y1, zm);
        Vector2 e = P(x1, y0, z0), apex2 = P(x1, y1, zm);
        quad(b, e, apex2, apex, fill(4));
        tri(a, b, apex, fill(8));
        edges({{a, b}, {b, apex}, {apex, a}, {b, e}, {e, apex2}, {apex, apex2}}, c);
        break;
    }
    case Shape::Ramp: {  // rising towards the back
        Vector2 a = P(x0, y0, z1), b = P(x0, y0, z0), e = P(x1, y0, z0), f = P(x1, y1, z0), g = P(x1, y1, z1);
        quad(a, b, f, g, fill(4));
        tri(b, e, f, fill(7));
        edges({{a, b}, {b, e}, {e, f}, {b, f}, {a, g}, {f, g}}, c);
        break;
    }
    case Shape::Lying:
    case Shape::Barrel:
    case Shape::Ring: {
        // round things: a glowing disc / tube, the front face is undistorted in this projection
        Vector2 fc = P(x0, ym, zm), bc = P(x1, ym, zm);
        float r = 5.5f * SCALE;
        if (k.shape == Shape::Lying) {
            Vector2 dir{bc.x - fc.x, bc.y - fc.y};
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            Vector2 n{-dir.y / len * r, dir.x / len * r};
            quad({fc.x + n.x, fc.y + n.y}, {bc.x + n.x, bc.y + n.y}, {bc.x - n.x, bc.y - n.y}, {fc.x - n.x, fc.y - n.y}, fill(5));
            DrawCircleV(bc, r, fill(5));
            line({fc.x + n.x, fc.y + n.y}, {bc.x + n.x, bc.y + n.y}, w, c);
            line({fc.x - n.x, fc.y - n.y}, {bc.x - n.x, bc.y - n.y}, w, c);
            DrawCircleV(fc, r, fill(7));
            DrawRing(fc, r - w, r, 0, 360, 40, c);
        } else if (k.shape == Shape::Barrel) {
            Vector2 bottom = P(x0 + 2, y0, zm), top = P(x0 + 2, y1, zm);
            DrawRectangleV({bottom.x - r, top.y}, {2 * r, bottom.y - top.y}, fill(6));
            DrawEllipse(int(top.x), int(top.y), r, r * 0.45f, fill(3));
            DrawEllipseLines(int(top.x), int(top.y), r, r * 0.45f, c);
            line({bottom.x - r, top.y}, {bottom.x - r, bottom.y}, w, c);
            line({bottom.x + r, top.y}, {bottom.x + r, bottom.y}, w, c);
            DrawCircleV({top.x, (top.y + bottom.y) / 2}, r * 0.35f, glow ? c : scale(c, 0.9f));  // the fuse light
        } else {
            DrawRing(fc, r * 0.55f, r, 0, 360, 40, c);
            if (glow) DrawCircleV(fc, r * 0.4f, scale(c, 0.6f));
        }
        break;
    }
    }
}

void BBRenderer::ball(const BBObject& o, double t, bool glow) {
    Vector2 c{(o.sx + 12) * SCALE, (o.sy + 10) * SCALE};
    float r = 6 * SCALE;
    // height above the floor: how far the sprite is above where it would be lying on it
    float lying_y = 100 - o.x / 2.0f, h = lying_y - o.sy;
    if (!glow && h > -2) {
        Vector2 s{c.x, (lying_y + 16) * SCALE};
        float k = std::clamp(1 - h / 70, 0.25f, 1.0f);
        DrawEllipse(int(s.x), int(s.y), r * 1.2f * k, r * 0.45f * k, Fade(BLACK, 0.7f * k));
        DrawEllipse(int(s.x), int(s.y), r * 0.9f * k, r * 0.3f * k, Fade(Color{150, 80, 255, 255}, 0.25f * k));
    }
    if (!glow) {
        DrawCircleV(c, r, Color{30, 10, 60, 255});
        DrawCircleV({c.x - r * 0.3f, c.y - r * 0.3f}, r * 0.55f, Color{150, 90, 255, 255});
        DrawCircleV({c.x - r * 0.35f, c.y - r * 0.35f}, r * 0.22f, WHITE);
    } else {
        DrawCircleV(c, r * 0.9f, Color{170, 110, 255, 255});
    }
    DrawRing(c, r - 3, r + (glow ? 4 : 1), 0, 360, 48, Color{230, 190, 255, 255});
    (void)t;
}

void BBRenderer::sprite(const BBObject& o, Color tint) {
    if (!sprites_.id || o.sprite < 0 || o.sprite >= SPRITES) return;
    Rectangle src{float(o.sprite * SPRITE_W), 0, float(SPRITE_W), float(SPRITE_H)};
    Rectangle dst{o.sx * SCALE, o.sy * SCALE, SPRITE_W * SCALE, SPRITE_H * SCALE};
    DrawTexturePro(sprites_, src, dst, {0, 0}, 0, tint);
}

void BBRenderer::burst(const BBObject& o, int points) {
    Kind k = kind_of(o.sprite);
    Vector2 c = P(o.x + CUBE / 2, o.y + CUBE / 2, o.z - CUBE / 2);
    std::uniform_real_distribution<float> u(0, 1);
    for (int i = 0; i < 46; i++) {
        float a = u(rng_) * 6.2832f, sp = 150 + u(rng_) * 650;
        parts_.push_back({c, {std::cos(a) * sp, std::sin(a) * sp - 250}, 0, 0.5f + u(rng_) * 0.6f, 3 + u(rng_) * 5,
                          u(rng_) < 0.3f ? WHITE : k.colour});
    }
    parts_.push_back({c, {0, 0}, 0, 0.25f, -1, k.colour});  // the flash (size < 0)
    if (points > 0) popups_.push_back({{c.x, c.y - 40}, 0, points, k.colour});
}

void BBRenderer::effects(bool glow) {
    for (const Particle& p : parts_) {
        float a = 1 - p.life / p.max;
        if (p.size < 0) {  // flash: an expanding ring
            float r = 30 + 260 * (1 - a);
            DrawRing(p.p, r * 0.8f, r, 0, 360, 48, Fade(p.c, a * (glow ? 1 : 0.7f)));
            DrawCircleV(p.p, 60 * a, Fade(WHITE, a * 0.8f));
        } else {
            DrawCircleV(p.p, p.size * (glow ? 1.6f : 1) * (0.4f + 0.6f * a), Fade(p.c, a));
        }
    }
    if (!glow)
        for (const Popup& u : popups_) {
            float a = 1 - u.life / 1.1f;
            std::string txt = "+" + std::to_string(u.points);
            int fs = 56;
            DrawText(txt.c_str(), int(u.p.x - MeasureText(txt.c_str(), fs) / 2), int(u.p.y - u.life * 120), fs, Fade(WHITE, a));
        }
}

void BBRenderer::scene(const BBState& s, double t, bool glow) {
    room(s.room, t, glow);
    // painter's order, as the game sorts: far (large X) first, then left (large Z), low first
    std::vector<const BBObject*> order;
    for (const auto& o : s.objects) order.push_back(&o);
    std::stable_sort(order.begin(), order.end(), [](const BBObject* a, const BBObject* b) {
        if (a->x != b->x) return a->x > b->x;
        if (a->z != b->z) return a->z > b->z;
        return a->y < b->y;
    });
    // the ball's trail
    for (size_t i = 0; i + 1 < trail_.size(); i++) {
        float a = float(i + 1) / float(trail_.size());
        DrawCircleV(trail_[i], 4 * SCALE * (0.25f + 0.5f * a), Fade(Color{170, 100, 255, 255}, (glow ? 0.35f : 0.15f) * a));
    }
    for (const BBObject* o : order) {
        if (o->slot == 0) ball(*o, t, glow);
        else if (o->slot >= 3 && o->sprite >= 0x19 && o->sprite <= 0x1f) continue;  // exploding: the burst shows it
        else if (o->slot >= 3 && !(o->sprite >= 0x11 && o->sprite <= 0x18)) brick(*o, t, glow);
        else if (o->slot == 1) {
            // the bat: a box where its sprite is (its own 3D Z is its middle, not an edge)
            BBObject bat = *o;
            bat.z = 160 + o->x - o->sx;
            bat.sprite = -1;
            brick(bat, t, glow);
        } else if (o->sprite >= 0x11 && o->sprite <= 0x14)  // the creature
            sprite(*o, glow ? Color{120, 255, 170, 255} : Color{140, 255, 190, 255});
        else
            sprite(*o, glow ? Color{220, 160, 255, 255} : Color{200, 150, 255, 255});
    }
    effects(glow);
}

void BBRenderer::prepare(const BBState& s, double t) {
    float dt = last_t_ < 0 ? 0 : float(std::clamp(t - last_t_, 0.0, 0.1));
    last_t_ = t;
    for (Particle& p : parts_) {
        p.life += dt;
        p.p.x += p.v.x * dt, p.p.y += p.v.y * dt;
        p.v.y += 900 * dt;  // a little gravity
        p.v.x *= 1 - 1.5f * dt;
    }
    std::erase_if(parts_, [](const Particle& p) { return p.life >= p.max; });
    for (Popup& u : popups_) u.life += dt;
    std::erase_if(popups_, [](const Popup& u) { return u.life >= 1.1f; });
    const BBObject* ball = nullptr;
    for (const auto& o : s.objects)
        if (o.slot == 0) ball = &o;
    if (ball) {
        Vector2 c{(ball->sx + 12) * SCALE, (ball->sy + 10) * SCALE};
        if (trail_.empty() || std::hypot(c.x - trail_.back().x, c.y - trail_.back().y) > 4) trail_.push_back(c);
        if (!trail_.empty() && std::hypot(c.x - trail_.back().x, c.y - trail_.back().y) > 200) trail_.clear();
        while (trail_.size() > 10) trail_.pop_front();
    } else
        trail_.clear();
    BeginTextureMode(glow_);
    ClearBackground(BLACK);
    scene(s, t, true);
    EndTextureMode();
    // blur at quarter size, twice each way
    BeginTextureMode(half_a_);
    ClearBackground(BLACK);
    DrawTexturePro(glow_.texture, {0, 0, float(W), -float(H)}, {0, 0, float(W / 4), float(H / 4)}, {0, 0}, 0, WHITE);
    EndTextureMode();
    for (int pass = 0; pass < 2; pass++)
        for (int axis = 0; axis < 2; axis++) {
            RenderTexture2D& src = axis == 0 ? half_a_ : half_b_;
            RenderTexture2D& dst = axis == 0 ? half_b_ : half_a_;
            float dir[2] = {axis == 0 ? 1.0f / (W / 4) : 0, axis == 1 ? 1.0f / (H / 4) : 0};
            SetShaderValue(blur_, blur_dir_loc_, dir, SHADER_UNIFORM_VEC2);
            BeginTextureMode(dst);
            ClearBackground(BLACK);
            BeginShaderMode(blur_);
            DrawTexturePro(src.texture, {0, 0, float(W / 4), -float(H / 4)}, {0, 0, float(W / 4), float(H / 4)}, {0, 0},
                           0, WHITE);
            EndShaderMode();
            EndTextureMode();
        }
}

void BBRenderer::debug_dump(const char* dir) {
    Image a = LoadImageFromTexture(glow_.texture);
    ExportImage(a, (std::string(dir) + "/glow.png").c_str());
    UnloadImage(a);
    Image b = LoadImageFromTexture(half_a_.texture);
    ExportImage(b, (std::string(dir) + "/glow_blur.png").c_str());
    UnloadImage(b);
}

void BBRenderer::status_bar(const BBState& s) {
    DrawRectangle(0, FIELD_H, W, H - FIELD_H, Color{8, 4, 18, 255});
    DrawLineEx({0, float(FIELD_H) + 2}, {float(W), float(FIELD_H) + 2}, 3, NEON);
    char buf[64];
    std::snprintf(buf, sizeof buf, "SCORE  %07d", s.score);
    DrawText(buf, 60, FIELD_H + 36, 52, RAYWHITE);
    std::snprintf(buf, sizeof buf, "SCREEN  %d", s.level + 1);
    DrawText(buf, 640, FIELD_H + 36, 52, Color{200, 160, 255, 255});
    DrawText("LIVES", 1020, FIELD_H + 36, 52, RAYWHITE);
    for (int i = 0; i < std::min(s.lives, 9); i++) DrawCircle(1220 + i * 56, FIELD_H + 62, 18, Color{220, 140, 255, 255});
    std::snprintf(buf, sizeof buf, "MISSILES  %02d", s.missiles);
    DrawText(buf, 1500, FIELD_H + 36, 52, RAYWHITE);
}

void BBRenderer::draw(const BBState& s, double t) {
    scene(s, t, false);
    BeginBlendMode(BLEND_ADDITIVE);
    DrawTexturePro(half_a_.texture, {0, 0, float(W / 4), -float(H / 4)}, {0, 0, float(W), float(H)}, {0, 0}, 0, WHITE);
    EndBlendMode();
    status_bar(s);
}
