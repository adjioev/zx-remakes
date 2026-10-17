#include "ballbreaker_hd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <rlgl.h>

namespace {

constexpr int SPRITES = 60, SPRITE_W = 24, SPRITE_H = 20, SPRITE_BYTES = 120;
constexpr float CUBE = 12;
constexpr float CREATURE_PAD = 3;  // sprite pixels around each painted creature frame (tools/bb_creatures.py)
constexpr float WALL_H = 64, LIP = 8;
constexpr float ROOM_X = 92, ROOM_Z = 148;  // the original's room: back wall at X = 92, left wall at Z = 148
// this level's: some levels have bricks set into a wall (the original's flat sprites just cover
// it); the walls move out to them, so they stand against the wall (BBRenderer::prepare)
float FLOOR_X = ROOM_X, FLOOR_Z = ROOM_Z;

// What each sprite is (the bricks in the game's instructions), drawn as that shape in that colour.
enum class Shape { Cube, Armored, Framed, LBlock, Pyramid, Diamond, Lying, Barrel, LifeBall, Wire, Prism, Ramp, Slabs };

struct Kind {
    Shape shape;
    Color colour;
};

Kind kind_of(int sprite) {
    const Color plain{170, 110, 255, 255};
    switch (sprite) {
    case -1: return {Shape::Cube, {240, 210, 255, 255}};                 // the bat
    case 0x01: return {Shape::Lying, {80, 220, 255, 255}};               // 5 extra missiles
    case 0x02: return {Shape::LifeBall, {90, 255, 150, 255}};                // bonus life
    case 0x03: return {Shape::Wire, {200, 240, 255, 255}};               // changes bat size
    case 0x04: case 0x31: case 0x32: case 0x33: case 0x3a:
        return {Shape::Pyramid, {255, 90, 220, 255}};                    // 200 points
    case 0x3b: return {Shape::Diamond, {255, 140, 240, 255}};
    case 0x05: return {Shape::LBlock, {255, 150, 70, 255}};              // 400 points
    case 0x06: return {Shape::Barrel, {255, 70, 90, 255}};               // bomb
    case 0x07: case 0x22: return {Shape::Prism, {255, 220, 90, 255}};    // 500 points
    case 0x0d: case 0x23: return {Shape::Ramp, {140, 200, 255, 255}};
    case 0x08: return {Shape::Framed, {255, 200, 60, 255}};              // 1000 bonus points
    case 0x09: return {Shape::Armored, {170, 190, 210, 255}};            // only a bomb breaks it
    case 0x10: return {Shape::Cube, {150, 150, 180, 255}};               // 600 points
    case 0x0e: case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30:
        return {Shape::Slabs, {255, 120, 200, 255}};
    case 0x20: return {Shape::Armored, {120, 160, 190, 255}};             // bomb or missile
    case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
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

// Each room's materials and light, so the 7 look like 7 places (the patterns stay the original's).
// Surfaces are mid-dark so the ball and the bricks read on any of them.
enum class Look { Neon, Reactor, Copper, Circuit, Obsidian, Ice, Temple };
struct RoomPalette {
    Look look;
    Color void_fill, ribs;
    Color floor, floor_far, floor_alt;  // near / far edge of the floor, the checker's other squares
    Color back_low, back_high;          // the back wall, bottom and top
    Color left_low, left_high;
    Color lip_front, lip_right;
    Color edge, core, dim;  // the room's edges; patterns and grid (scaled by GRID)
    Color rail, accent;     // the front rail; the theme's own light (door, inlays, nodes)
};
const RoomPalette THEMES[7] = {
    // A: neon arena - dark glass, violet and pink edges
    {Look::Neon, {3, 1, 8, 255}, {36, 16, 72, 255}, {16, 8, 34, 255}, {10, 5, 24, 255}, {28, 12, 58, 255},
     {12, 6, 28, 255}, {8, 4, 18, 255}, {10, 5, 22, 255}, {6, 3, 14, 255}, {20, 8, 40, 255}, {26, 10, 52, 255},
     {200, 120, 255, 255}, {200, 120, 255, 255}, {120, 60, 200, 255}, {255, 120, 220, 255}, {255, 110, 210, 255}},
    // B: reactor - graphite metal, amber light, a glowing door
    {Look::Reactor, {4, 3, 2, 255}, {34, 26, 16, 255}, {30, 28, 27, 255}, {18, 17, 17, 255}, {40, 37, 34, 255},
     {34, 30, 27, 255}, {16, 15, 15, 255}, {26, 24, 22, 255}, {12, 12, 12, 255}, {36, 32, 28, 255}, {46, 40, 34, 255},
     {255, 170, 60, 255}, {255, 180, 80, 255}, {170, 100, 30, 255}, {255, 200, 110, 255}, {255, 150, 40, 255}},
    // C: copper machine - copper panels, warm light, rivets
    {Look::Copper, {5, 3, 2, 255}, {44, 24, 12, 255}, {44, 26, 16, 255}, {24, 14, 9, 255}, {54, 32, 20, 255},
     {70, 38, 20, 255}, {34, 18, 10, 255}, {50, 27, 14, 255}, {24, 13, 8, 255}, {60, 34, 18, 255}, {80, 44, 22, 255},
     {255, 160, 90, 255}, {255, 190, 120, 255}, {180, 100, 50, 255}, {255, 210, 150, 255}, {255, 200, 120, 255}},
    // D: emerald circuit - dark panels, green traces, light nodes
    {Look::Circuit, {1, 5, 3, 255}, {10, 40, 24, 255}, {8, 22, 16, 255}, {4, 12, 9, 255}, {12, 30, 22, 255},
     {8, 20, 15, 255}, {4, 10, 8, 255}, {6, 16, 12, 255}, {3, 8, 6, 255}, {10, 26, 18, 255}, {12, 34, 24, 255},
     {60, 255, 150, 255}, {80, 255, 160, 255}, {30, 150, 90, 255}, {150, 255, 200, 255}, {170, 255, 120, 255}},
    // E: obsidian - near black walls, red accents
    {Look::Obsidian, {2, 1, 1, 255}, {26, 6, 8, 255}, {10, 8, 9, 255}, {5, 4, 5, 255}, {14, 10, 11, 255},
     {9, 7, 8, 255}, {4, 3, 4, 255}, {7, 5, 6, 255}, {3, 2, 3, 255}, {16, 8, 9, 255}, {22, 10, 11, 255},
     {255, 50, 60, 255}, {255, 70, 70, 255}, {150, 20, 30, 255}, {255, 90, 80, 255}, {255, 40, 50, 255}},
    // F: ice hall - frosted blue, cold white light
    {Look::Ice, {2, 5, 10, 255}, {30, 60, 96, 255}, {44, 72, 104, 255}, {26, 44, 70, 255}, {58, 90, 124, 255},
     {52, 84, 120, 255}, {28, 48, 76, 255}, {40, 66, 98, 255}, {20, 36, 60, 255}, {50, 80, 112, 255}, {66, 100, 136, 255},
     {225, 245, 255, 255}, {210, 240, 255, 255}, {130, 180, 230, 255}, {240, 250, 255, 255}, {170, 230, 255, 255}},
    // G: temple of light - light stone, gold seams, turquoise inlays
    {Look::Temple, {10, 9, 7, 255}, {60, 50, 30, 255}, {150, 142, 126, 255}, {116, 110, 98, 255}, {136, 128, 112, 255},
     {164, 156, 140, 255}, {124, 118, 106, 255}, {140, 133, 120, 255}, {100, 95, 86, 255}, {118, 110, 96, 255}, {140, 130, 112, 255},
     {255, 215, 120, 255}, {255, 214, 120, 255}, {225, 175, 70, 255}, {255, 230, 160, 255}, {40, 200, 190, 255}},
};
const RoomPalette& theme_of(int room) { return THEMES[std::clamp(room, 0, 6)]; }

// Classic uses the Spectrum's bright paper colours with solid shaded faces.
// Geometry and the seven original room patterns are shared with Neon.
RoomPalette classic_room(int room) {
    constexpr Color paper[] = {{242, 210, 70, 255}, {88, 211, 224, 255}, {98, 210, 121, 255},
                             {222, 126, 213, 255}, {224, 228, 223, 255}, {115, 181, 234, 255},
                             {237, 210, 129, 255}};
    Color c = paper[std::clamp(room, 0, 6)];
    auto shade = [&](float k) { return Color{uint8_t(c.r*k), uint8_t(c.g*k), uint8_t(c.b*k), 255}; };
    const Color ink{44, 50, 65, 255};
    return {Look::Neon, {28, 34, 47, 255}, {40, 48, 63, 255},
            shade(0.74f), shade(0.65f), shade(0.87f),
            shade(0.88f), c, shade(0.70f), shade(0.82f),
            shade(0.50f), shade(0.60f), ink, shade(0.38f), shade(0.48f),
            ink, {255, 244, 191, 255}};
}

// Game 3D point -> canvas. g_scale / g_off: drawn elsewhere and at another size (the menus'
// brick thumbnails); the play field is SCALE at 0, 0.
float g_scale = BBRenderer::SCALE;
Vector2 g_off{0, 0};
Vector2 P(float x, float y, float z) {
    return {(160 + x - z) * g_scale + g_off.x, (118 - x / 2 - y) * g_scale + g_off.y};
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

// A triangle with a colour per corner (raylib's DrawTriangle has one).
void tri_grad(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) std::swap(b, c), std::swap(cb, cc);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(ca.r, ca.g, ca.b, ca.a);
    rlVertex2f(a.x, a.y);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a);
    rlVertex2f(b.x, b.y);
    rlColor4ub(cc.r, cc.g, cc.b, cc.a);
    rlVertex2f(c.x, c.y);
    rlEnd();
}

void quad_grad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color ca, Color cb, Color cc, Color cd) {
    tri_grad(a, b, c, ca, cb, cc);
    tri_grad(a, c, d, ca, cc, cd);
}

Color add(Color a, Color b, float k) {
    return {uint8_t(std::min(255.f, a.r + b.r * k)), uint8_t(std::min(255.f, a.g + b.g * k)),
            uint8_t(std::min(255.f, a.b + b.b * k)), a.a};
}

// How bright the room's grid and patterns are against its edges: low, so the ball reads anywhere.
constexpr float GRID = 0.55f;

Color scale(Color c, float k) {
    return {uint8_t(std::min(255.f, c.r * k)), uint8_t(std::min(255.f, c.g * k)), uint8_t(std::min(255.f, c.b * k)), c.a};
}

// Sprites as vectors. A polyline loses its points that lie (within eps) on the line between their
// neighbours (Douglas-Peucker), then has its corners cut (Chaikin): pixel steps become clean lines.
void simplify(const std::vector<Vector2>& in, size_t a, size_t b, float eps, std::vector<Vector2>& out) {
    float dmax = 0;
    size_t imax = a;
    Vector2 p = in[a], q = in[b];
    float lx = q.x - p.x, ly = q.y - p.y, ll = std::max(1e-6f, std::sqrt(lx * lx + ly * ly));
    for (size_t i = a + 1; i < b; i++) {
        float d = std::fabs((in[i].x - p.x) * ly - (in[i].y - p.y) * lx) / ll;
        if (d > dmax) dmax = d, imax = i;
    }
    if (dmax > eps) {
        simplify(in, a, imax, eps, out);
        simplify(in, imax, b, eps, out);
    } else
        out.push_back(in[b]);
}
std::vector<Vector2> smooth(const std::vector<Vector2>& pts, bool closed, float eps, int rounds) {
    if (pts.size() < 3) return pts;
    std::vector<Vector2> s;
    if (closed) {  // split at the point farthest from the first
        size_t far = 0;
        float best = -1;
        for (size_t i = 0; i < pts.size(); i++) {
            float d = (pts[i].x - pts[0].x) * (pts[i].x - pts[0].x) + (pts[i].y - pts[0].y) * (pts[i].y - pts[0].y);
            if (d > best) best = d, far = i;
        }
        std::vector<Vector2> ring(pts);
        ring.push_back(pts[0]);
        s.push_back(ring[0]);
        simplify(ring, 0, far, eps, s);
        simplify(ring, far, ring.size() - 1, eps, s);
        s.pop_back();  // the first again
    } else {
        s.push_back(pts[0]);
        simplify(pts, 0, pts.size() - 1, eps, s);
    }
    for (int r = 0; r < rounds && s.size() >= 3; r++) {
        std::vector<Vector2> c;
        size_t n = s.size(), m = closed ? n : n - 1;
        if (!closed) c.push_back(s[0]);
        for (size_t i = 0; i < m; i++) {
            Vector2 a = s[i], b = s[(i + 1) % n];
            c.push_back({a.x * 0.75f + b.x * 0.25f, a.y * 0.75f + b.y * 0.25f});
            c.push_back({a.x * 0.25f + b.x * 0.75f, a.y * 0.25f + b.y * 0.75f});
        }
        if (!closed) c.push_back(s[n - 1]);
        s = std::move(c);
    }
    return s;
}

// a slow shimmer that differs from place to place
float twinkle_at(double t, float a, float b) { return 0.6f + 0.4f * float(std::sin(t * 1.5 + a * 0.08 + b * 0.11)); }

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

float slab_lift(int sprite) {
    switch (sprite) {
    case 0x2f: return 0;
    case 0x2e: return 1;
    case 0x2d: return 2;
    case 0x2c: return 3;
    case 0x0e: return 4;
    case 0x30: return 5;
    default: return -1;
    }
}

BBRenderer::BBRenderer(const std::string& fonts_dir, const std::string& intro, const std::string& creatures) {
    if (FileExists(intro.c_str())) {
        intro_ = LoadTexture(intro.c_str());
        SetTextureFilter(intro_, TEXTURE_FILTER_BILINEAR);
    }
    if (FileExists(creatures.c_str())) {
        creatures_ = LoadTexture(creatures.c_str());
        GenTextureMipmaps(&creatures_);  // drawn small: mipmaps keep it smooth
        SetTextureFilter(creatures_, TEXTURE_FILTER_TRILINEAR);
    }
    std::string f = fonts_dir + "/Orbitron.ttf";
    if (FileExists(f.c_str())) {
        font_big_ = LoadFontEx(f.c_str(), 160, nullptr, 0);
        font_ui_ = LoadFontEx(f.c_str(), 56, nullptr, 0);
        SetTextureFilter(font_big_.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(font_ui_.texture, TEXTURE_FILTER_BILINEAR);
    } else
        font_big_ = font_ui_ = GetFontDefault();
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
    if (classic_sprites_.id) UnloadTexture(classic_sprites_);
    if (intro_.id) UnloadTexture(intro_);
    if (creatures_.id) UnloadTexture(creatures_);
    if (font_big_.texture.id != GetFontDefault().texture.id) UnloadFont(font_big_);
    if (font_ui_.texture.id != GetFontDefault().texture.id) UnloadFont(font_ui_);
}

void BBRenderer::load_sprites(const uint8_t* data) {
    sprite_data_.assign(data, data + SPRITES * SPRITE_BYTES);
    // Each sprite row: 3 bytes graphics (1 = ink, dark), 3 bytes mask (1 = the background shows).
    auto solid = [&](int n, int x, int y) {
        if (x < 0 || y < 0 || x >= SPRITE_W || y >= SPRITE_H) return false;
        return !(data[n * SPRITE_BYTES + y * 6 + 3 + x / 8] & (0x80 >> (x % 8)));
    };
    auto ink = [&](int n, int x, int y) { return solid(n, x, y) && (data[n * SPRITE_BYTES + y * 6 + x / 8] & (0x80 >> (x % 8))); };
    // the fill atlas (one row of 60 sprites): the solid parts as glass, the ink darker
    Image img = GenImageColor(SPRITES * SPRITE_W, SPRITE_H, BLANK);
    auto* px = static_cast<Color*>(img.data);
    for (int n = 0; n < SPRITES; n++)
        for (int y = 0; y < SPRITE_H; y++)
            for (int x = 0; x < SPRITE_W; x++)
                if (solid(n, x, y)) px[y * img.width + n * SPRITE_W + x] = ink(n, x, y) ? Color{40, 40, 40, 235} : Color{110, 110, 110, 220};
    if (sprites_.id) UnloadTexture(sprites_);
    sprites_ = LoadTextureFromImage(img);
    SetTextureFilter(sprites_, TEXTURE_FILTER_BILINEAR);
    for (int n = 0; n < SPRITES; n++)
        for (int y = 0; y < SPRITE_H; y++)
            for (int x = 0; x < SPRITE_W; x++)
                if (solid(n, x, y)) px[y * img.width + n * SPRITE_W + x] = ink(n, x, y) ? Color{50, 55, 65, 255} : WHITE;
    if (classic_sprites_.id) UnloadTexture(classic_sprites_);
    classic_sprites_ = LoadTextureFromImage(img);
    SetTextureFilter(classic_sprites_, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);

    vec_.assign(SPRITES, {});
    for (int n = 0; n < SPRITES; n++) {
        SpriteVec& v = vec_[size_t(n)];
        // The silhouette: the pixel edges between solid and not, chained into loops (solid on the
        // right); their midpoints make a staircase into a straight slope.
        struct Edge {
            int x0, y0, x1, y1;
            bool used = false;
        };
        std::vector<Edge> edges;
        for (int y = 0; y < SPRITE_H; y++)
            for (int x = 0; x < SPRITE_W; x++) {
                if (!solid(n, x, y)) continue;
                if (!solid(n, x, y - 1)) edges.push_back({x, y, x + 1, y});
                if (!solid(n, x + 1, y)) edges.push_back({x + 1, y, x + 1, y + 1});
                if (!solid(n, x, y + 1)) edges.push_back({x + 1, y + 1, x, y + 1});
                if (!solid(n, x - 1, y)) edges.push_back({x, y + 1, x, y});
            }
        for (size_t e0 = 0; e0 < edges.size(); e0++) {
            if (edges[e0].used) continue;
            std::vector<Vector2> loop;
            size_t e = e0;
            while (!edges[e].used) {
                Edge& ed = edges[e];
                ed.used = true;
                loop.push_back({(ed.x0 + ed.x1) * 0.5f, (ed.y0 + ed.y1) * 0.5f});
                // the next edge from where this one ends; at a corner touch, the right turn
                int dx = ed.x1 - ed.x0, dy = ed.y1 - ed.y0;
                size_t next = e, straight = SIZE_MAX;
                for (size_t k = 0; k < edges.size(); k++) {
                    const Edge& c = edges[k];
                    if (c.used || c.x0 != ed.x1 || c.y0 != ed.y1) continue;
                    int cx = c.x1 - c.x0, cy = c.y1 - c.y0;
                    if (dx * cy - dy * cx > 0) {  // a right turn (y down)
                        next = k;
                        break;
                    }
                    straight = k;
                }
                if (next == e) next = straight;
                if (next == SIZE_MAX) break;
                e = next;
            }
            if (loop.size() >= 4) v.outlines.push_back(smooth(loop, true, 0.5f, 1));
        }
        // ink strokes inside (eyes, panels): ink pixels with solid all round, walked into lines
        std::vector<int> seen(SPRITE_W * SPRITE_H, 0);
        auto inner = [&](int x, int y) {
            if (!ink(n, x, y)) return false;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++)
                    if (!solid(n, x + i, y + j)) return false;
            return true;
        };
        auto free_at = [&](int x, int y) { return inner(x, y) && !seen[y * SPRITE_W + x]; };
        auto degree = [&](int x, int y) {
            int d = 0;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++)
                    if ((i || j) && free_at(x + i, y + j)) d++;
            return d;
        };
        for (;;) {
            int sx = -1, sy = -1, best = 99;  // start at an end: the fewest free neighbours
            for (int y = 0; y < SPRITE_H; y++)
                for (int x = 0; x < SPRITE_W; x++)
                    if (free_at(x, y) && degree(x, y) < best) best = degree(x, y), sx = x, sy = y;
            if (sx < 0) break;
            std::vector<Vector2> path;
            int x = sx, y = sy, dx = 0, dy = 0;
            for (;;) {
                seen[y * SPRITE_W + x] = 1;
                path.push_back({x + 0.5f, y + 0.5f});
                int bx = 0, by = 0, score = -99;  // on: straight ahead first, then side steps, then diagonals
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++) {
                        if ((!i && !j) || !free_at(x + i, y + j)) continue;
                        int sc = (i == dx && j == dy ? 4 : 0) + (i == 0 || j == 0 ? 2 : 0);
                        if (sc > score) score = sc, bx = i, by = j;
                    }
                if (score == -99) break;
                x += bx, y += by, dx = bx, dy = by;
            }
            if (path.size() == 1) path.push_back({path[0].x + 0.01f, path[0].y});  // a dot
            v.strokes.push_back(smooth(path, false, 0.4f, 1));
        }
    }
}

