#include "ballbreaker_hd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <rlgl.h>

#include "bb_studio.h"

namespace {

constexpr int SPRITES = 60, SPRITE_W = 24, SPRITE_H = 20, SPRITE_BYTES = 120;
constexpr float CUBE = 12;
constexpr float CREATURE_PAD = 3;  // sprite pixels around each painted creature frame (tools/bb_creatures.py)
constexpr float WALL_H = 64, LIP = 8;
constexpr float ROOM_X = BB_ROOM_WIDTH, ROOM_Z = 148;  // HD room encloses the full 8 x 12 brick columns; left wall at Z = 148
// this level's: some levels have bricks set into a wall (the original's flat sprites just cover
// it); the walls move out to them, so they stand against the wall (BBRenderer::prepare)
float FLOOR_X = ROOM_X, FLOOR_Z = ROOM_Z;

// What each sprite is (the bricks in the game's instructions), drawn as that shape in that colour.
enum class Shape { Cube, Armored, Framed, Gate, Spinner, Pyramid, Diamond, Lying, Barrel, LifeBall, Wire, Prism, Ramp, Slabs };

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
    case 0x05: return {Shape::Gate, {255, 150, 70, 255}};                // 400 points
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
        return {Shape::Spinner, {120, 160, 255, 255}};                   // a wall breathing in it
    case 0x00: return {Shape::Cube, {110, 80, 240, 255}};
    case 0x21: return {Shape::Cube, {120, 170, 255, 255}};
    case 0x25: return {Shape::Cube, {200, 110, 255, 255}};
    case 0x29: return {Shape::Cube, {240, 100, 230, 255}};
    default: return {Shape::Cube, plain};
    }
}