void BBRenderer::room(int room, double t, bool glow) {
    const RoomPalette th = classic() ? classic_room(room) : theme_of(room);
    // the void around the room, with the ceiling's diagonal ribs
    if (!glow) {
        DrawRectangle(0, 0, W, FIELD_H, th.void_fill);
        // ceiling ribs over the left wall (the walls, drawn next, cover the rest)
        for (int i = -10; i < 60; i++) {
            float x0 = float(i) * 48;
            line({x0, 0}, {x0 - 520, 260}, 3, th.ribs);
        }
    }
    Vector2 f0 = P(0, 0, 0), f1 = P(FLOOR_X, 0, 0), f2 = P(FLOOR_X, 0, FLOOR_Z), f3 = P(0, 0, FLOOR_Z);
    Vector2 w1 = P(FLOOR_X, WALL_H, 0), w2 = P(FLOOR_X, WALL_H, FLOOR_Z), w3 = P(0, WALL_H, FLOOR_Z);
    float pulse = classic() ? 1.0f : 0.85f + 0.15f * float(std::sin(t * 2.0));
    Color edge_c = scale(th.edge, pulse);                                            // the room's edges
    // patterns, grid (on light stone, dimmed lines would read as dark: there they stay bright, gold)
    float grid_k = classic() || th.look == Look::Temple ? 1.0f : GRID;
    Color core = scale(th.core, pulse * grid_k), dim = scale(th.dim, pulse * grid_k);
    if (!glow) {
        quad_grad(f0, f1, f2, f3, th.floor, th.floor_far, th.floor_far, th.floor);           // floor
        quad_grad(f1, w1, w2, f2, th.back_low, th.back_high, th.back_high, th.back_low);     // back wall (X = 92)
        quad_grad(f2, w2, w3, f3, th.left_low, th.left_high, th.left_high, th.left_low);     // left wall (Z = 148)
        // the lip: front face (X = 0) and right face (Z = 0), below the floor
        Vector2 d{0, LIP * SCALE};
        quad(f3, f0, {f0.x + d.x, f0.y + d.y}, {f3.x + d.x, f3.y + d.y}, th.lip_front);
        quad(f0, f1, {f1.x, f1.y + d.y}, {f0.x, f0.y + d.y}, th.lip_right);
    }
    float thin = glow ? 3 : 1.5f, edge = glow ? 7 : 3;
    const RoomStyle& st = ROOM_STYLES[std::clamp(room, 0, 6)];
    // floor
    if (st.floor == Floor::Checker && !glow)
        for (int i = 0; i * CUBE < FLOOR_X; i++)
            for (int j = 0; j * CUBE < FLOOR_Z; j++)
                if ((i + j) % 2 == 0) {
                    float xa = i * CUBE, xb = std::min(FLOOR_X, xa + CUBE), za = j * CUBE, zb = std::min(FLOOR_Z, za + CUBE);
                    quad(P(xa, 0, za), P(xb, 0, za), P(xb, 0, zb), P(xa, 0, zb), Fade(th.floor_alt, 0.75f));
                }
    if (st.floor == Floor::Grid || st.floor == Floor::Checker) {
        for (float x = CUBE; x < FLOOR_X; x += CUBE) line(P(x, 0, 0), P(x, 0, FLOOR_Z), thin, dim);
        for (float z = CUBE; z < FLOOR_Z; z += CUBE) line(P(0, 0, z), P(FLOOR_X, 0, z), thin, dim);
    } else if (st.floor == Floor::Dots) {
        for (float x = CUBE / 2; x < FLOOR_X; x += CUBE)
            for (float z = CUBE / 2; z < FLOOR_Z; z += CUBE) DrawCircleV(P(x, 0, z), glow ? 7 : 3.5f, dim);
        if (th.look == Look::Circuit)  // circuit: traces between the nodes, a few nodes lit
            for (float x = CUBE / 2; x < FLOOR_X; x += CUBE)
                for (float z = CUBE / 2; z < FLOOR_Z; z += CUBE) {
                    int h = int(x * 7 + z * 13) % 5;
                    if (h == 0 && z + CUBE < FLOOR_Z) line(P(x, 0, z), P(x, 0, z + CUBE), thin, dim);
                    if (h == 1 && x + CUBE < FLOOR_X) line(P(x, 0, z), P(x + CUBE, 0, z), thin, dim);
                    if (h == 2) DrawCircleV(P(x, 0, z), glow ? 9 : 5, scale(th.accent, twinkle_at(t, x, z) * 0.8f));
                }
    } else if (!glow) {  // dark glass: a soft sheen across it
        for (int i = 0; i < 6; i++) {
            float x = FLOOR_X * (0.15f + 0.14f * i);
            line(P(x, 0, 0), P(x, 0, FLOOR_Z), 1, add(th.floor, th.dim, 0.12f));
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
                int zi = int(z / 6), yi = int(y / 6);
                if (th.look == Look::Temple && !glow && (zi % 4 == 1 && yi % 2 == 1))  // turquoise inlays
                    DrawRectangleRec({b.x, b.y, a.x - b.x, a.y - b.y}, Fade(th.accent, 0.45f + 0.2f * twinkle(z, y)));
                float k = th.look == Look::Temple ? 0.75f + 0.25f * twinkle(z, y) : 0.5f * twinkle(z, y);
                DrawRectangleLinesEx({b.x, b.y, a.x - b.x, a.y - b.y}, thin, scale(core, k));
            }
        break;
    case Back::Plain: break;
    }
    // the theme's own details
    switch (th.look) {
    case Look::Copper:  // rivets along the joints, a pipe along the top
        for (float y = 6; y < WALL_H; y += 12)
            for (float z = 6; z < FLOOR_Z; z += 24) DrawCircleV(P(FLOOR_X, y, z), glow ? 5 : 3, scale(core, 0.8f));
        if (!glow) line(P(FLOOR_X, WALL_H - 4, 0), P(FLOOR_X, WALL_H - 4, FLOOR_Z), 14, Color{120, 64, 30, 255});
        line(P(FLOOR_X, WALL_H - 4.6f, 0), P(FLOOR_X, WALL_H - 4.6f, FLOOR_Z), glow ? 4 : 2.5f, scale(th.accent, 0.7f));
        break;
    case Look::Circuit:  // traces across the back wall between its crosses: runs with a bend, pads
        for (int i = 0; (i + 1.5f) * CUBE < FLOOR_Z; i++)
            for (int j = 0; (j + 1.5f) * CUBE < WALL_H; j++) {
                int h = (i * 7 + j * 3 + i * j * 5) % 6;
                if (h > 2) continue;
                float z = (i + 0.5f) * CUBE, y = (j + 0.5f) * CUBE;
                int len = 1 + (i + j) % 3;
                float z2 = std::min(FLOOR_Z - CUBE / 2, z + len * CUBE), y2 = h == 0 ? y + CUBE : h == 1 ? y : y - CUBE;
                y2 = std::clamp(y2, CUBE / 2, WALL_H - CUBE / 2);
                float zb = z2 - CUBE / 2;  // the bend
                line(P(FLOOR_X, y, z), P(FLOOR_X, y, zb), thin, core);
                line(P(FLOOR_X, y, zb), P(FLOOR_X, y2, z2), thin, core);
                DrawCircleV(P(FLOOR_X, y, z), glow ? 6 : 3.5f, core);
                DrawCircleV(P(FLOOR_X, y2, z2), glow ? 8 : 4.5f, scale(th.accent, 0.7f * twinkle(z2, y2)));
            }
        break;
    case Look::Obsidian: {  // a red seam around the room at a third of the height, pulsing
        Color seam = scale(th.accent, 0.55f + 0.45f * pulse);
        line(P(FLOOR_X, WALL_H * 0.33f, 0), P(FLOOR_X, WALL_H * 0.33f, FLOOR_Z), edge * 0.6f, seam);
        line(P(FLOOR_X, WALL_H * 0.33f, FLOOR_Z), P(0, WALL_H * 0.33f, FLOOR_Z), edge * 0.6f, seam);
        break;
    }
    case Look::Ice:  // frost: pale streaks down the walls
        if (!glow)
            for (int i = 0; i < 9; i++) {
                float z = FLOOR_Z * (0.07f + 0.11f * i), y0 = WALL_H * (0.35f + 0.07f * float(i % 3));
                line(P(FLOOR_X, WALL_H, z), P(FLOOR_X, y0, z + 4), 2, Fade(WHITE, 0.10f));
            }
        break;
    default: break;
    }
    // left wall grid
    if (st.back != Back::Plain) {
        for (float x = CUBE; x < FLOOR_X; x += CUBE) line(P(x, 0, FLOOR_Z), P(x, WALL_H, FLOOR_Z), thin, scale(dim, 0.5f));
        for (float y = CUBE; y < WALL_H; y += CUBE) line(P(0, y, FLOOR_Z), P(FLOOR_X, y, FLOOR_Z), thin, scale(dim, 0.5f));
    }
    if (st.door) {  // a doorway in the left wall: X 12..68, Y 12..56
        Vector2 a = P(12, 12, FLOOR_Z), b = P(68, 12, FLOOR_Z), c = P(68, 56, FLOOR_Z), d = P(12, 56, FLOOR_Z);
        if (th.look == Look::Reactor) {  // lit from inside: amber, brightest at the bottom
            Color hot = th.accent, cool = scale(th.accent, 0.25f);
            if (glow) hot = scale(hot, 0.5f), cool = BLACK;
            quad_grad(a, b, c, d, hot, hot, cool, cool);
            if (!glow) {  // its light on the floor in front of it
                Vector2 fa = P(12, 0, FLOOR_Z), fb = P(68, 0, FLOOR_Z), fc = P(68, 0, FLOOR_Z - 40), fd = P(12, 0, FLOOR_Z - 40);
                quad_grad(fa, fb, fc, fd, Fade(th.accent, 0.30f), Fade(th.accent, 0.30f), Fade(th.accent, 0), Fade(th.accent, 0));
                for (float y = 18; y < 56; y += 6) line(P(12, y, FLOOR_Z), P(68, y, FLOOR_Z), 2, Fade(BLACK, 0.35f));
            }
        } else
            quad(a, b, c, d, glow ? BLACK : th.void_fill);
        line(a, b, edge, edge_c);
        line(b, c, edge, edge_c);
        line(c, d, edge, edge_c);
        line(d, a, edge, edge_c);
    }
    // the room's edges, bright
    line(f1, f2, edge, edge_c);
    line(f2, f3, edge, edge_c);
    line(f1, w1, edge, edge_c);
    line(f2, w2, edge, edge_c);
    line(w1, w2, edge, edge_c);
    line(w2, w3, edge, edge_c);
    line(f3, w3, edge, edge_c);
    // the lip: a glowing rail along the right edge, its front edge
    Color rail = scale(th.rail, pulse);
    line(f0, f1, edge * 1.4f, rail);
    line(f0, f3, edge, edge_c);
    line({f0.x, f0.y + LIP * SCALE}, {f1.x, f1.y + LIP * SCALE}, thin, dim);
    line({f0.x, f0.y + LIP * SCALE}, {f3.x, f3.y + LIP * SCALE}, thin, dim);
    // a light pulse running along the floor grid
    float run = float(std::fmod(t * 0.35, 1.0));
    float zr = FLOOR_Z * run;
    line(P(0, 0, zr), P(FLOOR_X, 0, zr), edge, scale(core, glow ? 0.7f : 0.4f));
}

Color BBRenderer::lit(Color base, float x, float y, float z) const {
    if (classic()) return base;
    Color out = base;
    auto add_light = [&](float lx, float ly, float lz, float radius, float intensity, Color c) {
        float d = std::sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly) + (z - lz) * (z - lz));
        if (d >= radius) return;
        float k = 1 - d / radius;
        out = add(out, c, intensity * k * k);
    };
    for (const Light& l : lights_) add_light(l.x, l.y, l.z, l.radius, l.intensity * (1 - l.life / l.max), l.c);
    if (ball_.have) add_light(ball_.x + 6, ball_.h + 6, ball_.z - 6, 26, 0.35f, Color{170, 120, 255, 255});
    return out;
}

void BBRenderer::brick(const BBObject& o, double, bool glow) {
    Kind k = kind_of(o.sprite);
    Color c = classic() && k.shape == Shape::Wire ? Color{238, 236, 208, 255} : k.colour;
    float x0 = float(o.x), y0 = float(o.y), z1 = float(o.z), z0 = z1 - CUBE, x1 = x0 + CUBE, y1 = y0 + CUBE;
    float xm = (x0 + x1) / 2, ym = (y0 + y1) / 2, zm = (z0 + z1) / 2;
    float w = glow ? 4 : 2.5f;
    // dark glass faces, a shade per side (top lightest), lit by the ball and explosions
    auto face = [&](float k9, float fx, float fy, float fz) {
        if (glow) return BLACK;
        if (classic()) k9 = std::max(1.0f, 1.0f + (k9 - 3.2f) * 0.12f);
        Color b{uint8_t(c.r / k9), uint8_t(c.g / k9), uint8_t(c.b / k9), 255};
        return lit(b, fx, fy, fz);
    };
    auto fill = [&](float k9) { return face(k9, xm, ym, zm); };
    auto edges = [&](std::initializer_list<std::pair<Vector2, Vector2>> es, Color col) {
        for (auto [p, q] : es) line(p, q, w, classic() ? scale(col, 0.42f) : col);
    };
    // a box between two corners, its three visible faces: front (X = a), right (Z = b), top (Y = top);
    // faces darker at the bottom (contact shadow), a bevel just inside the edges
    auto box = [&](float xa, float xb, float ya, float yb, float za, float zb, Color col, bool faces = true) {
        Vector2 a = P(xa, ya, zb), b = P(xa, ya, za), cc = P(xa, yb, za), d = P(xa, yb, zb);
        Vector2 e = P(xb, ya, za), f = P(xb, yb, za), g = P(xb, yb, zb);
        if (faces) {
            Color front = face(8, xa, (ya + yb) / 2, (za + zb) / 2), right = face(5.5f, (xa + xb) / 2, (ya + yb) / 2, za);
            Color top = face(3.2f, (xa + xb) / 2, yb, (za + zb) / 2);
            Color fb = scale(front, classic() ? 0.88f : 0.5f), rb = scale(right, classic() ? 0.88f : 0.5f);
            quad_grad(a, b, cc, d, fb, fb, front, front);
            quad_grad(b, e, f, cc, rb, rb, right, right);
            quad(d, cc, f, g, top);
            if (!glow) {
                float m = 1.4f;  // the bevel: light lines just inside each face's edges
                Color bev = Fade(scale(col, 1.2f), 0.35f);
                Vector2 fa = P(xa, ya + m, zb - m), fbv = P(xa, ya + m, za + m), fc = P(xa, yb - m, za + m), fd = P(xa, yb - m, zb - m);
                Vector2 ra = P(xa + m, ya + m, za), rbv = P(xb - m, ya + m, za), rc = P(xb - m, yb - m, za), rd = P(xa + m, yb - m, za);
                Vector2 ta = P(xa + m, yb, zb - m), tb = P(xa + m, yb, za + m), tc = P(xb - m, yb, za + m), td = P(xb - m, yb, zb - m);
                for (auto [p, q] : {std::pair{fa, fbv}, {fbv, fc}, {fc, fd}, {fd, fa}, {ra, rbv}, {rbv, rc}, {rc, rd}, {rd, ra},
                                    {ta, tb}, {tb, tc}, {tc, td}, {td, ta}})
                    line(p, q, 1.2f, bev);
                // a glassy reflection across the front face
                if (!classic()) line(P(xa, ya + (yb - ya) * 0.25f, zb - 1), P(xa, ya + (yb - ya) * 0.85f, za + (zb - za) * 0.35f), 2,
                     Fade(WHITE, 0.07f));
            }
        }
        edges({{a, b}, {b, cc}, {cc, d}, {d, a}, {b, e}, {e, f}, {f, cc}, {f, g}, {g, d}}, col);
    };
    switch (k.shape) {
    case Shape::Cube: box(x0, x1, y0, y1, z0, z1, c); break;
    case Shape::Armored: {
        box(x0, x1, y0, y1, z0, z1, c);
        // Steel bands and a bomb emblem on both visible sides. Use the same
        // projected face coordinates for full-size bricks and menu thumbnails.
        for (int side = 0; side < 2; ++side) {
            auto point = [&](float u, float v) {
                return side == 0 ? P(x0, y0 + v, z1 - u) : P(x0 + u, y0 + v, z0);
            };
            Color steel = classic() ? Color{223, 231, 237, 255} : Color{105, 130, 155, 255};
            Color ink = classic() ? Color{30, 40, 53, 255} : Color{255, 205, 95, 255};
            if (!glow) {
                for (float u : {0.8f, 9.5f}) {
                    quad(point(u, 0.7f), point(u + 1.7f, 0.7f), point(u + 1.7f, 11.3f), point(u, 11.3f), steel);
                    for (float v : {1.6f, 10.4f})
                        DrawCircleV(point(u + 0.85f, v), g_scale * 0.28f, Color{44, 57, 72, 255});
                }
            }
            if (o.sprite != 0x09) continue;
            // Round bomb with a short bent fuse: recognizable without colour.
            Vector2 centre = point(6, 5.6f);
            for (int i = 0; i < 24; ++i) {
                float a = float(i) * 2*PI/24, b = float(i+1) * 2*PI/24;
                Vector2 p = point(6 + 2.1f*std::cos(a), 5.6f + 2.1f*std::sin(a));
                Vector2 q = point(6 + 2.1f*std::cos(b), 5.6f + 2.1f*std::sin(b));
                if (!glow) tri(centre, p, q, ink);
            }
            line(point(6, 7.4f), point(6, 8.5f), g_scale * 0.6f, ink);
            line(point(6, 8.5f), point(7.4f, 9.2f), g_scale * 0.6f, ink);
            line(point(7.4f, 9.2f), point(8, 8.5f), g_scale * 0.45f, ink);
        }
        break;
    }
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
    case Shape::Slabs: {  // the upper plate bobs, as in the original
        float lift = o.lift >= 0 ? o.lift : std::max(slab_lift(o.sprite), 0.0f);
        box(x0, x1, y0, y0 + 4, z0, z1, c);
        box(x0, x1, y0 + 4 + lift, y0 + 8 + lift, z0, z1, c);
        break;
    }
    case Shape::Wire: {
        box(x0, x1, y0, y1, z0, z1, c, classic());
        if (classic() && !glow) {
            // A solid size-bonus block: arrows follow each face's projection,
            // so the symbol stays legible in play and in instruction thumbnails.
            const Color ink{42, 57, 72, 255};
            auto arrow = [&](auto at) {
                line(at(3.5f, 6), at(8.5f, 6), 0.45f * g_scale, ink);
                tri(at(2.3f, 6), at(4.5f, 8), at(4.5f, 4), ink);
                tri(at(9.7f, 6), at(7.5f, 4), at(7.5f, 8), ink);
            };
            arrow([&](float u, float v) { return P(x0, y0 + v, z1 - u); });
            arrow([&](float u, float v) { return P(x0 + u, y0 + v, z0); });
        }
        break;
    }
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
    case Shape::LifeBall: {
        // A stationary green bonus capsule occupies a brick-sized box. Its flat
        // ball-plus emblem cannot be mistaken for the moving silver sphere.
        box(x0, x1, y0, y1, z0, z1, c);
        if (glow) break;
        const Color emblem = classic() ? Color{22, 68, 47, 255} : Color{193, 255, 216, 255};
        for (int side=0; side<2; ++side) {
            auto point = [&](float u,float v) {
                return side == 0 ? P(x0,y0+v,z1-u) : P(x0+u,y0+v,z0);
            };
            for(int i=0;i<24;++i) {
                float a=i*2*PI/24, b=(i+1)*2*PI/24;
                tri(point(4.5f,6),point(4.5f+2.1f*std::cos(a),6+2.1f*std::sin(a)),
                    point(4.5f+2.1f*std::cos(b),6+2.1f*std::sin(b)),emblem);
            }
            line(point(7.6f,6),point(10,6),g_scale*0.55f,emblem);
            line(point(8.8f,4.8f),point(8.8f,7.2f),g_scale*0.55f,emblem);
        }
        break;
    }
    case Shape::Lying:
    case Shape::Barrel: {
        // round things: a glowing disc / tube, the front face is undistorted in this projection
        Vector2 fc = P(x0, ym, zm), bc = P(x1, ym, zm);
        float r = 5.5f * g_scale;
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
        }
        break;
    }
    }
    if (glow || o.slot < 3) return;
    const Color ink = classic() ? Color{24, 33, 48, 255} : Color{255, 240, 190, 255};
    auto decal = [&](float u, float v) {
        if (k.shape == Shape::Pyramid || k.shape == Shape::Diamond) {
            float h = k.shape == Shape::Diamond ? 6 + v*0.45f : v;
            float inset = k.shape == Shape::Diamond ? (h-6) : h/2;
            return P(xm+(u-6)*(1-inset/6), y0+h, z0+inset);
        }
        if (k.shape == Shape::Prism) return P(x0+u, y0+v, z0+v/2);
        if (k.shape == Shape::Ramp) return P(x0+v, y0+v, z1-u);
        return P(x0+u, y0+v, z0);
    };
    auto stroke = [&](float u, float v, float u2, float v2, float width=0.32f) {
        line(decal(u,v), decal(u2,v2), g_scale*width, ink);
    };
    auto circle = [&](float u, float v, float radius) {
        for(int i=0;i<24;++i) {
            float a=i*2*PI/24, b=(i+1)*2*PI/24;
            stroke(u+radius*std::cos(a),v+radius*std::sin(a),u+radius*std::cos(b),v+radius*std::sin(b));
        }
    };
    auto plus = [&](float u,float v) { stroke(u-0.75f,v,u+0.75f,v); stroke(u,v-0.75f,u,v+0.75f); };
    auto rocket = [&](float u,float v) {
        stroke(u-0.7f,v-1.2f,u-0.7f,v+1); stroke(u+0.7f,v-1.2f,u+0.7f,v+1);
        stroke(u-0.7f,v+1,u,v+2); stroke(u,v+2,u+0.7f,v+1);
        stroke(u-0.7f,v-0.4f,u-1.3f,v-1.5f); stroke(u+0.7f,v-0.4f,u+1.3f,v-1.5f);
        stroke(u,v-1.1f,u,v-2);
    };
    switch(o.sprite) {
    case 0x03:
        if (!classic()) {
            stroke(3,6,9,6); stroke(3,6,4.5f,7.5f); stroke(3,6,4.5f,4.5f);
            stroke(9,6,7.5f,7.5f); stroke(9,6,7.5f,4.5f);
        }
        break;
    case 0x21: // clock: slower ball
        circle(6,7,2.5f); stroke(6,7,6,8.8f); stroke(6,7,7.4f,6.3f); break;
    case 0x01: case 0x0d:
        rocket(5,7); plus(8.5f,7); break;
    case 0x08: case 0x27: // bonus points: star and plus
        for(int i=0;i<10;++i) {
            float a=PI/2+i*PI/5, b=a+PI/5;
            float ra=i%2 ? 1.05f : 2.4f, rb=i%2 ? 2.4f : 1.05f;
            stroke(5+ra*std::cos(a),7+ra*std::sin(a),5+rb*std::cos(b),7+rb*std::sin(b));
        }
        plus(9,7); break;
    case 0x20: // armour can be destroyed by either bomb or missile
        circle(4,6.7f,1.25f); stroke(4,8,4.7f,9); rocket(8,7); break;
    case 0x24: case 0x26: {
        // Directional arrows lie on the top, pointing towards the rear striking face.
        for(float x : {x0+3, x0+8}) {
            line(P(x,y1,z0+2),P(x,y1,z1-2),g_scale*0.4f,ink);
            line(P(x,y1,z1-2),P(x-1.3f,y1,z1-4),g_scale*0.4f,ink);
            line(P(x,y1,z1-2),P(x+1.3f,y1,z1-4),g_scale*0.4f,ink);
        }
        break;
    }
    case 0x3b: // door / next screen
        stroke(3,5,3,10); stroke(3,10,7,10); stroke(7,10,7,5);
        stroke(5,7.5f,9,7.5f); stroke(9,7.5f,7.8f,8.7f); stroke(9,7.5f,7.8f,6.3f); break;
    default: break;
    }

}