// A brick's shape and colour for the Studio look's 3D scene.
BBStudio::Look studio_look(int sprite) {
    const Kind k = kind_of(sprite);
    int shape = 0;
    switch (k.shape) {
    case Shape::Pyramid: shape = 1; break;
    case Shape::Slabs: shape = 2; break;
    case Shape::Lying: shape = 3; break;
    case Shape::Diamond: shape = 4; break;
    case Shape::Barrel: shape = 5; break;
    case Shape::Gate: shape = 6; break;
    case Shape::Spinner: shape = 7; break;
    case Shape::Prism: shape = 8; break;
    case Shape::Ramp: shape = 9; break;
    case Shape::Armored: shape = 10; break;
    case Shape::Framed: shape = 11; break;
    case Shape::Wire: shape = 12; break;
    case Shape::LifeBall: shape = 13; break;
    default: break;
    }
    return BBStudio::Look{k.colour, shape};
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
    temple_dir_ = creatures.substr(0, creatures.rfind("/creatures/")) + "/adventure";
    if ((menu_bg_ = bb_load_art(temple_dir_ + "/title")).id) SetTextureFilter(menu_bg_, TEXTURE_FILTER_BILINEAR);
    for (auto [tex, name] : {std::pair{&hud_board_, "board"}, {&hud_star_, "star"}, {&hud_heart_, "heart"}, {&hud_missile_, "missile"}}) {
        const std::string f = temple_dir_ + "/hud/" + name + ".png";
        if (!FileExists(f.c_str())) continue;
        *tex = LoadTexture(f.c_str());
        GenTextureMipmaps(tex);
        SetTextureFilter(*tex, TEXTURE_FILTER_TRILINEAR);
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
    if (const std::string c = fonts_dir + "/Cinzel-Black.ttf"; FileExists(c.c_str())) {  // carved capitals
        adv_big_ = LoadFontEx(c.c_str(), 160, nullptr, 0);
        GenTextureMipmaps(&adv_big_.texture);
        SetTextureFilter(adv_big_.texture, TEXTURE_FILTER_TRILINEAR);
    }
    if (const std::string c = fonts_dir + "/Cinzel-Bold.ttf"; FileExists(c.c_str())) {
        adv_ui_ = LoadFontEx(c.c_str(), 64, nullptr, 0);
        GenTextureMipmaps(&adv_ui_.texture);
        SetTextureFilter(adv_ui_.texture, TEXTURE_FILTER_TRILINEAR);
    }
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
    for (Texture2D* tx : {&hud_board_, &hud_star_, &hud_heart_, &hud_missile_, &menu_bg_})
        if (tx->id) UnloadTexture(*tx);
    if (font_big_.texture.id != GetFontDefault().texture.id) UnloadFont(font_big_);
    if (adv_big_.texture.id) UnloadFont(adv_big_);
    if (adv_ui_.texture.id) UnloadFont(adv_ui_);
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
        quad_grad(f1, w1, w2, f2, th.back_low, th.back_high, th.back_high, th.back_low);     // back wall
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
    case Shape::Gate: {  // a "П" lying flat: the bar at the back, the legs along the floor towards the bat
        const float bar = 5, leg = 4;
        box(x0, x1, y0, y1, z1 - bar, z1, c);  // far to near: the bar, the far leg, the near one
        box(x1 - leg, x1, y0, y1, z0, z1 - bar, c);
        box(x0, x0 + leg, y0, y1, z0, z1 - bar, c);
        break;
    }
    case Shape::Spinner: {
        // an upturned arch (a "U": two sides and a floor, open towards the bat and up) with a wall at
        // its back that breathes: the game plays 0x37 0x39 0x38 0x36 and back, the wall from full to
        // a quarter (0x35: none)
        const float t = 3;
        static const float FILL[5] = {0, 0.25f, 1, 0.5f, 0.75f};  // 0x35..0x39
        const float fill = FILL[std::clamp(o.sprite - 0x35, 0, 4)];
        box(x0 + t, x1 - t, y0, y0 + t, z0, z1, c);  // the floor
        if (fill > 0) box(x0 + t, x1 - t, y0 + t, y0 + t + (y1 - y0 - t) * fill, z1 - t, z1, c);  // the wall
        box(x1 - t, x1, y0, y1, z0, z1, c);  // the far side, its inner face seen
        box(x0, x0 + t, y0, y1, z0, z1, c);
        break;
    }
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
            // the tube shaded round, lit from above (-n): strips across it, a highlight near the top
            const Color body = fill(5);
            auto shade = [&](float t) {  // t: -1 the upper edge .. 1 the lower
                if (glow) return body;
                float u = (t + 0.3f) / 1.3f, k = 0.45f + 0.75f * std::max(0.0f, 1 - u * u);
                Color out = scale(body, k);
                float hl = std::max(0.0f, 1 - std::fabs(t + 0.5f) / 0.18f);
                return add(out, WHITE, (classic() ? 0.35f : 0.18f) * hl);
            };
            // strips across it, from the front end to the round back end (shaded as the body, so a
            // tube of several bricks stays one)
            const Vector2 u{dir.x / len * r, dir.y / len * r};
            const int strips = 16;
            for (int i = 0; i < strips; i++) {
                float ta = -1 + 2.0f * i / strips, tb = -1 + 2.0f * (i + 1) / strips;
                float ea = std::sqrt(std::max(0.0f, 1 - ta * ta)), eb = std::sqrt(std::max(0.0f, 1 - tb * tb));
                Color ca = shade(ta), cb = shade(tb);
                quad_grad({fc.x + n.x * ta, fc.y + n.y * ta}, {bc.x + n.x * ta + u.x * ea, bc.y + n.y * ta + u.y * ea},
                          {bc.x + n.x * tb + u.x * eb, bc.y + n.y * tb + u.y * eb}, {fc.x + n.x * tb, fc.y + n.y * tb},
                          ca, ca, cb, cb);
            }
            line({fc.x + n.x, fc.y + n.y}, {bc.x + n.x, bc.y + n.y}, w, c);
            line({fc.x - n.x, fc.y - n.y}, {bc.x - n.x, bc.y - n.y}, w, c);
            // the end: a disc, lighter to its upper left, a rim inside its edge
            if (glow) DrawCircleV(fc, r, fill(7));
            else {
                Color cap = fill(7);
                DrawCircleV(fc, r, scale(cap, 0.85f));
                const Vector2 hc{fc.x - r * 0.18f, fc.y - r * 0.18f};
                const Color in = scale(cap, classic() ? 1.12f : 1.4f), out = scale(cap, 0.85f);
                for (int i = 0; i < 32; i++) {  // a disc lighter at its centre (raylib's gradient circle
                    float a = i * 2 * PI / 32, b = (i + 1) * 2 * PI / 32;  // differs between versions)
                    tri_grad(hc, {hc.x + r * 0.78f * std::cos(a), hc.y + r * 0.78f * std::sin(a)},
                             {hc.x + r * 0.78f * std::cos(b), hc.y + r * 0.78f * std::sin(b)}, in, out, out);
                }
                DrawRing(fc, r * 0.72f, r * 0.8f, 0, 360, 40, Fade(scale(cap, 0.6f), 0.6f));
            }
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
    // the bat's mark ahead (shade_, from prepare): the lower part of the face that looks at the bat,
    // over the bat's width; on a pyramid its slanting face, on the wire cube the plane of its front
    if (!glow) {
        if (auto it = shade_.find(o.slot); it != shade_.end()) {
            const float xa = std::max(x0, it->second.x0), xb = std::min(x1, it->second.x1), h = CUBE * 0.55f;
            // the light of the bat's lane on the floor (bat_lane), carried on to the brick it meets
            const Color tone = classic() ? Color{255, 250, 225, 255} : Color{150, 230, 255, 255};
            const Color dark = Fade(tone, classic() ? 0.28f : 0.25f), none = Fade(tone, 0);
            if (xb > xa) {
                if (k.shape == Shape::Pyramid) {  // the face from (x0..x1, y0, z0) up to the apex (xm, y1, zm)
                    auto on = [&](float x, float v) {  // a point of the face at height v, x squeezed towards xm
                        float f = v / CUBE;
                        return P(xm + (x - xm) * (1 - f), y0 + v, z0 + (zm - z0) * f);
                    };
                    quad_grad(on(xa, 0), on(xb, 0), on(xb, h), on(xa, h), dark, dark, none, none);
                } else {
                    quad_grad(P(xa, y0, z0), P(xb, y0, z0), P(xb, y0 + h, z0), P(xa, y0 + h, z0), dark, dark, none, none);
                }
            }
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
    const float width = o.bat_width > 0 ? o.bat_width : shape.dx;
    return bb_visible_paddle_body(o.x, width, o.z, o.bat_inset, shape);
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

void BBRenderer::aim_mark(const BBObject& o, float x, bool glow) {
    // a small diamond on the floor just ahead of the bat's striking edge, in the lane's light
    if (glow) return;
    const BatBody b = bat_body(o);
    const Color light = classic() ? Color{60, 76, 96, 255} : Color{150, 230, 255, 255};
    const Color c = Fade(light, classic() ? 0.75f : 0.6f);
    const float z = b.z1 + 9, r = 2.4f;
    Vector2 n = P(x, 0, z - r), e = P(x + r, 0, z), sth = P(x, 0, z + r), w = P(x - r, 0, z);
    for (auto [p, q] : {std::pair{n, e}, {e, sth}, {sth, w}, {w, n}}) line(p, q, 2.0f, c);
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
        if (wv.geometric) {
            // Modern contacts include the side faces. Keep the spark on the
            // touched surface instead of falsely flashing the top striking edge.
            Vector2 hit = P(x0 + (x1 - x0) * wv.along, wv.y, wv.z);
            DrawCircleV(hit, (glow ? 28 : 12) * a, Fade(Color{220, 245, 255, 255}, a));
            continue;
        }
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
    if (studio() && studio_ && studio_->room()) {  // the Adventure look: chunks, and the world's own puff
        auto mixc = [](Color a, Color b, float t) {
            return Color{uint8_t(a.r + (b.r - a.r) * t), uint8_t(a.g + (b.g - a.g) * t), uint8_t(a.b + (b.b - a.b) * t), 255};
        };
        const BBWorld w = studio_->world();
        // the chunks: stone with a trace of its paint; ice; black rock
        Color base{104, 94, 80, 255};  // stone
        if (w == BBWorld::Ice) base = {200, 230, 255, 255};
        else if (w == BBWorld::Volcano || w == BBWorld::Castle) base = {52, 44, 42, 255};
        else if (w == BBWorld::Atlantis || w == BBWorld::Alien) base = {120, 120, 128, 255};
        else if (w == BBWorld::Tomb) base = {170, 140, 96, 255};
        else if (w == BBWorld::Steam || w == BBWorld::Cyber) base = {110, 112, 118, 255};  // metal
        const Color chunk = mixc(k.colour, base, w == BBWorld::Ice ? 0.75f : 0.86f);
        for (int i = 0, n = 14 + int(u(rng_) * 9); i < n; i++) {
            float a = -3.1416f * (0.05f + 0.9f * u(rng_)), sp = 200 + u(rng_) * 420;
            if (w == BBWorld::Atlantis) sp *= 0.55f;  // (slowed by the water)
            shards_.push_back({c, {std::cos(a) * sp, std::sin(a) * sp}, u(rng_) * 6.28f, (u(rng_) - 0.5f) * 12,
                               6 + u(rng_) * 14, 0, 0.6f + u(rng_) * 0.4f, scale(chunk, 0.8f + 0.4f * u(rng_))});
        }
        auto puff = [&](int count, int kind, Color col, float size, float speed, float rise, float life) {
            for (int i = 0; i < count; i++) {
                float a = u(rng_) * 6.2832f, sp = speed * (0.3f + u(rng_));
                dust_.push_back({{c.x + std::cos(a) * 24, c.y + std::sin(a) * 16}, {std::cos(a) * sp, std::sin(a) * sp * 0.5f - rise},
                                 size * (0.6f + 0.8f * u(rng_)), 0, life * (0.7f + 0.6f * u(rng_)), kind, col});
            }
        };
        switch (w) {
        case BBWorld::Jungle: puff(9, 0, Color{110, 94, 72, 255}, 40, 90, 40, 1.4f); break;
        case BBWorld::Atlantis:
            puff(6, 0, Color{150, 140, 110, 255}, 36, 50, 10, 1.6f);  // sand
            puff(14, 1, Color{210, 245, 255, 255}, 9, 80, 120, 1.6f);  // bubbles
            break;
        case BBWorld::Ice: puff(10, 2, Color{235, 245, 255, 255}, 38, 110, 20, 1.2f); break;
        case BBWorld::Volcano:
            puff(6, 3, Color{40, 32, 30, 255}, 44, 50, 70, 1.8f);  // smoke
            puff(14, 4, Color{255, 150, 40, 255}, 4, 380, 260, 0.9f);  // embers
            break;
        case BBWorld::Tomb: puff(12, 0, Color{176, 146, 96, 255}, 44, 110, 20, 1.6f); break;  // sand
        case BBWorld::Garden:
            puff(5, 0, Color{120, 110, 100, 255}, 34, 70, 30, 1.2f);
            puff(16, 6, Color{255, 180, 205, 255}, 7, 220, 160, 2.2f);  // petals
            break;
        case BBWorld::Castle:
            puff(6, 3, Color{60, 66, 76, 255}, 40, 50, 30, 1.6f);
            puff(8, 5, Color{120, 255, 160, 255}, 6, 60, 90, 2.0f);  // wisps
            break;
        case BBWorld::Steam:
            puff(9, 2, Color{230, 230, 225, 255}, 44, 120, 60, 1.4f);  // steam
            puff(10, 4, Color{255, 190, 90, 255}, 3, 340, 220, 0.7f);  // sparks
            break;
        case BBWorld::Cyber:
            puff(5, 3, Color{40, 36, 56, 255}, 40, 50, 50, 1.4f);
            for (int i = 0; i < 12; i++) {  // electric sparks
                float a = u(rng_) * 6.2832f, sp = 700 + u(rng_) * 700;
                sparks_.push_back({c, {std::cos(a) * sp, std::sin(a) * sp}, 0, 0.12f + u(rng_) * 0.12f,
                                   i % 3 ? Color{90, 230, 255, 255} : Color{255, 120, 230, 255}});
            }
            break;
        case BBWorld::Alien: puff(16, 5, Color{150, 255, 140, 255}, 6, 90, 80, 2.0f); break;  // spores
        }
        const Color warm = bb_world_style(w).glow_in;
        lights_.push_back({cx, cy, cz, bomb ? 55.f : 24.f, bomb ? 2.0f : 0.8f, 0, 0.3f, warm});
        if (bomb) flashes_.push_back({c, 260.f, 0, 0.2f, Color{255, 150, 60, 255}});
        if (points > 0) popups_.push_back({{c.x, c.y - 40}, 0, points});
        return;
    }
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
    const bool temple = studio() && studio_ && studio_->room();
    if (temple && !glow) {  // the temple: dust under the chunks, chunks solid
        for (const Dust& d : dust_) {
            const float a = 1 - d.life / d.max;
            const Color c = d.c;
            if (d.kind == 1) {  // a bubble: a ring with a glint
                DrawCircleLinesV(d.p, d.r, Color{c.r, c.g, c.b, uint8_t(200 * a)});
                DrawCircleV({d.p.x - d.r * 0.35f, d.p.y - d.r * 0.35f}, d.r * 0.25f, Color{255, 255, 255, uint8_t(180 * a)});
            } else if (d.kind == 5) {  // a glow floating up
                BeginBlendMode(BLEND_ADDITIVE);
                DrawCircleGradient(d.p, d.r * 3, Color{c.r, c.g, c.b, uint8_t(220 * a)}, Color{c.r, c.g, c.b, 0});
                EndBlendMode();
            } else if (d.kind == 6) {  // a petal, turning as it flutters
                const float ang = d.life * 5 + d.r, ca = std::cos(ang), sa = std::sin(ang), l = d.r, w2 = d.r * 0.5f;
                const Vector2 p0{d.p.x + ca * l, d.p.y + sa * l}, p1{d.p.x - sa * w2, d.p.y + ca * w2},
                    p2{d.p.x - ca * l, d.p.y - sa * l}, p3{d.p.x + sa * w2, d.p.y - ca * w2};
                const Color pc{c.r, c.g, c.b, uint8_t(240 * std::min(1.0f, a * 2))};
                tri(p0, p1, p2, pc), tri(p0, p2, p3, pc);
            } else if (d.kind == 4) {  // an ember
                BeginBlendMode(BLEND_ADDITIVE);
                DrawCircleGradient(d.p, d.r * 2.5f, Color{c.r, c.g, c.b, uint8_t(255 * a)}, Color{255, 60, 0, 0});
                EndBlendMode();
            } else {
                DrawCircleGradient(d.p, d.r, Color{c.r, c.g, c.b, uint8_t((d.kind == 2 ? 200 : 170) * a * a)}, Color{c.r, c.g, c.b, 0});
            }
        }
        for (const Shard& sh : shards_) {
            const float a = std::min(1.0f, 3 * (1 - sh.life / sh.max));
            Vector2 p[4];
            for (int i = 0; i < 4; i++) {  // a rough four-sided chip
                const float ang = sh.ang + i * 1.57f + (i & 1 ? 0.35f : 0), rr = sh.size * (i == 2 ? 0.7f : 1.0f);
                p[i] = {sh.p.x + std::cos(ang) * rr, sh.p.y + std::sin(ang) * rr};
            }
            tri(p[0], p[1], p[2], Fade(sh.c, a)), tri(p[0], p[2], p[3], Fade(scale(sh.c, 0.75f), a));
            line(p[0], p[1], 1.5f, Fade(scale(sh.c, 1.25f), a));  // its lit edge
        }
    }
    for (const Shard& sh : shards_) {
        if (temple) break;
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
            if (temple) {  // gold, carved, with a dark rim
                const Vector2 at{u.p.x, u.p.y - u.life * 120};
                text(txt.c_str(), {at.x + 2, at.y + 3}, 50, Fade(Color{30, 18, 4, 255}, a));
                text(txt.c_str(), at, 50, Fade(Color{255, 214, 120, 255}, a));
                continue;
            }
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
    // The ball against everything it overlaps on screen: two convex bodies, the sphere is in front
    // when it lies towards the viewer from the other's nearest point (into the picture is (2, -1, 2)
    // in X, Y, Z: it projects to a point). The sort above goes by sprite anchors, which puts a ball
    // beside a brick behind it ("under" it) or over the bat's end; it is placed after the last thing
    // it is in front of (or before the first it is behind), outside the comparator (a pair-specific
    // comparison inside stable_sort would not be a strict weak ordering).
    auto ball_it = std::find_if(order.begin(), order.end(), [](auto o) { return o->slot == 0; });
    if (ball_it != order.end()) {
        const BBObject* sphere = *ball_it;
        // the rendered centre (object coordinates are sprite anchors; the bounce height is in sy)
        const float x = sphere->x + 5;
        const float z = 160 + x - (sphere->sx + 12);
        const float y = 118 - x / 2 - (sphere->sy + 10);
        const float cu = 160 + x - z, cv = 118 - x / 2 - y, r = BALL_RADIUS + 1;
        const size_t was = size_t(ball_it - order.begin());
        order.erase(ball_it);
        int after = -1, before = -1;
        for (size_t i = 0; i < order.size(); i++) {
            const BBObject& o = *order[i];
            float x0, x1, y0, y1, z0, z1;
            if (o.slot == 1) {
                const auto body = bat_body(o);
                x0 = body.x0, x1 = body.x1, y0 = 0, y1 = body.height, z0 = body.z0, z1 = body.z1;
            } else if (o.slot >= 3) {
                x0 = o.x, x1 = o.x + CUBE, y0 = o.y, y1 = o.y + CUBE, z0 = o.z - CUBE, z1 = o.z;
            } else continue;
            // overlapping on screen at all? (the box's projected bounds against the ball's)
            float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
            for (float px : {x0, x1}) for (float py : {y0, y1}) for (float pz : {z0, z1}) {
                const float u = 160 + px - pz, v = 118 - px / 2 - py;
                u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
            }
            if (cu + r < u0 || cu - r > u1 || cv + r < v0 || cv - r > v1) continue;
            const float qx = std::clamp(x, x0, x1), qy = std::clamp(y, y0, y1), qz = std::clamp(z, z0, z1);
            if (2 * (x - qx) - (y - qy) + 2 * (z - qz) < 0) after = int(i);
            else if (before < 0) before = int(i);
        }
        const size_t at = after >= 0 ? size_t(after + 1) : before >= 0 ? size_t(before) : was;
        order.insert(order.begin() + long(std::min(at, order.size())), sphere);
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
        if (o.slot == 1) {
            bat_lane(o, t, glow);
            if (s.aim >= 0) aim_mark(o, s.aim, glow);
        }
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
    if (studio()) {  // the Studio look: its reflection pass now, outside the frame's target
        using Sc = BBMenu::Screen;
        if (!studio_) studio_ = std::make_unique<BBStudio>(temple_dir_);  // (the menus' brick pictures too)
        if (s.menu.screen == Sc::None || (s.menu.screen == Sc::LevelCard && s.menu.leave >= 1)) {
            studio_->set_world(world_of(s));
            // (during the card the objects are not the level's yet: the room empty)
            BBState empty;
            if (s.menu.screen == Sc::LevelCard) empty = s, empty.objects.clear();
            studio_->prepare(s.menu.screen == Sc::LevelCard ? empty : s, SCALE, studio_look, [this](const BBObject& o) { return bat_body(o); }, slab_lift);
        }
    }
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
    // the bat's shadow ahead: a missile flies straight along Z on the floor from the bat and hits the
    // first floor brick in its way. The bat's width cast forward onto those bricks, on the face that
    // looks at the bat, shows where shots go: over the bat's X range, in thin steps, the nearest
    // floor brick of each step takes that piece (two bricks may share it)
    shade_.clear();
    {
        const BBObject* bat = nullptr;
        for (const auto& o : s.objects)
            if (o.slot == 1) bat = &o;
        if (bat) {
            const BatBody body = bat_body(*bat);
            for (float x = body.x0 + 0.125f; x < body.x1; x += 0.25f) {
                const BBObject* hit = nullptr;
                for (const auto& o : s.objects)
                    if (o.slot >= 3 && o.y < 1 && !(o.sprite >= 0x11 && o.sprite <= 0x1f) && x >= o.x && x < o.x + CUBE &&
                        o.z - CUBE >= body.z1 - 0.5f && (!hit || o.z < hit->z))
                        hit = &o;
                if (!hit) continue;
                auto [it, fresh] = shade_.try_emplace(hit->slot, Shade{x - 0.125f, x + 0.125f});
                if (!fresh) it->second.x0 = std::min(it->second.x0, x - 0.125f), it->second.x1 = std::max(it->second.x1, x + 0.125f);
            }
        }
    }
    for (Shard& sh : shards_) {
        sh.life += dt;
        sh.p.x += sh.v.x * dt, sh.p.y += sh.v.y * dt;
        sh.v.y += 1400 * dt;
        sh.ang += sh.spin * dt;
    }
    for (Dust& d : dust_) {
        d.life += dt, d.p.x += d.v.x * dt, d.p.y += d.v.y * dt;
        if (d.kind == 4) {  // embers fly and fall
            d.v.y += 700 * dt;
            continue;
        }
        if (d.kind == 5) {  // glows drift up, swaying
            d.v.x *= 1 - 1.2f * dt, d.v.y = d.v.y * (1 - 1.2f * dt) - 40 * dt, d.p.x += 30 * float(std::sin(d.life * 3 + d.r)) * dt;
            continue;
        }
        if (d.kind == 6) {  // petals flutter down
            d.v.x *= 1 - 1.0f * dt, d.v.y = d.v.y * (1 - 2.0f * dt) + 140 * dt, d.p.x += 40 * float(std::sin(d.life * 4 + d.r)) * dt;
            continue;
        }
        d.v.x *= 1 - 1.5f * dt, d.v.y *= 1 - 1.5f * dt;
        if (d.kind == 1) d.v.y -= 160 * dt, d.p.x += 20 * float(std::sin(d.life * 9 + d.r)) * dt;  // bubbles rise, wobble
        else d.r += (d.kind == 3 ? 45.f : 30.f) * dt;
    }
    std::erase_if(dust_, [](const Dust& d) { return d.life >= d.max; });
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
        if (b && age < 0.35f)
            bat_waves_.push_back({hit.along, age, hit.geometric, hit.y,
                                  hit.z - BB_BALL_RADIUS * hit.nz});
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

bool BBRenderer::studio_draw(const BBState& s, double t) {
    // the play only (the menus as ever); the level card over the room once it has come in
    using Sc = BBMenu::Screen;
    const bool play = s.menu.screen == Sc::None || (s.menu.screen == Sc::LevelCard && s.menu.leave >= 1);
    if (!play) return false;
    if (!studio_) return false;  // (made in prepare)
    // the temple painted (walls, floor, the jungle round it), else around the 3D one the jungle's dark
    if (const Texture2D* room = studio_->room()) {
        // (darker and moodier than painted: the jungle's shade)
        DrawTexturePro(*room, {0, 0, float(room->width), float(room->height)}, {0, 0, float(W), float(H)}, {0, 0}, 0,
                       bb_world_style(studio_->world()).room_tint);
    } else {
        DrawRectangleGradientV(0, 0, W, H, Color{20, 28, 18, 255}, Color{6, 8, 5, 255});
        DrawCircleGradient(Vector2{W * 0.55f, H * 0.5f}, W * 0.55f, Color{110, 70, 25, 90}, Color{20, 28, 18, 0});
    }
    BBState empty;  // (during the card the objects are not the level's yet: the room empty)
    if (s.menu.screen == Sc::LevelCard) empty = s, empty.objects.clear();
    const BBState& shown = s.menu.screen == Sc::LevelCard ? empty : s;
    studio_->draw(shown, t, SCALE, studio_look, [this](const BBObject& o) { return bat_body(o); }, slab_lift);
    if (studio_->room()) {
        ambient(t);
        // the corners sink into the dark
        const Color sh = bb_world_style(studio_->world()).shadow;
        const Color clear{sh.r, sh.g, sh.b, 0}, dark{sh.r, sh.g, sh.b, 200};
        DrawRectangleGradientV(0, 0, W, 260, Color{sh.r, sh.g, sh.b, 150}, clear);
        DrawRectangleGradientV(0, H - 220, W, 220, clear, dark);
        DrawRectangleGradientH(0, 0, 320, H, dark, clear);
        DrawRectangleGradientH(W - 320, 0, 320, H, clear, dark);
    }
    // over it in 2D (lowered with the scene): the torches' glow, the creatures, the missile, the effects
    const Camera2D lower{{0, BBStudio::DROP}, {0, 0}, 0, 1};
    BeginMode2D(lower);
    BeginBlendMode(BLEND_ADDITIVE);
    if (studio_->room())  // the ball's band lights the floor under it
        for (const BBObject& o : shown.objects)
            if (o.slot == 0) {
                const Vector3 c = BBStudio::ball_at(o);
                const float hgt = std::max(0.0f, c.y - BALL_RADIUS), k = std::clamp(1 - hgt / 40, 0.2f, 1.0f);
                DrawCircleGradient(Vector2{(160 + c.x - c.z) * SCALE, (118 - c.x / 2) * SCALE}, 90,
                                   Color{60, 190, 255, uint8_t(70 * k)}, Color{60, 190, 255, 0});
            }
    for (int i = 0; i < BBStudio::TORCHES; i++) {
        const Vector3 f = BBStudio::torch(i);
        const Vector2 p{(160 + f.x - f.z) * SCALE, (118 - f.x / 2 - f.y) * SCALE};
        const float g = BBStudio::torch_glow(i, t), k = studio_->room() ? 0.75f : 1.0f;
        const BBWorldStyle& ws = bb_world_style(studio_->world());
        Color o = ws.glow_out, n = ws.glow_in;
        DrawCircleGradient(p, 150 * g, Color{o.r, o.g, o.b, uint8_t(std::min(255.f, 70 * g * k))}, Color{o.r, o.g, o.b, 0});
        DrawCircleGradient(p, 40 * g, Color{n.r, n.g, n.b, uint8_t(std::min(255.f, 90 * g * k))}, Color{n.r, n.g, n.b, 0});
    }
    EndBlendMode();
    for (const BBObject& o : shown.objects) {
        if (o.slot == 2 || (o.sprite >= 0x16 && o.sprite <= 0x18)) missile(o, t, false);
        else if (o.slot >= 3 && o.sprite >= 0x11 && o.sprite <= 0x15) {
            if (!adventure_creature(o, t)) sprite(o, Color{140, 255, 180, 255}, t, false);
        } else if (o.slot >= 3 && o.sprite >= 0x19 && o.sprite <= 0x1F && studio_->room()) {
            // coming or going (the explosion's frames, forwards or back): a flash in the world's light
            const float k = 1 - float(o.sprite - 0x19) / 7, cx = (o.sx + 12) * SCALE, cy = (o.sy + 10) * SCALE;
            const Color c = bb_world_style(studio_->world()).glow_in;
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleGradient(Vector2{cx, cy}, 40 + 90 * k, Color{c.r, c.g, c.b, uint8_t(160 * k)}, Color{c.r, c.g, c.b, 0});
            EndBlendMode();
        }
    }
    effects(t, false);
    EndMode2D();
    if (s.menu.screen == Sc::LevelCard) level_card(s, t, false);
    temple_panel(s, t);
    return true;
}

bool BBRenderer::adventure_creature(const BBObject& o, double t) {
    // the world's own creature: the walker's four steps (as the game turns them), the other's two
    // frames turn by time; a bob on its step or a float, a shadow under it
    const Texture2D* tex = studio_ ? studio_->creatures() : nullptr;
    if (!tex) return false;
    const float cw = float(tex->width) / 6, ch = float(tex->height), S = SCALE;
    const bool walker = o.sprite <= 0x14;
    const float phase = float(o.x * 0.37f + o.z * 0.21f);
    float lift = walker ? std::fabs(float(std::sin(t * 11 + phase))) * 0.5f : 1.2f + float(std::sin(t * 3 + phase)) * 1.0f;
    const Rectangle dst{(o.sx - 3) * S, (o.sy - 3 - lift) * S, 30 * S, 26 * S};
    const float floor_y = (o.sy + 20) * S;
    DrawEllipse(int(dst.x + dst.width / 2), int(floor_y - 4), dst.width * (walker ? 0.3f : 0.22f), 14, Color{0, 0, 0, uint8_t(walker ? 90 : 60)});
    auto cell = [&](int i, float a) {
        DrawTexturePro(*tex, {cw * float(i), 0, cw, ch}, dst, {0, 0}, 0, Fade(WHITE, a));
    };
    if (walker) cell(o.sprite - 0x11, 1);
    else {  // two frames, a short blend at each turn
        const float u = float(std::fmod(t * 2.2 + phase, 2.0)), f = u < 1 ? 0.0f : 1.0f, w = std::fmod(u, 1.0f);
        const float blend = std::clamp((w - 0.85f) / 0.15f, 0.0f, 1.0f);
        cell(4 + int(f), 1);
        if (blend > 0) cell(4 + (1 - int(f)), blend);
    }
    return true;
}

BBWorld BBRenderer::world_of(const BBState& s) const {
    // five levels each, in turn (BB_WORLD=0..9 holds one, to look at it)
    static const char* force = std::getenv("BB_WORLD");
    if (force) return BBWorld(std::clamp(std::atoi(force), 0, BB_WORLDS - 1));
    return BBWorld(std::clamp(s.level / BB_WORLD_LEVELS, 0, BB_WORLDS - 1));
}

void BBRenderer::ambient(double t) {
    // each world's own life over the picture (stateless: each particle is where its time puts it)
    auto h = [](int i, int k) { return float((i * (73 + 18 * k) + k * 31) % 101) / 101.f; };
    switch (studio_->world()) {
    case BBWorld::Jungle: sunlight(t); break;
    case BBWorld::Atlantis: {  // light from the surface, wavering; bubbles rising
        const Vector2 sun{900, -260};
        BeginBlendMode(BLEND_ADDITIVE);
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < 7; i++) {
            const float ang = 1.32f + 0.12f * float(i) + 0.03f * float(std::sin(t * 0.4 + i * 1.7));
            const float wid = 0.03f + 0.015f * float((i * 5) % 3), len = 1300;
            const float a = 22 + 14 * float(std::sin(t * (0.5 + 0.09 * i) + i * 2.0));
            const Vector2 p1{sun.x + std::cos(ang - wid) * len, sun.y + std::sin(ang - wid) * len};
            const Vector2 p2{sun.x + std::cos(ang + wid) * len, sun.y + std::sin(ang + wid) * len};
            rlColor4ub(150, 240, 255, uint8_t(std::max(0.0f, a)));
            rlVertex2f(sun.x, sun.y);
            rlColor4ub(150, 240, 255, 0);
            rlVertex2f(p2.x, p2.y);
            rlVertex2f(p1.x, p1.y);
        }
        rlEnd();
        EndBlendMode();
        for (int i = 0; i < 46; i++) {
            const float sp = 0.05f + 0.08f * h(i, 1), v = 1 - std::fmod(h(i, 2) + float(t) * sp, 1.0f);
            const float r = 2.5f + 6 * h(i, 3);
            const Vector2 p{40 + h(i, 0) * (W - 80) + 14 * float(std::sin(t * 2.0 + i)), 120 + v * (H - 120)};
            DrawCircleLinesV(p, r, Color{200, 245, 255, uint8_t(90 + 60 * h(i, 4))});
            DrawCircleV({p.x - r * 0.35f, p.y - r * 0.35f}, r * 0.25f, Color{255, 255, 255, 120});
        }
        break;
    }
    case BBWorld::Ice: {  // snow falling, swaying
        for (int i = 0; i < 140; i++) {
            const float sp = 0.06f + 0.1f * h(i, 1), v = std::fmod(h(i, 2) + float(t) * sp, 1.0f), r = 1.5f + 3.5f * h(i, 3);
            const Vector2 p{h(i, 0) * W + 40 * float(std::sin(t * (0.6 + h(i, 4)) + i)), v * (H + 40) - 20};
            DrawCircleGradient(p, r * 1.8f, Color{255, 255, 255, uint8_t(120 + 100 * h(i, 5))}, Color{220, 235, 255, 0});
        }
        break;
    }
    case BBWorld::Tomb: {  // sand streaming through the dark, catching the light
        for (int i = 0; i < 110; i++) {
            const float sp = 0.05f + 0.07f * h(i, 1), u = 1 - std::fmod(h(i, 2) + float(t) * sp, 1.0f);
            const Vector2 p{u * (W + 100) - 50, 140 + h(i, 0) * (H - 200) + 20 * float(std::sin(t * 1.3 + i))};
            const float a = 70 + 90 * h(i, 3);
            DrawLineEx(p, {p.x + 6 + 10 * h(i, 4), p.y - 1}, 1.6f, Color{235, 200, 140, uint8_t(a)});
        }
        break;
    }
    case BBWorld::Garden: {  // cherry petals drifting down
        for (int i = 0; i < 60; i++) {
            const float sp = 0.04f + 0.05f * h(i, 1), v = std::fmod(h(i, 2) + float(t) * sp, 1.0f);
            const Vector2 p{h(i, 0) * W + 120 * float(std::sin(t * (0.5 + h(i, 4)) + i)) - v * 200, v * (H + 40) - 20};
            const float ang = float(t) * (1.5f + 2 * h(i, 5)) + i, l = 5 + 4 * h(i, 3), w2 = l * 0.5f;
            const float ca = std::cos(ang), sa = std::sin(ang);
            const Color pc{255, uint8_t(175 + 30 * h(i, 6)), uint8_t(200 + 30 * h(i, 6)), 220};
            tri({p.x + ca * l, p.y + sa * l}, {p.x - sa * w2, p.y + ca * w2}, {p.x - ca * l, p.y - sa * l}, pc);
            tri({p.x + ca * l, p.y + sa * l}, {p.x - ca * l, p.y - sa * l}, {p.x + sa * w2, p.y - ca * w2}, pc);
        }
        break;
    }
    case BBWorld::Castle: {  // fog crawling over the floor; a few wisps
        for (int i = 0; i < 14; i++) {
            const float sp = 0.012f + 0.01f * h(i, 1), u = std::fmod(h(i, 2) + float(t) * sp, 1.0f);
            const Vector2 p{u * (W + 800) - 400, 640 + h(i, 0) * 420};
            DrawCircleGradient(p, 220 + 160 * h(i, 3), Color{150, 170, 190, uint8_t(38 + 20 * h(i, 4))}, Color{150, 170, 190, 0});
        }
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 10; i++) {
            const float sp = 0.03f + 0.03f * h(i, 1), v = 1 - std::fmod(h(i, 2) + float(t) * sp, 1.0f);
            const Vector2 p{200 + h(i, 0) * (W - 400) + 60 * float(std::sin(t * 0.8 + i)), 200 + v * (H - 300)};
            DrawCircleGradient(p, 14, Color{140, 255, 180, uint8_t(120 * v)}, Color{140, 255, 180, 0});
        }
        EndBlendMode();
        break;
    }
    case BBWorld::Steam: {  // steam blowing from vents at the edges, now and then
        for (int i = 0; i < 6; i++) {
            const Vector2 vent{i < 3 ? 60.0f + 50 * i : W - 60.0f - 50 * (i - 3), H - 140.0f - 160 * float(i % 3)};
            const float cycle = std::fmod(float(t) * 0.25f + h(i, 1), 1.0f);
            for (int k = 0; k < 6; k++) {
                const float age = cycle - k * 0.04f;
                if (age < 0 || age > 0.5f) continue;
                const float a = 1 - age / 0.5f;
                const Vector2 p{vent.x + (i < 3 ? 1 : -1) * age * 500, vent.y - age * 300};
                DrawCircleGradient(p, 40 + age * 260, Color{235, 235, 230, uint8_t(70 * a)}, Color{235, 235, 230, 0});
            }
        }
        break;
    }
    case BBWorld::Cyber: {  // rain, slanting; the neon now and then stuttering
        for (int i = 0; i < 220; i++) {
            const float sp = 0.9f + 0.6f * h(i, 1), v = std::fmod(h(i, 2) + float(t) * sp, 1.0f);
            const Vector2 p{h(i, 0) * (W + 200) - v * 160, v * (H + 60) - 30};
            DrawLineEx(p, {p.x - 8, p.y + 28}, 1.4f, Color{170, 190, 255, uint8_t(60 + 50 * h(i, 3))});
        }
        if (std::fmod(t, 7.3) < 0.12 && std::fmod(t * 40, 2.0) < 1.0)
            DrawRectangle(0, 0, W, H, Color{20, 0, 30, 60});  // (a flicker of the signs)
        break;
    }
    case BBWorld::Alien: {  // spores rising, glowing
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 70; i++) {
            const float sp = 0.03f + 0.05f * h(i, 1), v = 1 - std::fmod(h(i, 2) + float(t) * sp, 1.0f), r = 2 + 3 * h(i, 3);
            const Vector2 p{h(i, 0) * W + 60 * float(std::sin(t * (0.4 + h(i, 4)) + i * 1.3)), 120 + v * (H - 120)};
            const Color sc = i % 3 ? Color{140, 255, 150, 255} : Color{230, 120, 255, 255};
            const float a = (0.5f + 0.5f * float(std::sin(t * (1.5 + 2 * h(i, 5)) + i))) * v;
            DrawCircleGradient(p, r * 3, Color{sc.r, sc.g, sc.b, uint8_t(200 * a)}, Color{sc.r, sc.g, sc.b, 0});
        }
        EndBlendMode();
        break;
    }
    case BBWorld::Volcano: {  // embers rising, flickering; the heat below breathing
        BeginBlendMode(BLEND_ADDITIVE);
        const float heat = 0.5f + 0.5f * float(std::sin(t * 0.9));
        DrawRectangleGradientV(0, H - 300, W, 300, Color{255, 80, 10, 0}, Color{255, 80, 10, uint8_t(40 + 40 * heat)});
        for (int i = 0; i < 70; i++) {
            const float sp = 0.07f + 0.12f * h(i, 1), v = 1 - std::fmod(h(i, 2) + float(t) * sp, 1.0f), r = 1.5f + 2.5f * h(i, 3);
            const Vector2 p{h(i, 0) * W + 50 * float(std::sin(t * (0.8 + h(i, 4)) + i * 1.3)), 100 + v * (H - 100)};
            const float a = (0.5f + 0.5f * float(std::sin(t * (3 + 4 * h(i, 5)) + i))) * v;
            DrawCircleGradient(p, r * 2.5f, Color{255, 170, 60, uint8_t(220 * a)}, Color{255, 60, 0, 0});
        }
        EndBlendMode();
        break;
    }
    }
}

void BBRenderer::sunlight(double t) {
    // the painted temple's sun: shafts from the upper left, slowly breathing, and dust drifting in them
    const Vector2 sun{620, -140};
    BeginBlendMode(BLEND_ADDITIVE);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 6; i++) {
        const float ang = 2.05f + 0.13f * float(i) + 0.02f * float(std::sin(t * 0.21 + i * 1.3));  // down-left
        const float wid = 0.025f + 0.012f * float((i * 7) % 3), len = 980;
        const float a = 26 + 16 * float(std::sin(t * (0.35 + 0.07 * i) + i * 2.0));
        const Vector2 p1{sun.x + std::cos(ang - wid) * len, sun.y + std::sin(ang - wid) * len};
        const Vector2 p2{sun.x + std::cos(ang + wid) * len, sun.y + std::sin(ang + wid) * len};
        rlColor4ub(255, 214, 140, uint8_t(std::max(0.0f, a)));
        rlVertex2f(sun.x, sun.y);
        rlColor4ub(255, 214, 140, 0);
        rlVertex2f(p2.x, p2.y);
        rlVertex2f(p1.x, p1.y);
    }
    rlEnd();
    for (int i = 0; i < 60; i++) {  // the motes: each its own slow drift and twinkle
        const float h1 = float((i * 73) % 101) / 101.f, h2 = float((i * 37) % 89) / 89.f, h3 = float((i * 53) % 97) / 97.f;
        const float u = std::fmod(h1 + float(t) * (0.004f + 0.006f * h3), 1.0f), v = std::fmod(h2 + float(t) * 0.01f * h1, 1.0f);
        const Vector2 p{80 + u * 560 + 30 * float(std::sin(t * 0.5 + i)), 60 + v * 420 + u * 160};
        const float a = 90 * (0.5f + 0.5f * float(std::sin(t * (1.0 + h3 * 2) + i * 1.7)));
        DrawCircleV(p, 1.4f + 1.6f * h3, Color{255, 236, 190, uint8_t(a)});
    }
    EndBlendMode();
}

void BBRenderer::temple_panel(const BBState& s, double) {
    if (hud_board_.id && hud_star_.id && hud_heart_.id && hud_missile_.id) {
        // carved wooden boards over the jungle: the score with its star, the level, the lives and missiles
        const Color white{250, 246, 236, 255}, ink{28, 16, 6, 255};
        const float y = 8, hgt = 88;
        auto board = [&](float x, float w) {
            // the pointed ends keep their shape (scaled to the height), the middle stretches
            const float tw = float(hud_board_.width), th = float(hud_board_.height), e = th * 0.55f, k = hgt / th;
            DrawTexturePro(hud_board_, {0, 0, e, th}, {x, y, e * k, hgt}, {0, 0}, 0, WHITE);
            DrawTexturePro(hud_board_, {e, 0, tw - 2 * e, th}, {x + e * k, y, w - 2 * e * k, hgt}, {0, 0}, 0, WHITE);
            DrawTexturePro(hud_board_, {tw - e, 0, e, th}, {x + w - e * k, y, e * k, hgt}, {0, 0}, 0, WHITE);
        };
        auto icon = [&](const Texture2D& tx, Vector2 c, float size) {
            const float k = size / float(std::max(tx.width, tx.height));
            DrawTexturePro(tx, {0, 0, float(tx.width), float(tx.height)},
                           {c.x - tx.width * k / 2, c.y - tx.height * k / 2, tx.width * k, tx.height * k}, {0, 0}, 0, WHITE);
        };
        // bold lettering with a dark rim (the font has one weight: drawn a few times over)
        auto say = [&](const char* str, Vector2 at, float size, bool centre = false) {
            if (centre) {
                const Vector2 m = MeasureTextEx(lettering(false), str, size, size * 0.06f);
                at = {at.x - m.x / 2, at.y - m.y / 2};
            }
            const float r = size * 0.07f;
            for (float dx = -r; dx <= r + 1; dx += r / 2)
                for (float dy = -r; dy <= r + 2; dy += r / 2) text_left(str, {at.x + dx, at.y + dy}, size, ink);
            for (float dx = 0; dx <= size * 0.035f; dx += 0.75f) text_left(str, {at.x + dx, at.y}, size, white);
        };
        char buf[64];
        board(20, 420);
        icon(hud_star_, {98, y + hgt / 2}, 64);
        say("SCORE", {146, y + 10}, 28);
        std::snprintf(buf, sizeof buf, "%07d", s.score);
        say(buf, {146, y + 38}, 42);
        board(W / 2.0f - 170, 340);
        std::snprintf(buf, sizeof buf, "LEVEL %d", s.level + 1);
        say(buf, {W / 2.0f, y + hgt / 2}, 44, true);
        const float rx = W - 20 - 560, mid = rx + 330;
        board(rx, 560);
        DrawRectangle(int(mid), int(y + 14), 4, int(hgt - 28), Color{40, 24, 12, 200});  // the two halves
        DrawRectangle(int(mid) + 4, int(y + 14), 2, int(hgt - 28), Color{150, 110, 70, 120});
        say("LIVES", {rx + 165, y + 24}, 28, true);
        const int shown = std::min(s.lives, 4);
        for (int i = 0; i < shown; i++) icon(hud_heart_, {rx + 165 + (i - (shown - 1) / 2.0f) * 50, y + 60}, 44);
        if (s.lives > shown || s.unlimited_lives) {
            std::snprintf(buf, sizeof buf, s.unlimited_lives ? "+" : "+%d", s.lives - shown);
            say(buf, {rx + 175 + (shown + 1) / 2.0f * 50, y + 46}, 28);
        }
        say("MISSILES", {mid + 95, y + 24}, 24, true);
        icon(hud_missile_, {mid + 52, y + 60}, 44);
        std::snprintf(buf, sizeof buf, s.unlimited_missiles ? "--" : "%02d", s.missiles);
        say(buf, {mid + 78, y + 39}, 42);
        return;
    }
    // a carved stone lintel over the temple: the score, the level, the lives and missiles, in gold
    constexpr int PH = 96;
    const Color gold{238, 190, 92, 255}, dim{176, 150, 110, 255}, cut{40, 32, 24, 255};
    DrawRectangleGradientV(0, 0, W, PH, Color{92, 78, 58, 255}, Color{58, 48, 36, 255});
    for (int x = 0; x < W; x += 160) DrawRectangle(x, 0, 2, PH - 6, Color{48, 40, 30, 120});  // its stones
    DrawRectangle(0, PH - 6, W, 3, cut);
    DrawRectangle(0, PH - 3, W, 3, Color{200, 150, 60, 255});
    // the step frets at its ends
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < 4; i++) {
            const int x = side ? W - 30 - (i + 1) * 18 : 30 + i * 18, hgt = 14 + i * 12;
            DrawRectangle(x, PH - 14 - hgt, 16, hgt, Color{70, 58, 42, 255});
            DrawRectangle(x, PH - 14 - hgt, 16, 3, Color{200, 150, 60, 200});
        }
    auto carved = [&](const char* str, Vector2 at, float size, Color c, bool centre) {
        if (centre) text(str, {at.x + 2, at.y + 3}, size, cut), text(str, at, size, c);
        else text_left(str, {at.x + 2, at.y + 3}, size, cut), text_left(str, at, size, c);
    };
    char buf[64];
    carved("SCORE", {140, 14}, 26, dim, false);
    std::snprintf(buf, sizeof buf, "%07d", s.score);
    carved(buf, {140, 38}, 50, gold, false);
    std::snprintf(buf, sizeof buf, "LEVEL %d", s.level + 1);
    carved(buf, {W / 2.0f, PH / 2.0f - 2}, 56, gold, true);
    carved("LIVES", {1180, 14}, 26, dim, false);
    for (int i = 0; i < std::min(s.lives, 9); i++) {  // golden balls
        const Vector2 c{1196.0f + i * 44, 62};
        DrawCircleV({c.x + 2, c.y + 3}, 16, cut);
        DrawCircleGradient(c, 16, Color{255, 226, 140, 255}, Color{170, 112, 34, 255});
        DrawCircleV({c.x - 5, c.y - 6}, 4, Color{255, 250, 220, 220});
    }
    if (s.unlimited_lives) carved("UNLIMITED", {1180, 78}, 18, dim, false);
    carved("MISSILES", {1560, 14}, 26, dim, false);
    std::snprintf(buf, sizeof buf, "%02d", s.missiles);
    carved(buf, {1560, 38}, 50, gold, false);
    if (s.unlimited_missiles) carved("UNLIMITED", {1640, 54}, 18, dim, false);
}

void BBRenderer::draw(const BBState& s, double t) {
    bat_edge_ = false;
    if (studio()) {  // the Adventure look: its play, else its menus (no neon bloom)
        if (!studio_draw(s, t)) scene(s, t, false);
        // the picture opaque again (what was blended in thinned its alpha: holes where it is shown)
        rlDrawRenderBatchActive();
        rlSetBlendFactorsSeparate(RL_ZERO, RL_ONE, RL_ONE, RL_ZERO, RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
        DrawRectangle(0, 0, W, H, WHITE);
        EndBlendMode();
        return;
    }
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
const char* MENU_ITEMS[8] = {"PLAY GAME", "CONTROLS", "KEYS", "HI-SCORES", "INSTRUCTIONS", "SOUND FX", "OPTIONS", "EXIT"};
constexpr Color MENU_GOLD{255, 214, 120, 255}, MENU_LILAC{205, 170, 255, 255}, MENU_DIM{150, 120, 200, 255};
}  // namespace

void BBRenderer::text(const char* s, Vector2 centre, float size, Color c, bool big) const {
    const Font& f = lettering(big);
    Vector2 m = MeasureTextEx(f, s, size, size * 0.06f);
    DrawTextEx(f, s, {centre.x - m.x / 2, centre.y - m.y / 2}, size, size * 0.06f, c);
}

void BBRenderer::text_left(const char* s, Vector2 at, float size, Color c, bool big) const {
    DrawTextEx(lettering(big), s, at, size, size * 0.06f, c);
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
    } else if (studio() && studio_ && !glow) {  // the Adventure look: the brick in 3D, its stone
        studio_->icon(sprite, {centre.x, centre.y + bob}, scale * 1.15f, t, studio_look, slab_lift);
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
    if (studio() && studio_ && studio_->room() && !glow) {  // the Adventure look: the world it is in, above
        const char* name = bb_world_style(studio_->world()).name;
        DrawRectangleGradientV(0, 230, W, 70, Fade(BLACK, 0), Fade(BLACK, 0.45f));
        text(name, {W / 2.0f + 2, 272}, 44, Color{30, 18, 6, 220});
        text(name, {W / 2.0f, 269}, 44, Color{238, 196, 110, 255});
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "SCREEN %d", s.level + 1);
    float pulse = 0.85f + 0.15f * float(std::sin(t * 4));
    // (the Adventure look: gold and cream; else lilac)
    const bool adventure = studio() && studio_ && studio_->room();
    const Color head = adventure ? Color{250, 226, 170, 255} : Color{230, 170, 255, 255};
    const Color sub = adventure ? Color{226, 196, 140, 255} : MENU_LILAC;
    if (!s.menu.level_name.empty()) {  // Ballbreaker II: the level's name, its number, its designer
        float size = 110;
        while (size > 50 && MeasureTextEx(lettering(true), s.menu.level_name.c_str(), size, size * 0.06f).x > W - 160) size -= 6;
        text(s.menu.level_name.c_str(), {W / 2.0f, 395}, size, scale(head, pulse), true);
        text(buf, {W / 2.0f, 478}, 40, Fade(sub, 0.85f));
        if (!s.menu.designer.empty()) text(("DESIGNED BY " + s.menu.designer).c_str(), {W / 2.0f, 528}, 32, Fade(sub, 0.7f));
        return;
    }
    text(buf, {W / 2.0f, 400}, 140, scale(head, pulse), true);
    text("GET READY", {W / 2.0f, 500}, 40, Fade(sub, 0.8f));
}

void BBRenderer::menu_scene(const BBState& s, double t, bool glow) {
    using Sc = BBMenu::Screen;
    const BBMenu& m = s.menu;
    // the Adventure look: gold, cream and sand over the temple's gate; else neon lilac over the room
    const bool adv = studio();
    const Color GILT = adv ? Color{246, 200, 110, 255} : MENU_GOLD, LILAC = adv ? Color{222, 196, 150, 255} : MENU_LILAC;
    const Color DIM = adv ? Color{196, 176, 140, 255} : MENU_DIM, BRIGHT = adv ? Color{252, 242, 222, 255} : WHITE;
    if (m.screen == Sc::Loading) {  // the title picture, the name over it
        const Texture2D& pic = adv && menu_bg_.id ? menu_bg_ : intro_;  // (the Adventure look: the temple's gate)
        if (pic.id && !glow)
            DrawTexturePro(pic, {0, 0, float(pic.width), float(pic.height)}, {0, 0, float(W), float(H)}, {0, 0}, 0, WHITE);
        if (adv && !glow) DrawRectangleGradientV(0, H - 380, W, 380, Color{6, 8, 3, 0}, Color{6, 8, 3, 210});
        float pulse = 0.85f + 0.15f * float(std::sin(t * 2));
        if (!glow) text(m.title.c_str(), {W / 2.0f + 6, 836 + 6}, 170, Fade(adv ? Color{40, 22, 6, 255} : Color{40, 10, 80, 255}, 0.85f), true);
        text(m.title.c_str(), {W / 2.0f, 836}, 170, scale(adv ? Color{250, 205, 110, 255} : Color{235, 165, 255, 255}, pulse), true);
        if (!glow) text(m.publisher.c_str(), {W / 2.0f, 940}, 30, Fade(LILAC, 0.8f));
        float blink = 0.5f + 0.5f * float(std::sin(t * 4));
        text("PRESS ANY KEY", {W / 2.0f, 1010}, 36, Fade(WHITE, glow ? 0.5f * blink : blink));
        return;
    }
    if (adv && menu_bg_.id) {  // the temple's gate, its torches flickering, the corners dark
        DrawTexturePro(menu_bg_, {0, 0, float(menu_bg_.width), float(menu_bg_.height)}, {0, 0, float(W), float(H)}, {0, 0}, 0,
                       m.screen == Sc::Main ? Color{190, 182, 170, 255} : Color{120, 114, 106, 255});
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 2; i++) {
            const Vector2 p{i ? 1385.0f : 535.0f, 655};
            const float g = BBStudio::torch_glow(i, t);
            DrawCircleGradient(p, 170 * g, Color{255, 140, 50, uint8_t(60 * g)}, Color{255, 120, 40, 0});
            DrawCircleGradient(p, 46 * g, Color{255, 220, 150, uint8_t(80 * g)}, Color{255, 180, 80, 0});
        }
        EndBlendMode();
        DrawRectangleGradientV(0, 0, W, 330, Color{6, 8, 3, 190}, Color{6, 8, 3, 0});
        DrawRectangleGradientV(0, H - 200, W, 200, Color{6, 8, 3, 0}, Color{6, 8, 3, 200});  // (the footer reads)
        DrawRectangleGradientH(0, 0, 360, H, Color{6, 8, 3, 170}, Color{6, 8, 3, 0});
        DrawRectangleGradientH(W - 360, 0, 360, H, Color{6, 8, 3, 0}, Color{6, 8, 3, 170});
        if (m.screen != Sc::Main) DrawRectangleRounded({300, 270, W - 600.0f, 700}, 0.06f, 8, Fade(Color{12, 8, 4, 255}, 0.72f));
    } else {  // the room, dimmed: the menus float over it
        room(0, t, glow);
        DrawRectangle(0, 0, W, H, Fade(BLACK, glow ? 0.7f : 0.62f));
    }
    // the logo
    float pulse = 0.9f + 0.1f * float(std::sin(t * 2));
    // leaving for a game: the title rises away, the items part left and right (as the original's wipe)
    const float gone = m.leave < 1 ? m.leave * m.leave * (3 - 2 * m.leave) : 0, up = gone * 420, fade = 1 - gone;
    if (adv) {  // carved gold: a dark rim, the gold, a lit upper edge
        for (int dx = -4; dx <= 4; dx += 4)
            for (int dy = -2; dy <= 8; dy += 5) text(m.title.c_str(), {W / 2.0f + dx, 136 + dy - up}, 150, Fade(Color{36, 20, 4, 255}, 0.9f * fade), true);
        text(m.title.c_str(), {W / 2.0f, 136 - up}, 150, Fade(scale(Color{240, 190, 90, 255}, pulse), fade), true);
        text(m.title.c_str(), {W / 2.0f, 133 - up}, 150, Fade(Color{255, 236, 170, 255}, 0.35f * fade), true);
    } else {
        text(m.title.c_str(), {W / 2.0f + 6, 136 + 6 - up}, 150, Fade(Color{60, 20, 120, 255}, glow ? 0 : 0.8f * fade), true);
        text(m.title.c_str(), {W / 2.0f, 136 - up}, 150, Fade(scale(Color{225, 150, 255, 255}, pulse), fade), true);
    }
    if (!glow) text((m.publisher + "    ZX REMAKE").c_str(), {W / 2.0f, 232 - up}, 26, Fade(LILAC, 0.75f * fade));

    auto footer = [&](const char* s2) {
        if (!glow) text(s2, {W / 2.0f, H - 60.0f}, 28, Fade(LILAC, 0.7f));
    };
    auto title = [&](const char* s2) { text(s2, {W / 2.0f, 320}, 64, GILT); };
    switch (m.screen) {
    case Sc::Main: {
        for (int i = 0; i < 8; i++) {  // the game's six, and the remake's OPTIONS and EXIT
            float y = (adv ? 330 : 380) + i * (adv ? 62.0f : 70.0f), cx = W / 2.0f + (i % 2 ? 1 : -1) * gone * W * 0.75f;
            bool on = i == m.cursor;
            if (adv) {  // the chosen one on a wooden board; all with a dark rim to read over the picture
                const char* item = i == 5 ? (m.sound_fx ? "IN-GAME MUSIC:  OFF" : "IN-GAME MUSIC:  ON") : MENU_ITEMS[i];
                if (on && hud_board_.id) {
                    const float bw = 600, bh = 72, tw = float(hud_board_.width), th = float(hud_board_.height), e = th * 0.55f, k = bh / th;
                    const float x = cx - bw / 2, yy = y - bh / 2;
                    const Color tint = Fade(WHITE, fade);
                    DrawTexturePro(hud_board_, {0, 0, e, th}, {x, yy, e * k, bh}, {0, 0}, 0, tint);
                    DrawTexturePro(hud_board_, {e, 0, tw - 2 * e, th}, {x + e * k, yy, bw - 2 * e * k, bh}, {0, 0}, 0, tint);
                    DrawTexturePro(hud_board_, {tw - e, 0, e, th}, {x + bw - e * k, yy, e * k, bh}, {0, 0}, 0, tint);
                }
                const float size = on ? 46 : 40;
                for (int dx = -2; dx <= 2; dx += 2)
                    for (int dy = -1; dy <= 3; dy += 2) text(item, {cx + dx, y + dy}, size, Fade(Color{20, 12, 4, 255}, 0.85f * fade));
                text(item, {cx, y}, size, Fade(on ? Color{255, 222, 140, 255} : BRIGHT, fade));
                continue;
            }
            if (on) {
                float wpx = 640, hpx = 72;
                Rectangle bar{cx - wpx / 2, y - hpx / 2, wpx, hpx};
                if (!glow) DrawRectangleRounded(bar, 0.4f, 8, Fade(Color{120, 60, 220, 255}, 0.35f * fade));
                DrawRectangleRoundedLinesEx(bar, 0.4f, 8, glow ? 6 : 3, Fade(scale(Color{230, 170, 255, 255}, pulse), fade));
            }
            // the original's last item names what it would switch to (SOUND FX / MUSIC): here, what it
            // does now (the remake's own effects sound either way: it is the tune in play, on or off)
            const char* item = i == 5 ? (m.sound_fx ? "IN-GAME MUSIC:  OFF" : "IN-GAME MUSIC:  ON") : MENU_ITEMS[i];
            text(item, {cx, y}, on ? 50 : 42, Fade(on ? WHITE : (glow ? Fade(DIM, 0.4f) : DIM), fade));
        }
        if (!glow && gone == 0)
            text("Programming Richard Taylor    Graphics Jon Law    Design Clement Chambers    Sound Jay Derrett",
                 {W / 2.0f, H - 110.0f}, 24, Fade(LILAC, adv ? 0.85f : 0.6f));
        if (gone == 0) footer("UP / DOWN  choose      ENTER  select      ESC  options");
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
            if (!glow) text_left(rows[i][0], {360, y - 22}, 40, LILAC);
            text_left(rows[i][1], {900, y - 22}, 40, BRIGHT);
        }
        footer("ESC / ENTER  back");
        break;
    }
    case Sc::Keys: {
        title("REMAKE KEYS");
        const char* rows[][2] = {{"R  /  TAB", "original 1987  /  remastered"},
                                 {"L", "unlimited lives + missiles"},
                                 {"O", "original speed (slowdowns) / even"},
                                 {"T", "HD theme"},
                                 {"M  /  N", "music  /  sound effects  on / off"},
                                 {"ESC", "pause: jump to a level, sound, quit"},
                                 {"F11", "full screen"}};
        const int n = int(sizeof rows / sizeof rows[0]);
        for (int i = 0; i < n; i++) {
            float y = 400 + i * 72.0f;
            text_left(rows[i][0], {460, y - 22}, 40, GILT);
            if (!glow) text_left(rows[i][1], {800, y - 22}, 40, BRIGHT);
        }
        footer("any key  back");
        break;
    }
    case Sc::HiScores: {
        title(m.entry_row >= 0 ? "NEW HI-SCORE  -  TYPE YOUR NAME" : "HI-SCORES");
        for (size_t i = 0; i < m.scores.size() && i < 9; i++) {
            float y = 400 + float(i) * 62;
            bool on = int(i) == m.entry_row;
            Color c = on ? BRIGHT : (i == 0 ? GILT : LILAC);
            if (on && adv) {
                DrawRectangleRounded({440, y - 28, 1040, 56}, 0.4f, 8, Fade(Color{120, 80, 30, 255}, 0.55f));
                DrawRectangleRoundedLinesEx({440, y - 28, 1040, 56}, 0.4f, 8, 2.5f, scale(GILT, pulse));
            } else if (on) {
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
            Vector2 w = MeasureTextEx(lettering(false), buf, 40, 40 * 0.06f);
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
                text_left(buf, {cx + 80, ty}, 30, GILT);
                ty += 40;
            }
            std::string txt = it.text;
            for (size_t a = 0; a < txt.size();) {  // lines split at '*'
                size_t b = txt.find('*', a);
                std::string ln = txt.substr(a, b == std::string::npos ? std::string::npos : b - a);
                text_left(ln.c_str(), {cx + 80, ty}, 28, BRIGHT);
                ty += 34;
                if (b == std::string::npos) break;
                a = b + 1;
            }
        }
        for (int p = 0; p < pages; p++)  // page dots
            DrawCircle(int(W / 2.0f + (p - (pages - 1) / 2.0f) * 30), H - 110, p == page ? 8 : 5, p == page ? BRIGHT : DIM);
        footer(m.instructions_auto
                   ? (m.instructions_page >= 0 ? "LEFT / RIGHT  pages  /  auto paging off  /  ESC  back"
                                               : "LEFT / RIGHT  pages  /  automatic pages  /  ESC  back")
                   : "LEFT / RIGHT  pages  /  ESC / ENTER  back");
        break;
    }
    default: break;
    }
}