BBRenderer::BatShape BBRenderer::bat_shape(int sprite) const {
    return bb_paddle_shape(sprite < 0 || sprite >= SPRITES || sprite_data_.empty()
                           ? nullptr : sprite_data_.data() + sprite * SPRITE_BYTES);
}

BBRenderer::BatBody BBRenderer::bat_body(const BBObject& o) const {
    const BatShape shape = bat_shape(o.sprite);
    return bb_paddle_body(o.x, o.bat_width > 0 ? o.bat_width : shape.dx, o.z, shape);
}

void BBRenderer::bat_lane(const BBObject& o, double /*t*/, bool glow) {
    // A little light spills from the striking edge; quiet wall ticks show alignment.
    // Keep these cues out of the bloom pass so they stay below the ball and bat.
    if (glow) return;
    const BatBody b = bat_body(o);
    float x0 = b.x0, x1 = b.x1, z1 = b.z1;
    if (z1 >= FLOOR_Z) return;
    const Color light = classic() ? Color{255, 250, 225, 255} : Color{150, 230, 255, 255};
    float end = z1 + (FLOOR_Z - z1) / 3.0f;
    Color near = Fade(light, classic() ? 0.28f : 0.14f), far = Fade(light, 0.0f);
    quad_grad(P(x0, 0, z1), P(x1, 0, z1), P(x1, 0, end), P(x0, 0, end), near, near, far, far);
    // Static, separate marks at the wall's foot, with no connecting line.
    const Color tick = classic() ? Color{42, 57, 72, 180} : Fade(light, 0.32f);
    for (float x : {x0, x1})
        line(P(x, 0, FLOOR_Z), P(x, 3, FLOOR_Z), classic() ? 2.0f : 1.2f, tick);
}

void BBRenderer::bat(const BBObject& o, double t, bool glow) {
    // a dark metal block with bevelled edges, light slits, and a bright cold-blue striking edge
    // on the side that faces play (Z+), positioned from the original collision coordinates
    const BatBody b = bat_body(o);
    float x0 = b.x0, x1 = b.x1, y0 = 0, y1 = b.height;
    float z1 = b.z1, z0 = b.z0;
    const Color body = classic() ? Color{192, 201, 213, 255} : Color{34, 38, 54, 255};
    const Color side = classic() ? Color{135, 149, 170, 255} : Color{26, 30, 44, 255};
    const Color top = classic() ? Color{246, 247, 233, 255} : Color{58, 66, 88, 255};
    const Color steel = classic() ? Color{63, 77, 98, 255} : Color{120, 140, 175, 255};
    const Color ice{150, 230, 255, 255};
    float w = glow ? 4 : 2;
    if (!glow) {  // contact shadow
        quad(P(x0 - 1.5f, 0, z0 - 1.5f), P(x1 + 1.5f, 0, z0 - 1.5f), P(x1 + 1.5f, 0, z1 + 1.5f), P(x0 - 1.5f, 0, z1 + 1.5f),
             Fade(BLACK, 0.55f));
    }
    const float c = 1.2f;  // the chamfer on the top edges
    Vector2 fa = P(x0, y0, z1), fb = P(x0, y0, z0), fc = P(x0, y1 - c, z0), fd = P(x0, y1 - c, z1);  // front
    Vector2 ra = P(x1, y0, z0), rb = P(x1, y1 - c, z0);                                            // right
    Vector2 ta = P(x0 + c, y1, z1 - c), tb = P(x0 + c, y1, z0 + c), tc = P(x1, y1, z0 + c), td = P(x1, y1, z1 - c);  // top
    if (!glow) {
        quad_grad(fa, fb, fc, fd, scale(body, 0.6f), scale(body, 0.6f), body, body);
        quad_grad(fb, ra, rb, fc, scale(side, 0.6f), scale(side, 0.6f), side, side);
        quad(fd, fc, tb, ta, scale(top, 0.8f));  // the chamfers
        quad(fc, rb, tc, tb, scale(top, 0.7f));
        quad(ta, tb, tc, td, lit(top, (x0 + x1) / 2, y1, (z0 + z1) / 2));
    } else {
        quad(fa, fb, rb, fd, BLACK);
        quad(fd, fc, tc, td, BLACK);
    }
    for (auto [p, q] : {std::pair{fa, fb}, {fb, fc}, {fd, fa}, {fb, ra}, {ra, rb}, {rb, fc}, {fc, fd}, {fd, ta}, {fc, tb},
                        {ta, tb}, {tb, tc}, {rb, tc}})
        line(p, q, w * 0.7f, glow ? Fade(steel, 0.4f) : steel);
    // light slits along the outer side
    float pulse = 0.75f + 0.25f * float(std::sin(t * 3));
    for (float y : {y1 * 0.35f, y1 * 0.62f})
        line(P(x0 + 1.5f, y, z0), P(x1 - 1.5f, y, z0), glow ? 5 : 2, Fade(ice, 0.75f * pulse));
    // the striking edge: the top edge on the play side, bright (and drawn crisp again after the glow)
    line(ta, td, glow ? 10 : 4, ice);
    line(td, tc, glow ? 6 : 2.5f, Fade(ice, 0.7f));
    if (!glow) bat_edge_a_ = ta, bat_edge_b_ = td, bat_edge_ = true;
    // hits: a flash where the ball struck, a pulse running out to both ends along the edge
    for (const BatWave& wv : bat_waves_) {
        float a = 1 - wv.life / 0.35f, spread = 0.08f + wv.life * 3.2f;
        float s0 = std::clamp(wv.along - spread, 0.0f, 1.0f), s1 = std::clamp(wv.along + spread, 0.0f, 1.0f);
        Vector2 hit = P(x0 + c + (x1 - x0 - c) * wv.along, y1, z1 - c);
        line(P(x0 + c + (x1 - x0 - c) * s0, y1, z1 - c), P(x0 + c + (x1 - x0 - c) * s1, y1, z1 - c), glow ? 14 : 6,
             Fade(WHITE, a));
        DrawCircleV(hit, (glow ? 36 : 18) * a, Fade(Color{220, 245, 255, 255}, a));
        for (float y : {y1 * 0.35f, y1 * 0.62f})  // the slits light up as the pulse passes
            line(P(x0 + (x1 - x0) * s0, y, z0), P(x0 + (x1 - x0) * s1, y, z0), glow ? 7 : 3, Fade(WHITE, a * 0.8f));
    }
}

// The view the sphere's pattern is seen from: the game's camera, front right, above.
namespace {
struct V3 {
    float x, y, z;
};
V3 norm(V3 v) {
    float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / l, v.y / l, v.z / l};
}
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
const V3 VIEW_TO = norm({-1, 0.8f, -1});         // from the ball towards the viewer
const V3 VIEW_RIGHT = norm({1, 0, -1});          // screen right
const V3 VIEW_UP = norm(cross(VIEW_TO, VIEW_RIGHT));
V3 mul(const float* m, V3 v) {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z};
}
// m = rotation(axis, angle) * m, then keep it orthonormal
void rotate(float* m, V3 axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    float r[9] = {t * axis.x * axis.x + c,          t * axis.x * axis.y - s * axis.z, t * axis.x * axis.z + s * axis.y,
                  t * axis.x * axis.y + s * axis.z, t * axis.y * axis.y + c,          t * axis.y * axis.z - s * axis.x,
                  t * axis.x * axis.z - s * axis.y, t * axis.y * axis.z + s * axis.x, t * axis.z * axis.z + c};
    float o[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o[i * 3 + j] = r[i * 3] * m[j] + r[i * 3 + 1] * m[3 + j] + r[i * 3 + 2] * m[6 + j];
    V3 c0 = norm({o[0], o[3], o[6]}), c1{o[1], o[4], o[7]};
    float d = dot(c0, c1);
    c1 = norm({c1.x - d * c0.x, c1.y - d * c0.y, c1.z - d * c0.z});
    V3 c2 = cross(c0, c1);
    float n[9] = {c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z};
    std::copy(n, n + 9, m);
}
}  // namespace

void BBRenderer::ball(const BBObject& o, double t, bool glow) {
    Vector2 c{(o.sx + 12) * SCALE, (o.sy + 10) * SCALE};
    float r = BALL_RADIUS * SCALE;
    // the trail, short and narrowing, drawn behind the ball
    const float TRAIL = classic() ? 0.055f : 0.16f;
    for (size_t i = 1; i < ball_.trail.size(); i++) {
        float a0 = float((t - ball_.trail[i - 1].t) / TRAIL), a1 = float((t - ball_.trail[i].t) / TRAIL);
        if (a0 >= 1) continue;
        float wd = r * 1.4f * (1 - std::clamp((a0 + a1) / 2, 0.0f, 1.0f));
        Color tc = Fade(Color{200, 150, 255, 255}, (glow ? 0.5f : 0.3f) * (1 - std::clamp(a1, 0.0f, 1.0f)));
        line(ball_.trail[i - 1].p, ball_.trail[i].p, wd, tc);
        DrawCircleV(ball_.trail[i].p, wd / 2, tc);
    }
    // landing: squashed for a moment, sitting on its bottom
    float sq = float(std::exp(-(t - ball_.land_t) * 16.0)) * 0.22f;
    float rx = r * (1 + sq * 0.8f), ry = r * (1 - sq);
    c.y += r - ry;
    if (glow) {
        DrawEllipse(int(c.x), int(c.y), rx * 0.62f, ry * 0.62f, WHITE);
        DrawEllipse(int(c.x), int(c.y), rx, ry, Fade(Color{170, 110, 255, 255}, 0.6f));
    } else if (classic()) {
        DrawEllipse(int(c.x), int(c.y), rx, ry, Color{69, 79, 96, 255});
        for (int i = 0; i < 12; i++) {
            float a = float(i) / 11;
            float radius = 0.96f - a * 0.65f;
            Color shade{uint8_t(145 + 105*a), uint8_t(155 + 98*a), uint8_t(168 + 87*a), 255};
            DrawEllipse(int(c.x - rx*a*0.22f), int(c.y - ry*a*0.25f), rx*radius, ry*radius, shade);
        }
    } else {
        // dark glass shell, a white-hot core
        DrawEllipse(int(c.x), int(c.y), rx, ry, Color{26, 10, 52, 255});
        for (int i = 0; i < 5; i++) {
            float k = 0.85f - i * 0.15f;
            DrawEllipse(int(c.x), int(c.y), rx * k, ry * k, Fade(Color{235, 220, 255, 255}, 0.18f + i * 0.12f));
        }
    }
    // the pattern on the shell, turning as it rolls: three meridians, the equator, two parallels
    auto curve = [&](auto point) {
        V3 prev{};
        bool have = false;
        for (int i = 0; i <= 40; i++) {
            float th = float(i) / 40 * 6.2832f;
            V3 wv = mul(ball_.rot, point(th));
            float front = dot(wv, VIEW_TO);
            V3 cur{dot(wv, VIEW_RIGHT), dot(wv, VIEW_UP), front};
            if (have) {
                float f = (front + prev.z) / 2;
                float alpha = f > 0 ? 0.35f + 0.65f * f : 0.12f;  // the back half faint, through the glass
                Color lc = Fade(f > 0 ? Color{230, 200, 255, 255} : Color{150, 90, 255, 255}, alpha * (glow ? 0.7f : 1));
                if (classic()) lc = Fade(Color{65, 83, 109, 255}, f > 0 ? 0.65f : 0.0f);
                line({c.x + prev.x * rx * 0.97f, c.y - prev.y * ry * 0.97f}, {c.x + cur.x * rx * 0.97f, c.y - cur.y * ry * 0.97f},
                     glow ? 4 : (f > 0 ? 2.2f : 1.2f), lc);
            }
            prev = cur;
            have = true;
        }
    };
    for (float ph : {0.0f, 2.094f, 4.189f})
        curve([ph](float th) { return V3{std::cos(th) * std::cos(ph), std::sin(th), std::cos(th) * std::sin(ph)}; });
    for (float lat : {-0.7f, 0.0f, 0.7f})
        curve([lat](float th) { return V3{std::cos(lat) * std::cos(th), std::sin(lat), std::cos(lat) * std::sin(th)}; });
    // a thin coloured rim and a fixed highlight: the light stays, the pattern turns
    DrawRing(c, std::min(rx, ry) - 2, std::max(rx, ry) + (glow ? 3 : 0.5f), 0, 360, 48, classic() ? Color{63, 77, 98, 255} : Color{190, 120, 255, 255});
    if (!glow) DrawCircleV({c.x - rx * 0.38f, c.y - ry * 0.4f}, r * 0.16f, Fade(WHITE, 0.9f));
}

void BBRenderer::sprite(const BBObject& o, Color tint, double t, bool glow) {
    sprite_at(o.sprite, {o.sx * SCALE, o.sy * SCALE, SPRITE_W * SCALE, SPRITE_H * SCALE}, tint, t, glow);
}

void BBRenderer::sprite_at(int n, Rectangle dst, Color tint, double t, bool glow) {
    if (creature(n, dst, tint, t, glow)) return;
    if (classic() && classic_sprites_.id && n >= 0 && n < SPRITES) {
        if (!glow) DrawTexturePro(classic_sprites_, {float(n * SPRITE_W), 0, float(SPRITE_W), float(SPRITE_H)}, dst, {0, 0}, 0, tint);
        return;
    }
    // anything else: dark glass inside, the silhouette a crisp neon line, the ink strokes finer
    if (!sprites_.id || n < 0 || n >= SPRITES || size_t(n) >= vec_.size()) return;
    float k = dst.width / SPRITE_W;
    if (!glow)
        DrawTexturePro(sprites_, {float(n * SPRITE_W), 0, float(SPRITE_W), float(SPRITE_H)}, dst, {0, 0}, 0, scale(tint, 0.42f));
    auto at = [&](Vector2 p) { return Vector2{dst.x + p.x * k, dst.y + p.y * k}; };
    std::vector<Vector2> pts;
    for (const auto& ol : vec_[size_t(n)].outlines) {
        pts.clear();
        for (Vector2 p : ol) pts.push_back(at(p));
        pts.push_back(pts[0]);
        pts.push_back(pts[1]);
        DrawSplineLinear(pts.data(), int(pts.size()), glow ? k * 1.1f : std::max(2.0f, k * 0.42f), tint);
    }
    Color dimc = glow ? scale(tint, 0.6f) : add(scale(tint, 0.8f), WHITE, 0.1f);
    for (const auto& st : vec_[size_t(n)].strokes) {
        pts.clear();
        for (Vector2 p : st) pts.push_back(at(p));
        DrawSplineLinear(pts.data(), int(pts.size()), glow ? k * 0.6f : std::max(1.5f, k * 0.26f), dimc);
    }
}

bool BBRenderer::creature(int n, Rectangle dst, Color tint, double t, bool glow) {
    // The aliens, drawn as vectors in the sprite's pixels (24 x 20), where the sprite is: dark glass,
    // a faceted neon outline, details finer. 0x11-0x14: a dinosaur's head facing right, its jaw
    // opening and closing as the game's frames do; 0x15: a spinning top.
    if (n < 0x11 || n > 0x15) return false;
    const float k = dst.width / SPRITE_W;
    if (creatures_.id && !classic()) {  // the painted frames, their glow painted in (none added in the glow pass)
        if (!glow) {
            const float cw = creatures_.width / 5.0f, pad = CREATURE_PAD * k;
            DrawTexturePro(creatures_, {cw * float(n - 0x11), 0, cw, float(creatures_.height)},
                           {dst.x - pad, dst.y - pad, dst.width + 2 * pad, dst.height + 2 * pad}, {0, 0}, 0, WHITE);
        }
        return true;
    }
    auto at = [&](float x, float y) { return Vector2{dst.x + x * k, dst.y + y * k}; };
    const Color fill = glow ? BLANK : classic() ? scale(tint, 0.85f) : Color{uint8_t(tint.r * 0.10f), uint8_t(tint.g * 0.12f), uint8_t(tint.b * 0.12f), 235};
    if (classic()) tint = scale(tint, 0.30f);
    const float w = glow ? k * 1.0f : std::max(2.0f, k * 0.40f), wd = glow ? k * 0.6f : std::max(1.4f, k * 0.24f);
    const Color fine = glow ? scale(tint, 0.6f) : add(scale(tint, 0.75f), WHITE, 0.1f);
    std::vector<Vector2> pts;
    auto poly = [&](std::initializer_list<Vector2> ps, Vector2 centre, Color c, float lw) {  // closed: filled, outlined
        pts.clear();
        for (Vector2 p : ps) pts.push_back(p);
        if (fill.a)
            for (size_t i = 0; i < pts.size(); i++) tri(centre, pts[i], pts[(i + 1) % pts.size()], fill);
        pts.push_back(pts[0]);
        pts.push_back(pts[1]);
        DrawSplineLinear(pts.data(), int(pts.size()), lw, c);
    };
    auto stroke = [&](std::initializer_list<Vector2> ps, Color c, float lw) {
        pts.assign(ps.begin(), ps.end());
        DrawSplineLinear(pts.data(), int(pts.size()), lw, c);
    };
    if (n == 0x15) {
        const float cx = 12, spin = float(t * 4.0);
        poly({at(12, 17.6f), at(16, 18.3f), at(12, 19), at(8, 18.3f)}, at(12, 18.3f), scale(tint, 0.35f), wd);  // its shadow light
        poly({at(6.5f, 10), at(17.5f, 10), at(18, 13.5f), at(16, 16), at(8, 16), at(6, 13.5f)}, at(12, 13), tint, w);  // base
        poly({at(9, 7), at(15, 7), at(14.6f, 10), at(9.4f, 10)}, at(12, 8.5f), tint, w);                                // collar
        poly({at(12, 1.2f), at(17.5f, 7), at(6.5f, 7)}, at(12, 5), tint, w);                                          // cone
        stroke({at(6.4f, 12), at(17.6f, 12)}, fine, wd);
        stroke({at(7.2f, 14.4f), at(16.8f, 14.4f)}, fine, wd);
        for (int i = 0; i < 3; i++) {  // facets turning with it (the ones in front)
            float ph = spin + float(i) * 2.0944f;
            if (std::cos(ph) < 0) continue;
            float sx = std::sin(ph);
            stroke({at(12, 1.6f), at(cx + 5.2f * sx, 7)}, fine, wd);
            stroke({at(cx + 5.5f * sx, 10.2f), at(cx + 5.7f * sx, 13.4f), at(cx + 4.0f * sx, 15.8f)}, fine, wd);
        }
        if (!glow) DrawCircleV(at(12, 4.6f), k * 0.55f, add(tint, WHITE, 0.5f));  // a light in the cone
        return true;
    }
    // the head bobs and the jaw opens with the frames: 0x11 closed, 0x12 / 0x14 half, 0x13 wide
    const float dy = n == 0x11 ? 2.0f : n == 0x13 ? -2.0f : 0.0f;
    const float jaw = n == 0x11 ? 0.0f : n == 0x13 ? 0.85f : 0.45f;
    auto hd = [&](float x, float y) { return at(x, y + dy); };
    const float hx = 4, hy = 12.5f;  // the jaw's hinge
    auto jw = [&](float x, float y) {
        float ddx = x - hx, ddy = y - hy, c = std::cos(jaw), sn = std::sin(jaw);
        return hd(hx + ddx * c - ddy * sn, hy + ddx * sn + ddy * c);
    };
    // neck, down to the sprite's bottom
    poly({hd(1.5f, 10), hd(5.5f, 12.8f), at(6.2f, 20), at(2, 20)}, at(3.8f, 16), tint, w);
    stroke({at(2.2f, 15.5f + dy * 0.5f), at(5.9f, 16.2f + dy * 0.5f)}, fine, wd);
    // the mouth inside, lit, when open
    if (jaw > 0 && !glow) {
        Vector2 a = hd(hx, hy), b = hd(19.8f, 11.9f), c = jw(19.4f, 12.6f);
        tri(a, b, c, Fade(Color{255, 80, 140, 255}, 0.35f + 0.15f * float(std::sin(t * 9))));
    }
    // lower jaw, its teeth pointing up
    poly({jw(4, 12.5f), jw(19.6f, 12.5f), jw(18.4f, 13.8f), jw(8, 14.9f), jw(4.4f, 14.4f)}, jw(10, 13.6f), tint, w);
    for (float x = 8.4f; x < 18.5f; x += 1.7f) stroke({jw(x, 12.6f), jw(x + 0.45f, 11.7f), jw(x + 0.9f, 12.6f)}, fine, wd);
    // the skull
    poly({hd(2, 12.5f), hd(0.8f, 9.4f), hd(1.2f, 6), hd(3.4f, 3.6f), hd(9.6f, 2.6f), hd(12.2f, 3.2f), hd(14.2f, 0.8f),
          hd(14.8f, 3.9f), hd(17.6f, 5.4f), hd(20.4f, 7), hd(22, 8.6f), hd(21.8f, 10.8f), hd(19.8f, 12), hd(4, 12.5f)},
         hd(9, 7.5f), tint, w);
    // upper teeth, pointing down
    for (float x = 9.2f; x < 19.5f; x += 1.7f) stroke({hd(x, 12.2f), hd(x + 0.45f, 13.2f), hd(x + 0.9f, 12.2f)}, fine, wd);
    // the face, in straight facets: brow ridge to the horn, the cheek plate, the snout's ridge, nostril
    stroke({hd(9.2f, 4.2f), hd(13.4f, 4.6f), hd(15.6f, 6.8f)}, fine, wd);
    stroke({hd(1.8f, 8.6f), hd(7.6f, 8.6f), hd(9.6f, 11.8f)}, fine, wd);
    stroke({hd(15.6f, 6.8f), hd(20.8f, 9.6f)}, fine, wd);
    stroke({hd(20.2f, 8.2f), hd(21, 8.9f)}, fine, wd);
    // the eye: a diamond with a hot core
    Vector2 e = hd(12.3f, 6.2f);
    float ex = k * 1.3f, ey = k * 1.5f;
    stroke({{e.x, e.y - ey}, {e.x + ex, e.y}, {e.x, e.y + ey}, {e.x - ex, e.y}, {e.x, e.y - ey}, {e.x + ex, e.y}}, tint, wd * 1.2f);
    float blink = 0.75f + 0.25f * float(std::sin(t * 5 + dst.x * 0.01));
    DrawCircleV(e, k * (glow ? 0.9f : 0.55f), add(scale(tint, blink), WHITE, glow ? 0.2f : 0.6f));
    return true;
}

void BBRenderer::missile(const BBObject& o, double t, bool glow) {
    // a bolt of light flying along Z (to the left on screen, towards the bricks), its tail behind
    Vector2 c{(o.sx + 12) * SCALE, (o.sy + 10) * SCALE};
    const float len = 190, head = 40;
    float flick = 0.85f + 0.15f * float(std::sin(t * 60));
    Color core{255, 255, 255, 255}, hue{110, 230, 255, 255};
    for (int i = 0; i < 10; i++) {  // the tail: thinner and fainter towards the end
        float a0 = float(i) / 10, a1 = float(i + 1) / 10;
        line({c.x + head * 0.4f + a0 * len, c.y}, {c.x + head * 0.4f + a1 * len, c.y}, (glow ? 22 : 12) * (1 - a0),
             Fade(hue, (glow ? 0.8f : 0.6f) * (1 - a0) * flick));
    }
    DrawEllipse(int(c.x), int(c.y), head, head * 0.38f, Fade(hue, glow ? 1 : 0.9f));
    DrawEllipse(int(c.x), int(c.y), head * 0.65f, head * 0.22f, glow ? Fade(core, 0.9f) : core);
    if (!glow) DrawCircleV({c.x - head * 0.55f, c.y}, 5, core);  // the tip
}

void BBRenderer::burst(const BBObject& o, int points) {
    Kind k = kind_of(o.sprite);
    bool bomb = k.shape == Shape::Barrel;
    float cx = o.x + CUBE / 2, cy = o.y + CUBE / 2, cz = o.z - CUBE / 2;
    Vector2 c = P(cx, cy, cz);
    std::uniform_real_distribution<float> u(0, 1);
    int n = 8 + int(u(rng_) * 9);  // 8-16 angular shards
    for (int i = 0; i < n; i++) {
        float a = -3.1416f * (0.05f + 0.9f * u(rng_)) + (u(rng_) < 0.25f ? 3.1416f : 0), sp = 250 + u(rng_) * 500;
        shards_.push_back({c, {std::cos(a) * sp, std::sin(a) * sp}, u(rng_) * 6.28f, (u(rng_) - 0.5f) * 16, 10 + u(rng_) * 18, 0,
                           0.55f + u(rng_) * 0.35f, k.colour});
    }
    for (int i = 0; i < 7; i++) {  // a few bright sparks
        float a = u(rng_) * 6.2832f, sp = 700 + u(rng_) * 600;
        sparks_.push_back({c, {std::cos(a) * sp, std::sin(a) * sp}, 0, 0.18f + u(rng_) * 0.15f, i < 3 ? WHITE : k.colour});
    }
    flashes_.push_back({c, bomb ? 260.f : 150.f, 0, 0.16f, k.colour});
    lights_.push_back({cx, cy, cz, bomb ? 55.f : 30.f, bomb ? 2.4f : 1.5f, 0, 0.35f, k.colour});
    if (bomb) rings_.push_back({cx, cz, 48, 0, 0.7f, Color{255, 90, 90, 255}});
    if (points > 0) popups_.push_back({{c.x, c.y - 40}, 0, points});
}

void BBRenderer::floor_marks(double t, bool glow) {
    if (classic()) {
        if (ball_.have && ball_.h > -2 && !glow) {
            float h = std::max(0.0f, ball_.h);
            float spread = 1 + h / 65;
            DrawEllipse(int(ball_.floor.x), int(ball_.floor.y), 6*SCALE*spread, 2.5f*SCALE*spread,
                        Fade(BLACK, 0.30f * std::clamp(1-h/90, 0.3f, 1.0f)));
        }
        return;
    }
    // where the ball is over the floor (in this view its height is hard to judge): a soft shadow
    // that spreads and fades as it rises, a faint glow, and a crisp ring right under it that keeps
    // its size and brightness at any height. Only where it is now: no prediction.
    if (ball_.have && ball_.h > -2) {
        Vector2 s = ball_.floor;
        float h = std::max(0.0f, ball_.h), r = 6 * SCALE;
        float k = std::clamp(1 - h / 60, 0.25f, 1.0f);  // 1 on the floor, less higher up
        if (!glow) {
            float spread = 1 + h / 50;                    // softer: wider and lighter
            for (int i = 0; i < 3; i++)
                DrawEllipse(int(s.x), int(s.y), r * (1.3f - i * 0.25f) * spread, r * (0.52f - i * 0.1f) * spread,
                            Fade(BLACK, 0.22f * k));
        }
        for (int i = 0; i < 3; i++)
            DrawEllipse(int(s.x), int(s.y), r * (2.2f - i * 0.5f), r * (0.9f - i * 0.2f),
                        Fade(Color{160, 100, 255, 255}, (glow ? 0.1f : 0.05f) * k));
        Color ring{235, 210, 255, 255};
        DrawEllipseLines(int(s.x), int(s.y), r * 0.95f, r * 0.42f, Fade(ring, glow ? 0.9f : 0.95f));
        DrawEllipseLines(int(s.x), int(s.y), r * 0.95f + 1.5f, r * 0.42f + 1, Fade(ring, glow ? 0.9f : 0.6f));
        if (glow) DrawEllipseLines(int(s.x), int(s.y), r * 0.95f + 3, r * 0.42f + 2, Fade(ring, 0.6f));
    }
    // its light trail on the floor
    const float FLOOR_TRAIL = 1.0f;
    for (size_t i = 1; i < ball_.floor_trail.size(); i++) {
        float a = 1 - float((t - ball_.floor_trail[i].t) / FLOOR_TRAIL);
        if (a <= 0) continue;
        line(ball_.floor_trail[i - 1].p, ball_.floor_trail[i].p, glow ? 7 : 3, Fade(Color{200, 140, 255, 255}, a * (glow ? 0.8f : 0.6f)));
    }
    // rings spreading over the floor (landings, bombs)
    for (const FloorRing& rg : rings_) {
        float a = 1 - rg.life / rg.max, rad = rg.radius * (1 - a * a * 0.0f) * (1 - a + 0.15f);
        Vector2 prev{};
        for (int i = 0; i <= 48; i++) {
            float th = float(i) / 48 * 6.2832f;
            float x = std::clamp(rg.x + std::cos(th) * rad, 0.0f, FLOOR_X), z = std::clamp(rg.z + std::sin(th) * rad, 0.0f, FLOOR_Z);
            Vector2 p = P(x, 0, z);
            if (i) line(prev, p, glow ? 9 : 3.5f, Fade(rg.c, a));
            prev = p;
        }
    }
}

void BBRenderer::effects(double, bool glow) {
    for (const Shard& sh : shards_) {
        float a = 1 - sh.life / sh.max, s = sh.size * (0.6f + 0.4f * a);
        Vector2 p[3];
        for (int i = 0; i < 3; i++) {
            float ang = sh.ang + i * 2.2f + (i == 2 ? 0.5f : 0);
            float rr = s * (i == 1 ? 0.6f : 1.0f);
            p[i] = {sh.p.x + std::cos(ang) * rr, sh.p.y + std::sin(ang) * rr};
        }
        if (!glow) tri(p[0], p[1], p[2], Fade(scale(sh.c, classic() ? 0.85f : 0.25f), a));
        for (int i = 0; i < 3; i++) line(p[i], p[(i + 1) % 3], glow ? 3.5f : 2, Fade(sh.c, a));
    }
    for (const Spark& sp : sparks_) {
        float a = 1 - sp.life / sp.max;
        line(sp.p, {sp.p.x - sp.v.x * 0.03f, sp.p.y - sp.v.y * 0.03f}, glow ? 5 : 2.5f, Fade(sp.c, a));
    }
    for (const Flash& f : flashes_) {
        float a = 1 - f.life / f.max;
        DrawCircleGradient(f.p, f.radius * (0.6f + 0.4f * (1 - a)), Fade(WHITE, a * (glow ? 1 : 0.8f)),
                           Fade(f.c, 0));
    }
    if (!glow)
        for (const Popup& u : popups_) {
            float a = 1 - u.life / 1.1f;
            std::string txt = "+" + std::to_string(u.points);
            int fs = 56;
            DrawText(txt.c_str(), int(u.p.x - MeasureText(txt.c_str(), fs) / 2), int(u.p.y - u.life * 120), fs, Fade(WHITE, a));
        }
}

std::vector<const BBObject*> BBRenderer::scene_order(const BBState& s) const {
    // painter's order, as the game sorts: far (large X) first, then left (large Z), low first
    std::vector<const BBObject*> order;
    for (const auto& o : s.objects) order.push_back(&o);
    std::stable_sort(order.begin(), order.end(), [](const BBObject* a, const BBObject* b) {
        if (a->x != b->x) return a->x > b->x;
        if (a->z != b->z) return a->z > b->z;
        return a->y < b->y;
    });
    auto ball_it = std::find_if(order.begin(), order.end(), [](auto o) { return o->slot == 0; });
    auto bat_it = std::find_if(order.begin(), order.end(), [](auto o) { return o->slot == 1; });
    if (ball_it != order.end() && bat_it != order.end()) {
        const BBObject* sphere = *ball_it;
        const BBObject* paddle = *bat_it;
        const auto body = bat_body(*paddle);
        // Ball object coordinates are sprite anchors; the paddle uses physical bounds.
        // Recover the rendered centre, including the bounce height encoded in sy.
        const float x = sphere->x + 5;
        const float z = 160 + x - (sphere->sx + 12);
        const float y = 118 - x / 2 - (sphere->sy + 10);
        // Keep the sphere behind the solid paddle until it clears a visible face.
        // Centre-only ordering lets a still-overlapping sphere pop over the end.
        const bool in_front = x + BALL_RADIUS <= body.x0 ||
                              z + BALL_RADIUS <= body.z0 ||
                              y - BALL_RADIUS >= body.height;
        if ((ball_it > bat_it) != in_front) {
            // Correct only this pair, outside the comparator: a pair-specific
            // comparison inside stable_sort would not be a strict weak ordering.
            order.erase(ball_it);
            bat_it = std::find(order.begin(), order.end(), paddle);
            order.insert(in_front ? bat_it + 1 : bat_it, sphere);
        }
    }
    return order;
}

void BBRenderer::scene(const BBState& s, double t, bool glow) {
    using Sc = BBMenu::Screen;
    if (s.menu.screen != Sc::None && s.menu.screen != Sc::LevelCard) {
        menu_scene(s, t, glow);
        return;
    }
    if (s.menu.screen == Sc::LevelCard && s.menu.leave < 1) {  // the main menu still going
        BBState m;
        m.menu = s.menu;
        m.menu.screen = Sc::Main;
        menu_scene(m, t, glow);
        return;
    }
    // the level card: the room (and all on it) comes in from the left, as the original's
    const bool sliding = s.menu.screen == Sc::LevelCard && s.menu.slide < 1;
    if (sliding) {
        rlDrawRenderBatchActive();
        rlPushMatrix();
        rlTranslatef(-(1 - s.menu.slide) * float(W), 0, 0);
    }
    room(s.room, t, glow);
    // contact shadows on the floor under the bricks standing on it
    if (!glow)
        for (const auto& o : s.objects)
            if (o.slot >= 3 && o.y < 1) {
                float x0 = o.x - 1.5f, x1 = o.x + CUBE + 1.5f, z0 = o.z - CUBE - 1.5f, z1 = o.z + 1.5f;
                quad(P(x0, 0, z0), P(x1, 0, z0), P(x1, 0, z1), P(x0, 0, z1), Fade(BLACK, 0.45f));
            }
    floor_marks(t, glow);
    for (const auto& o : s.objects)
        if (o.slot == 1) bat_lane(o, t, glow);
    const auto order = scene_order(s);
    for (const BBObject* o : order) {
        if (o->slot == 0) ball(*o, t, glow);
        else if (o->slot >= 3 && o->sprite >= 0x19 && o->sprite <= 0x1f) continue;  // exploding: the burst shows it
        else if (o->slot >= 3 && !(o->sprite >= 0x11 && o->sprite <= 0x18)) brick(*o, t, glow);
        else if (o->slot == 1) bat(*o, t, glow);
        else if (o->slot == 2 || (o->sprite >= 0x16 && o->sprite <= 0x18)) missile(*o, t, glow);
        else if (o->sprite >= 0x11 && o->sprite <= 0x15)  // aliens: their own frames, in green neon
            sprite(*o, glow ? Color{110, 255, 160, 255} : Color{140, 255, 180, 255}, t, glow);
        else
            sprite(*o, glow ? Color{220, 160, 255, 255} : Color{210, 170, 255, 255}, t, glow);
    }
    effects(t, glow);
    if (sliding) {
        rlDrawRenderBatchActive();
        rlPopMatrix();
    }
    if (s.menu.screen == Sc::LevelCard) level_card(s, t, glow);
}

void BBRenderer::update_ball(const BBObject* b, double t, float dt) {
    if (!b) {
        ball_.have = false;
        ball_.trail.clear();
        ball_.floor_trail.clear();
        return;
    }
    float h = (100 - b->x / 2.0f) - b->sy;
    Vector2 c{(b->sx + 12) * SCALE, (b->sy + 10) * SCALE};
    // under it on the floor: the screen x of its centre, the y it would have lying there (+ radius)
    Vector2 floor_p{c.x, ((100 - b->x / 2.0f) + 15) * SCALE};
    if (ball_.have && dt > 0) {
        float dx = b->x - ball_.x, dz = b->z - ball_.z, dist = std::sqrt(dx * dx + dz * dz);
        bool teleport = dist > 20;
        if (!teleport) {
            // rolling: turn by the distance travelled about the axis across the motion
            if (dist > 0.01f) {
                V3 axis = norm({dz, 0, -dx});
                rotate(ball_.rot, axis, dist / 6);
                ball_.wx = axis.x * dist / 6 / dt, ball_.wy = 0, ball_.wz = axis.z * dist / 6 / dt;
            } else {  // standing still (or straight up and down): the spin it had, fading
                float wl = std::sqrt(ball_.wx * ball_.wx + ball_.wz * ball_.wz);
                if (wl > 0.05f) rotate(ball_.rot, {ball_.wx / wl, 0, ball_.wz / wl}, wl * dt);
                ball_.wx *= std::exp(-1.5f * dt), ball_.wz *= std::exp(-1.5f * dt);
            }
            // ground velocity, smoothed; a sharp turn is a bounce off something
            float nvx = dx / dt, nvz = dz / dt;
            float pv = std::sqrt(ball_.vx * ball_.vx + ball_.vz * ball_.vz), nv = std::sqrt(nvx * nvx + nvz * nvz);
            if (pv > 10 && nv > 10 && (ball_.vx * nvx + ball_.vz * nvz) / (pv * nv) < 0.2f && t - ball_.bounce_t > 0.15) {
                ball_.bounce_t = t;
                Vector2 dir{nvx - nvz, -nvx / 2};  // the new direction on screen
                float dl = std::max(1.0f, std::sqrt(dir.x * dir.x + dir.y * dir.y));
                dir = {dir.x / dl, dir.y / dl};
                std::uniform_real_distribution<float> u(-1, 1);
                for (int i = 0; i < 7; i++) {
                    float sp = 500 + 300 * u(rng_), sx = dir.x + u(rng_) * 0.6f, sy = dir.y + u(rng_) * 0.6f;
                    sparks_.push_back({c, {sx * sp, sy * sp}, 0, 0.16f + 0.06f * u(rng_), i < 3 ? WHITE : Color{200, 150, 255, 255}});
                }
                flashes_.push_back({c, 70, 0, 0.1f, Color{220, 180, 255, 255}});
            }
            ball_.vx += (nvx - ball_.vx) * 0.4f, ball_.vz += (nvz - ball_.vz) * 0.4f;
            // landing: squash and a ring on the floor
            if (ball_.h > 3 && h <= 1.2f && t - ball_.land_t > 0.2) {
                ball_.land_t = t;
                rings_.push_back({b->x + 6, b->z - 6, 9, 0, 0.35f, Color{200, 150, 255, 255}});
            }
        }
        // trails: drop on a jump (respawn), then add the point; they fade by age, not by count
        if (!ball_.trail.empty() && std::hypot(c.x - ball_.trail.back().p.x, c.y - ball_.trail.back().p.y) > 160)
            ball_.trail.clear();
        if (ball_.trail.empty() || std::hypot(c.x - ball_.trail.back().p.x, c.y - ball_.trail.back().p.y) > 2)
            ball_.trail.push_back({c, t});
        if (h < 3) {
            if (!ball_.floor_trail.empty() &&
                std::hypot(floor_p.x - ball_.floor_trail.back().p.x, floor_p.y - ball_.floor_trail.back().p.y) > 160)
                ball_.floor_trail.clear();
            if (ball_.floor_trail.empty() ||
                std::hypot(floor_p.x - ball_.floor_trail.back().p.x, floor_p.y - ball_.floor_trail.back().p.y) > 3)
                ball_.floor_trail.push_back({floor_p, t});
        }
    }
    while (!ball_.trail.empty() && t - ball_.trail.front().t > 0.16) ball_.trail.pop_front();
    while (!ball_.floor_trail.empty() && t - ball_.floor_trail.front().t > 1.0) ball_.floor_trail.pop_front();
    ball_.have = true;
    ball_.x = b->x, ball_.z = b->z, ball_.h = h;
    ball_.floor = floor_p;
}

void BBRenderer::prepare(const BBState& s, double t) {
    if (s.level != room_level_) room_level_ = s.level, FLOOR_X = ROOM_X, FLOOR_Z = ROOM_Z;
    for (const auto& o : s.objects)
        if (o.slot >= 3 && !(o.sprite >= 0x11 && o.sprite <= 0x1f)) {  // bricks (not aliens, explosions)
            FLOOR_X = std::max(FLOOR_X, std::round(o.x) + CUBE);
            FLOOR_Z = std::max(FLOOR_Z, std::round(o.z));
        }
    if (const char* path = std::getenv("BB_SPRITE_SHEET"); path && has_sprites()) {  // debug: the aliens' frames, once
        static bool done = false;
        if (!done) {
            done = true;
            const int n0 = 0x11, count = 5, cw = 24 * 12, ch = 20 * 12;
            RenderTexture2D rt = LoadRenderTexture(cw * count, ch * 2);
            BeginTextureMode(rt);
            ClearBackground(Color{12, 8, 20, 255});
            for (int glow_pass = 0; glow_pass < 2; glow_pass++)
                for (int i = 0; i < count; i++)
                    sprite_at(n0 + i, {float(i * cw), float(glow_pass * ch), float(cw), float(ch)}, Color{140, 255, 180, 255}, 0.3,
                              glow_pass == 1);
            EndTextureMode();
            Image img = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&img);
            ExportImage(img, path);
            UnloadImage(img);
            UnloadRenderTexture(rt);
        }
    }
    const float elapsed = last_t_ < 0 ? 0 : float(std::max(0.0, t - last_t_));
    float dt = std::min(elapsed, 0.1f);
    last_t_ = t;
    for (Shard& sh : shards_) {
        sh.life += dt;
        sh.p.x += sh.v.x * dt, sh.p.y += sh.v.y * dt;
        sh.v.y += 1400 * dt;
        sh.ang += sh.spin * dt;
    }
    for (Spark& sp : sparks_) sp.life += dt, sp.p.x += sp.v.x * dt, sp.p.y += sp.v.y * dt, sp.v.x *= 1 - 3 * dt, sp.v.y *= 1 - 3 * dt;
    for (Flash& f : flashes_) f.life += dt;
    for (FloorRing& rg : rings_) rg.life += dt;
    for (Light& l : lights_) l.life += dt;
    for (Popup& u : popups_) u.life += dt;
    for (BatWave& wv : bat_waves_) wv.life += elapsed;  // expire even while the original view was shown
    std::erase_if(shards_, [](const Shard& x) { return x.life >= x.max; });
    std::erase_if(sparks_, [](const Spark& x) { return x.life >= x.max; });
    std::erase_if(flashes_, [](const Flash& x) { return x.life >= x.max; });
    std::erase_if(rings_, [](const FloorRing& x) { return x.life >= x.max; });
    std::erase_if(lights_, [](const Light& x) { return x.life >= x.max; });
    std::erase_if(popups_, [](const Popup& x) { return x.life >= 1.1f; });
    std::erase_if(bat_waves_, [](const BatWave& x) { return x.life >= 0.35f; });
    const BBObject* b = nullptr;
    for (const auto& o : s.objects) if (o.slot == 0) b = &o;
    const auto& hit = s.paddle_hit;
    if (!hit.id) bat_waves_.clear();  // level / life transition
    if (hit.id && hit.id != shown_paddle_hit_ && t + 1e-8 >= hit.t + s.render_delay) {
        shown_paddle_hit_ = hit.id;
        float age = float(std::max(0.0, t - hit.t - s.render_delay));
        // Don't replay an old collision after returning from the original view.
        if (b && age < 0.35f) bat_waves_.push_back({hit.along, age});
    }
    // Confirmed paddle contacts have their own effect; the velocity heuristic is
    // only for other bounces, and must not add a second flash as the ball leaves.
    if (hit.id && t >= hit.t && t < hit.t + s.render_delay + 0.15) ball_.bounce_t = t;
    update_ball(b, t, dt);

    if (classic()) return;  // solid materials need no bloom pass

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
    const RoomPalette th = classic() ? classic_room(s.room) : theme_of(s.room);
    const Color label = classic() ? Color{25, 45, 60, 255} : RAYWHITE;
    DrawRectangle(0, FIELD_H, W, H - FIELD_H, classic() ? Color{91, 211, 224, 255} : add(th.void_fill, th.dim, 0.05f));
    DrawLineEx({0, float(FIELD_H) + 2}, {float(W), float(FIELD_H) + 2}, 3, th.edge);
    char buf[64];
    std::snprintf(buf, sizeof buf, "SCORE  %07d", s.score);
    DrawText(buf, 60, FIELD_H + 36, 52, label);
    std::snprintf(buf, sizeof buf, "SCREEN  %d", s.level + 1);
    DrawText(buf, 640, FIELD_H + 36, 52, classic() ? label : add(scale(th.edge, 0.8f), WHITE, 0.15f));
    DrawText("LIVES", 1020, FIELD_H + 36, 52, label);
    for (int i = 0; i < std::min(s.lives, 9); i++) DrawCircle(1220 + i * 56, FIELD_H + 62, 18, th.rail);
    if (s.unlimited_lives) DrawText("UNLIMITED", 1220, FIELD_H + 88, 22, Color{200, 160, 255, 255});
    if (s.unlimited_missiles) DrawText("UNLIMITED", 1500, FIELD_H + 88, 22, Color{200, 160, 255, 255});
    std::snprintf(buf, sizeof buf, "MISSILES  %02d", s.missiles);
    DrawText(buf, 1500, FIELD_H + 36, 52, label);
}

void BBRenderer::draw(const BBState& s, double t) {
    bat_edge_ = false;
    scene(s, t, false);
    if (!classic()) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(half_a_.texture, {0, 0, float(W / 4), -float(H / 4)}, {0, 0, float(W), float(H)}, {0, 0}, 0, WHITE);
        EndBlendMode();
    }
    if (bat_edge_) {  // crisp: the edge you aim with (coming in with the room on the level card)
        float dx = s.menu.screen == BBMenu::Screen::LevelCard ? -(1 - s.menu.slide) * float(W) : 0;
        line({bat_edge_a_.x + dx, bat_edge_a_.y}, {bat_edge_b_.x + dx, bat_edge_b_.y}, 2.5f, Color{235, 250, 255, 255});
    }
    if (s.menu.screen == BBMenu::Screen::None || (s.menu.screen == BBMenu::Screen::LevelCard && s.menu.leave >= 1)) status_bar(s);
}

// ---- the menus ------------------------------------------------------------------------------
// The original runs them (its cursor, its screens, its hi-score table and instructions in RAM);
// they are drawn here over a dimmed room.

namespace {
const char* MENU_ITEMS[6] = {"PLAY GAME", "CONTROLS", "KEYS", "HI-SCORES", "INSTRUCTIONS", "SOUND FX"};
constexpr Color MENU_GOLD{255, 214, 120, 255}, MENU_LILAC{205, 170, 255, 255}, MENU_DIM{150, 120, 200, 255};
}  // namespace

void BBRenderer::text(const char* s, Vector2 centre, float size, Color c, bool big) const {
    const Font& f = big ? font_big_ : font_ui_;
    Vector2 m = MeasureTextEx(f, s, size, size * 0.06f);
    DrawTextEx(f, s, {centre.x - m.x / 2, centre.y - m.y / 2}, size, size * 0.06f, c);
}

void BBRenderer::text_left(const char* s, Vector2 at, float size, Color c, bool big) const {
    DrawTextEx(big ? font_big_ : font_ui_, s, at, size, size * 0.06f, c);
}

void BBRenderer::thumbnail(int sprite, Vector2 centre, float scale, double t, bool glow) {
    float save = g_scale;
    Vector2 save_off = g_off;
    g_scale = scale;
    // a cube at (0, 0, 12) has its middle at (160, 109) * scale: put that on centre, bobbing
    float bob = float(std::sin(t * 2 + sprite)) * scale * 0.8f;
    g_off = {centre.x - 160 * scale, centre.y - 109 * scale + bob};
    if (sprite >= 0x11 && sprite <= 0x18) {  // aliens (and the like): their sprite, in neon
        sprite_at(sprite, {centre.x - SPRITE_W * scale / 2, centre.y - SPRITE_H * scale / 2 + bob, SPRITE_W * scale, SPRITE_H * scale},
                  Color{140, 255, 180, 255}, t, glow);
    } else {
        BBObject o{0, 0, sprite, 0, 0, 12, 3};
        brick(o, t, glow);
    }
    g_scale = save;
    g_off = save_off;
}

void BBRenderer::level_card(const BBState& s, double t, bool glow) {
    // (the card stays over the room: the remake brings the room in during it)
    // "SCREEN n" over the room as it comes in
    if (!glow) DrawRectangle(0, 300, W, 260, Fade(BLACK, 0.45f));
    char buf[32];
    std::snprintf(buf, sizeof buf, "SCREEN %d", s.level + 1);
    float pulse = 0.85f + 0.15f * float(std::sin(t * 4));
    if (!s.menu.level_name.empty()) {  // Ballbreaker II: the level's name, its number, its designer
        float size = 110;
        while (size > 50 && MeasureTextEx(font_big_, s.menu.level_name.c_str(), size, size * 0.06f).x > W - 160) size -= 6;
        text(s.menu.level_name.c_str(), {W / 2.0f, 395}, size, scale(Color{230, 170, 255, 255}, pulse), true);
        text(buf, {W / 2.0f, 478}, 40, Fade(MENU_LILAC, 0.85f));
        if (!s.menu.designer.empty()) text(("DESIGNED BY " + s.menu.designer).c_str(), {W / 2.0f, 528}, 32, Fade(MENU_LILAC, 0.7f));
        return;
    }
    text(buf, {W / 2.0f, 400}, 140, scale(Color{230, 170, 255, 255}, pulse), true);
    text("GET READY", {W / 2.0f, 500}, 40, Fade(MENU_LILAC, 0.8f));
}

void BBRenderer::menu_scene(const BBState& s, double t, bool glow) {
    using Sc = BBMenu::Screen;
    const BBMenu& m = s.menu;
    if (m.screen == Sc::Loading) {  // the title picture, the name over it
        if (intro_.id && !glow)
            DrawTexturePro(intro_, {0, 0, float(intro_.width), float(intro_.height)}, {0, 0, float(W), float(H)}, {0, 0}, 0,
                           WHITE);
        float pulse = 0.85f + 0.15f * float(std::sin(t * 2));
        if (!glow) text(m.title.c_str(), {W / 2.0f + 6, 836 + 6}, 170, Fade(Color{40, 10, 80, 255}, 0.85f), true);
        text(m.title.c_str(), {W / 2.0f, 836}, 170, scale(Color{235, 165, 255, 255}, pulse), true);
        if (!glow) text(m.publisher.c_str(), {W / 2.0f, 940}, 30, Fade(MENU_LILAC, 0.8f));
        float blink = 0.5f + 0.5f * float(std::sin(t * 4));
        text("PRESS ANY KEY", {W / 2.0f, 1010}, 36, Fade(WHITE, glow ? 0.5f * blink : blink));
        return;
    }
    // the room, dimmed: the menus float over it
    room(0, t, glow);
    DrawRectangle(0, 0, W, H, Fade(BLACK, glow ? 0.7f : 0.62f));
    // the logo
    float pulse = 0.9f + 0.1f * float(std::sin(t * 2));
    // leaving for a game: the title rises away, the items part left and right (as the original's wipe)
    const float gone = m.leave < 1 ? m.leave * m.leave * (3 - 2 * m.leave) : 0, up = gone * 420, fade = 1 - gone;
    text(m.title.c_str(), {W / 2.0f + 6, 136 + 6 - up}, 150, Fade(Color{60, 20, 120, 255}, glow ? 0 : 0.8f * fade), true);
    text(m.title.c_str(), {W / 2.0f, 136 - up}, 150, Fade(scale(Color{225, 150, 255, 255}, pulse), fade), true);
    if (!glow) text((m.publisher + "    ZX REMAKE").c_str(), {W / 2.0f, 232 - up}, 26, Fade(MENU_LILAC, 0.75f * fade));

    auto footer = [&](const char* s2) {
        if (!glow) text(s2, {W / 2.0f, H - 60.0f}, 28, Fade(MENU_LILAC, 0.7f));
    };
    auto title = [&](const char* s2) { text(s2, {W / 2.0f, 320}, 64, MENU_GOLD); };
    switch (m.screen) {
    case Sc::Main: {
        for (int i = 0; i < 6; i++) {
            float y = 400 + i * 88.0f, cx = W / 2.0f + (i % 2 ? 1 : -1) * gone * W * 0.75f;
            bool on = i == m.cursor;
            if (on) {
                float wpx = 640, hpx = 72;
                Rectangle bar{cx - wpx / 2, y - hpx / 2, wpx, hpx};
                if (!glow) DrawRectangleRounded(bar, 0.4f, 8, Fade(Color{120, 60, 220, 255}, 0.35f * fade));
                DrawRectangleRoundedLinesEx(bar, 0.4f, 8, glow ? 6 : 3, Fade(scale(Color{230, 170, 255, 255}, pulse), fade));
            }
            text(MENU_ITEMS[i], {cx, y}, on ? 50 : 42, Fade(on ? WHITE : (glow ? Fade(MENU_DIM, 0.4f) : MENU_DIM), fade));
        }
        if (!glow && gone == 0)
            text("Programming Richard Taylor    Graphics Jon Law    Design Clement Chambers    Sound Jay Derrett",
                 {W / 2.0f, H - 110.0f}, 24, Fade(MENU_LILAC, 0.6f));
        if (gone == 0) footer("UP / DOWN  choose      ENTER  select      ESC  pause");
        break;
    }
    case Sc::Controls: {
        title("CONTROLS");
        const char* rows[][2] = {{"Move the bat", "ARROWS  /  W A S D  /  gamepad"},
                                 {"Fire a missile", "SPACE  /  CTRL  /  gamepad A"},
                                 {"Menus", "UP  DOWN   ENTER  /  SPACE"},
                                 {"Pause", "ESC  /  gamepad START"}};
        for (int i = 0; i < 4; i++) {
            float y = 440 + i * 90.0f;
            if (!glow) text_left(rows[i][0], {360, y - 22}, 40, MENU_LILAC);
            text_left(rows[i][1], {900, y - 22}, 40, WHITE);
        }
        footer("ENTER  back");
        break;
    }
    case Sc::Keys: {
        title("REMAKE KEYS");
        const char* rows[][2] = {{"R  /  TAB", "original 1987  /  remastered"},
                                 {"L", "unlimited lives"},
                                 {"K", "unlimited missiles"},
                                 {"O", "original speed (slowdowns) / even"},
                                 {"T", "HD theme"},
                                 {"ESC", "pause: jump to a level, sound, quit"},
                                 {"F11", "full screen"}};
        const int n = int(sizeof rows / sizeof rows[0]);
        for (int i = 0; i < n; i++) {
            float y = 400 + i * 72.0f;
            text_left(rows[i][0], {460, y - 22}, 40, MENU_GOLD);
            if (!glow) text_left(rows[i][1], {800, y - 22}, 40, WHITE);
        }
        footer("any key  back");
        break;
    }
    case Sc::HiScores: {
        title(m.entry_row >= 0 ? "NEW HI-SCORE  -  TYPE YOUR NAME" : "HI-SCORES");
        for (size_t i = 0; i < m.scores.size() && i < 9; i++) {
            float y = 400 + float(i) * 62;
            bool on = int(i) == m.entry_row;
            Color c = on ? WHITE : (i == 0 ? MENU_GOLD : MENU_LILAC);
            if (on) {
                Rectangle bar{440, y - 28, 1040, 56};
                if (!glow) DrawRectangleRounded(bar, 0.4f, 8, Fade(Color{120, 60, 220, 255}, 0.35f));
                DrawRectangleRoundedLinesEx(bar, 0.4f, 8, glow ? 5 : 2.5f, scale(Color{230, 170, 255, 255}, pulse));
            }
            char buf[48];
            std::snprintf(buf, sizeof buf, "%zu", i + 1);
            text_left(buf, {480, y - 22}, 40, c);
            std::string name = m.scores[i].name;
            if (on && std::fmod(t, 0.8) < 0.4) name += "_";
            text_left(name.c_str(), {600, y - 22}, 40, c);
            std::snprintf(buf, sizeof buf, "%07d", m.scores[i].score);
            Vector2 w = MeasureTextEx(font_ui_, buf, 40, 40 * 0.06f);
            text_left(buf, {1440 - w.x, y - 22}, 40, c);
        }
        footer(m.entry_row >= 0 ? "type your name, ENTER" : "any key  back");
        break;
    }
    case Sc::Instructions: {
        title("THE BRICKS");
        const int per = BBMenu::INSTRUCTIONS_PER_PAGE, pages = std::max(1, int((m.items.size() + per - 1) / per));
        int page = m.instructions_page >= 0 ? m.instructions_page % pages : int(m.since / 6) % pages;
        for (int i = 0; i < per; i++) {
            size_t k = size_t(page * per + i);
            if (k >= m.items.size()) break;
            const BBMenu::Item& it = m.items[k];
            float cx = 330 + (i % 3) * 560.0f, cy = 450 + (i / 3) * 180.0f;
            thumbnail(it.sprite, {cx, cy}, 5.0f, t, glow);
            if (glow) continue;
            float ty = cy - 46;
            if (it.points > 0) {
                char buf[24];
                std::snprintf(buf, sizeof buf, "%d POINTS", it.points);
                text_left(buf, {cx + 80, ty}, 30, MENU_GOLD);
                ty += 40;
            }
            std::string txt = it.text;
            for (size_t a = 0; a < txt.size();) {  // lines split at '*'
                size_t b = txt.find('*', a);
                std::string ln = txt.substr(a, b == std::string::npos ? std::string::npos : b - a);
                text_left(ln.c_str(), {cx + 80, ty}, 28, WHITE);
                ty += 34;
                if (b == std::string::npos) break;
                a = b + 1;
            }
        }
        for (int p = 0; p < pages; p++)  // page dots
            DrawCircle(int(W / 2.0f + (p - (pages - 1) / 2.0f) * 30), H - 110, p == page ? 8 : 5, p == page ? WHITE : MENU_DIM);
        footer(m.instructions_auto
                   ? (m.instructions_page >= 0 ? "LEFT / RIGHT  pages  /  auto paging off  /  returns to menu when finished"
                                               : "LEFT / RIGHT  pages  /  automatic pages  /  returns to menu when finished")
                   : "LEFT / RIGHT  pages  /  ENTER  back");
        break;
    }
    default: break;
    }
}
