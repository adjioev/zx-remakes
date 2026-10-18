#include "bb_studio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <rlgl.h>

#include "jpeg.h"

namespace {

constexpr float ROOM_X = BB_ROOM_WIDTH, ROOM_Z = 148, WALL_H = 64, LIP = 8, CUBE = 12;
constexpr float BEVEL = 1.3f;

struct V3 {
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
V3 norm(V3 a) {
    const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 0 ? a * (1 / l) : a;
}

// towards the light: from the upper left (+Z), a little from the front (-X)
const V3 LIGHT = norm({-0.35f, 1.0f, 0.55f});
// the sun's shadows: cast to the right and a little forward, onto the open floor where they show
// (cast along LIGHT they fall behind the bricks, hidden)
const V3 SUN_CAST = norm({0.12f, 1.0f, 0.7f});

const char* VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 wpos;
out vec3 wnormal;
out vec4 col;
out vec2 uv;
void main() {
    wpos = vertexPosition;
    wnormal = vertexNormal;
    col = vertexColor;
    uv = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

// World-space light (the projection is oblique: no view space to light in). Diffuse + hemisphere
// ambient + the torches + Blinn specular + a little rim; metal reflects a made-up temple (warm stone,
// the sun, fire low down). flat: the colour as it is (shadows, flames; the floor with its reflection).
const char* FS = R"(#version 330
in vec3 wpos;
in vec3 wnormal;
in vec4 col;
in vec2 uv;
uniform sampler2D texture0;
uniform vec3 lightDir;
uniform float metal;
uniform float gloss;
uniform float emit;
uniform float flatMode;
uniform vec4 torch[4];  // the torches' flames: where, how bright
uniform sampler2D stoneTex;
uniform float stone;
uniform sampler2D engraveTex;
uniform float engrave;  // 1: the ball (its surface as it has rolled: spin), 2: the bat (in the room)
uniform mat4 spin;
uniform float exposure;  // the whole lit picture (darker over a painted room)
// the world: its lights' colour, the tint of its air, its bricks' glowing cracks, its water's play
uniform vec3 fireCol;
uniform vec3 airTint;
uniform vec3 crackGlow;
uniform float caustics;
uniform float moss;
uniform float time;
out vec4 finalColor;
const vec3 V = normalize(vec3(-2.0, 1.0, -2.0));
float caustic(vec2 p) {  // light through moving water: bright wandering threads
    vec2 q = p * 0.09;
    float c = 0.0;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        q += vec2(sin(q.y * 1.7 + time * 0.8 + fi * 2.1), cos(q.x * 1.5 - time * 0.6 + fi * 1.3)) * 0.6;
        c += 0.05 / (0.06 + abs(sin(q.x * 2.0 + q.y * 1.3 + fi)));
    }
    return clamp(pow(c / 3.0, 2.0), 0.0, 1.5);
}
vec3 env(vec3 r) {  // the temple as a shiny thing sees it: warm stone, the sun from above, fire low
    if (r.y > 0.0) {
        vec3 sky = mix(vec3(1.0, 0.9, 0.72), vec3(0.72, 0.78, 0.66), clamp(r.y * 1.4, 0.0, 1.0));  // sun, the jungle's green
        float box = pow(max(dot(r, lightDir), 0.0), 40.0);
        return sky + vec3(1.5, 1.3, 1.0) * box;
    }
    vec3 fl = vec3(0.86, 0.74, 0.56) * (0.75 + 0.25 * r.y);
    float band = smoothstep(0.12, 0.0, -r.y);
    return mix(fl, vec3(1.0, 0.55, 0.15), band * 0.6);
}
vec3 fire(vec3 n) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        vec3 l = torch[i].xyz - wpos;
        float d = length(l);
        sum += fireCol * torch[i].w * max(dot(n, l / max(d, 0.001)), 0.0) / (1.0 + d * d / 260.0);
    }
    return sum;
}
void main() {
    if (flatMode > 2.5) {  // the water's light over a painted floor or wall (added)
        finalColor = vec4(vec3(0.45, 0.85, 1.0) * caustic(wpos.xz + vec2(wpos.y * 0.7)) * caustics * 0.35, 1.0);
        return;
    }
    if (flatMode > 0.5) {
        vec4 t = texture(texture0, uv);
        if (flatMode > 1.5) {  // the floor: its colour, the reflection over it, the shadows (alpha)
            vec3 c = col.rgb * (1.0 + fire(vec3(0.0, 1.0, 0.0)));
            finalColor = vec4(mix(c, t.rgb, 0.22 * t.a), 1.0);
            return;
        }
        finalColor = col;
        return;
    }
    vec3 n = normalize(wnormal);
    float diff = max(dot(n, lightDir), 0.0);
    vec3 amb = mix(vec3(0.34, 0.30, 0.28), vec3(0.56, 0.52, 0.46), n.y * 0.5 + 0.5) * airTint;
    vec3 h = normalize(lightDir + V);
    float spec = pow(max(dot(n, h), 0.0), 8.0 + gloss * 120.0) * (0.15 + 0.9 * gloss);
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.25;
    vec3 base = col.rgb;
    vec3 glow = vec3(0.0);  // light of its own (a volcano's bricks: their cracks smoulder)
    if (stone > 0.5) {  // one of four damaged faces (an atlas), by the brick and the side, mirrored or not
        vec3 a = abs(n);
        float side = a.x > a.y && a.x > a.z ? 1.0 : a.y > a.z ? 2.0 : 3.0;
        vec2 st = side < 1.5 ? wpos.zy : side < 2.5 ? wpos.xz : wpos.xy;
        float hsh = fract(sin(dot(floor((wpos - n) / 12.0), vec3(12.9898, 78.233, 37.719)) + side * 4.1) * 43758.5453);
        float tile = floor(hsh * 4.0);
        vec2 f = fract(st / 12.0);
        if (fract(hsh * 8.0) > 0.5) f.x = 1.0 - f.x;
        vec2 uv = vec2(mod(tile, 2.0), floor(tile / 2.0)) * 0.5 + 0.03 + f * 0.44;
        float l = dot(texture(stoneTex, uv).rgb, vec3(0.333));
        base *= clamp(1.12 + (l - 0.78) * 2.4, 0.2, 1.4);  // (its grain, cracks and chips, strongly)
        float hot = 1.0 - smoothstep(0.4, 0.62, l);
        glow = crackGlow * hot * (0.65 + 0.35 * sin(time * 1.6 + hsh * 40.0));
        if (n.y > 0.7 && moss > 0.5) {  // moss in the cracks on top of some
            float m = step(0.62, fract(hsh * 17.0)) * (1.0 - smoothstep(0.45, 0.75, l));
            base = mix(base, vec3(0.22, 0.33, 0.10), m * 0.85);
        }
    }
    float groove = 1.0;  // engraved lines: dark, no mirror
    if (engrave > 0.5) {
        vec3 q = engrave > 1.5 ? wpos / 26.0 : (spin * vec4(n, 0.0)).xyz * 0.16;
        vec3 w = pow(abs(engrave > 1.5 ? n : (spin * vec4(n, 0.0)).xyz), vec3(4.0));
        w /= w.x + w.y + w.z;
        float e = w.x * texture(engraveTex, fract(q.zy)).r + w.y * texture(engraveTex, fract(q.xz)).r
                + w.z * texture(engraveTex, fract(q.xy)).r;
        groove = smoothstep(0.3, 0.7, e);
        base *= mix(0.12, 1.0, groove);
        spec *= groove;
    }
    vec3 lit = base * (amb + diff * vec3(1.0, 0.92, 0.80) * airTint * 0.85 + fire(n)) + vec3(spec + rim * 0.5);
    if (caustics > 0.0) lit += base * vec3(0.5, 0.9, 1.0) * caustic(wpos.xz + vec2(wpos.y * 0.7)) * caustics * (0.35 + 0.65 * max(n.y, 0.0));
    if (metal > 0.0) {
        vec3 r = reflect(-V, n);
        vec3 m = env(r) * airTint * mix(vec3(1.0), base, 0.8) * (0.6 + 0.4 * diff) + base * fire(n) + vec3(spec * 1.4);
        lit = mix(lit, m, metal * mix(0.15, 1.0, groove));
    }
    lit += base * emit;
    finalColor = vec4(lit * exposure + glow, col.a);
})";

// A pass's way with the points: as they are, mirrored under the floor, or flattened onto it along
// the light (a shadow): the sun's (parallel) or a torch's (from g_lamp, onto the floor's own area).
// Each shadow lies at its own height g_shadow_y: with depth writes on, it darkens a spot once only.
enum class Pass { Normal, Reflect, Shadow };
Pass g_pass = Pass::Normal;
Color g_shadow{0, 0, 0, 255};
float g_shadow_y = 0.02f;
bool g_lamp_on = false;
V3 g_lamp{};

struct ShadowPoint {
    V3 p;
    float a;  // its alpha
};
ShadowPoint g_tri[3];
int g_tri_n = 0;

// a shadow's triangle, cut to the floor (else it would lie over the painted walls and the front edge)
void shadow_tri() {
    std::vector<ShadowPoint> poly(g_tri, g_tri + 3), out;
    auto clip = [&](auto inside, auto cross) {
        out.clear();
        for (size_t i = 0; i < poly.size(); i++) {
            const ShadowPoint &u = poly[i], &v = poly[(i + 1) % poly.size()];
            const bool iu = inside(u.p), iv = inside(v.p);
            if (iu) out.push_back(u);
            if (iu != iv) {
                const float t = cross(u.p, v.p);
                out.push_back({u.p + (v.p - u.p) * t, u.a + (v.a - u.a) * t});
            }
        }
        poly.swap(out);
    };
    clip([](V3 q) { return q.x >= 0; }, [](V3 u, V3 v) { return u.x / (u.x - v.x); });
    clip([](V3 q) { return q.x <= ROOM_X; }, [](V3 u, V3 v) { return (ROOM_X - u.x) / (v.x - u.x); });
    clip([](V3 q) { return q.z >= 0; }, [](V3 u, V3 v) { return u.z / (u.z - v.z); });
    clip([](V3 q) { return q.z <= ROOM_Z; }, [](V3 u, V3 v) { return (ROOM_Z - u.z) / (v.z - u.z); });
    for (size_t i = 1; i + 1 < poly.size(); i++)
        for (const ShadowPoint* q : {&poly[0], &poly[i], &poly[i + 1]}) {
            rlColor4ub(g_shadow.r, g_shadow.g, g_shadow.b, uint8_t(std::clamp(q->a, 0.0f, 255.0f)));
            rlNormal3f(0, 1, 0);
            rlVertex3f(q->p.x, g_shadow_y, q->p.z);
        }
}

void vtx(V3 p, V3 n, Color c) {
    if (g_pass == Pass::Reflect) p.y = -p.y, n.y = -n.y;
    if (g_pass == Pass::Shadow) {
        float a = g_shadow.a;
        if (g_lamp_on) {
            const V3 q{p.x, std::min(p.y, g_lamp.y - 3), p.z};
            const float k = std::min(2.0f, g_lamp.y / (g_lamp.y - std::max(q.y, 0.0f)));
            p = g_lamp + (q - g_lamp) * k;
            // fading with the distance from the torch (along the floor)
            a *= std::clamp(1.2f - std::hypot(p.x - g_lamp.x, p.z - g_lamp.z) / 60, 0.0f, 1.0f);
        } else {
            p = p - SUN_CAST * (std::max(p.y, 0.0f) / SUN_CAST.y);
        }
        g_tri[g_tri_n++] = {p, a};
        if (g_tri_n == 3) g_tri_n = 0, shadow_tri();
        return;
    }
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(p.x, p.y, p.z);
}

void tri(V3 a, V3 b, V3 c, V3 n, Color col) { vtx(a, n, col), vtx(b, n, col), vtx(c, n, col); }
void quad(V3 a, V3 b, V3 c, V3 d, V3 n, Color col) { tri(a, b, c, n, col), tri(a, c, d, n, col); }
V3 face_n(V3 a, V3 b, V3 c) {
    const V3 u = b - a, v = c - a;
    return norm({u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x});
}

// A box with chamfered edges (b): its six faces, the twelve edge strips, the eight corners
void box(float x0, float x1, float y0, float y1, float z0, float z1, Color c, float b = BEVEL) {
    b = std::min({b, (x1 - x0) / 3, (y1 - y0) / 3, (z1 - z0) / 3});
    const float X0 = x0 + b, X1 = x1 - b, Y0 = y0 + b, Y1 = y1 - b, Z0 = z0 + b, Z1 = z1 - b;
    // faces
    quad({x0, Y0, Z0}, {x0, Y1, Z0}, {x0, Y1, Z1}, {x0, Y0, Z1}, {-1, 0, 0}, c);
    quad({x1, Y0, Z0}, {x1, Y0, Z1}, {x1, Y1, Z1}, {x1, Y1, Z0}, {1, 0, 0}, c);
    quad({X0, y0, Z0}, {X0, y0, Z1}, {X1, y0, Z1}, {X1, y0, Z0}, {0, -1, 0}, c);
    quad({X0, y1, Z0}, {X1, y1, Z0}, {X1, y1, Z1}, {X0, y1, Z1}, {0, 1, 0}, c);
    quad({X0, Y0, z0}, {X1, Y0, z0}, {X1, Y1, z0}, {X0, Y1, z0}, {0, 0, -1}, c);
    quad({X0, Y0, z1}, {X0, Y1, z1}, {X1, Y1, z1}, {X1, Y0, z1}, {0, 0, 1}, c);
    // edge strips along X, Y, Z
    const float s = 0.7071f;
    for (int sy = 0; sy < 2; sy++)
        for (int sz = 0; sz < 2; sz++) {  // along X
            const float y = sy ? y1 : y0, yi = sy ? Y1 : Y0, z = sz ? z1 : z0, zi = sz ? Z1 : Z0;
            quad({X0, yi, z}, {X1, yi, z}, {X1, y, zi}, {X0, y, zi}, {0, sy ? s : -s, sz ? s : -s}, c);
        }
    for (int sx = 0; sx < 2; sx++)
        for (int sz = 0; sz < 2; sz++) {  // along Y
            const float x = sx ? x1 : x0, xi = sx ? X1 : X0, z = sz ? z1 : z0, zi = sz ? Z1 : Z0;
            quad({xi, Y0, z}, {x, Y0, zi}, {x, Y1, zi}, {xi, Y1, z}, {sx ? s : -s, 0, sz ? s : -s}, c);
        }
    for (int sx = 0; sx < 2; sx++)
        for (int sy = 0; sy < 2; sy++) {  // along Z
            const float x = sx ? x1 : x0, xi = sx ? X1 : X0, y = sy ? y1 : y0, yi = sy ? Y1 : Y0;
            quad({xi, y, Z0}, {x, yi, Z0}, {x, yi, Z1}, {xi, y, Z1}, {sx ? s : -s, sy ? s : -s, 0}, c);
        }
    const float k = 0.577f;
    for (int sx = 0; sx < 2; sx++)
        for (int sy = 0; sy < 2; sy++)
            for (int sz = 0; sz < 2; sz++) {  // corners
                const float x = sx ? x1 : x0, xi = sx ? X1 : X0, y = sy ? y1 : y0, yi = sy ? Y1 : Y0;
                const float z = sz ? z1 : z0, zi = sz ? Z1 : Z0;
                tri({x, yi, zi}, {xi, y, zi}, {xi, yi, z}, {sx ? k : -k, sy ? k : -k, sz ? k : -k}, c);
            }
}

void pyramid(float x0, float x1, float y0, float y1, float z0, float z1, Color c) {
    const V3 apex{(x0 + x1) / 2, y1, (z0 + z1) / 2};
    const V3 a{x0, y0, z0}, b{x1, y0, z0}, cc{x1, y0, z1}, d{x0, y0, z1};
    for (auto [p, q] : {std::pair{a, b}, {b, cc}, {cc, d}, {d, a}}) tri(p, q, apex, face_n(p, q, apex) * -1, c);
}

void diamond(float x0, float x1, float y0, float y1, float z0, float z1, Color c) {
    const float ym = (y0 + y1) / 2;
    const V3 top{(x0 + x1) / 2, y1, (z0 + z1) / 2}, bot{top.x, y0, top.z};
    const V3 a{x0, ym, z0}, b{x1, ym, z0}, cc{x1, ym, z1}, d{x0, ym, z1};
    for (auto [p, q] : {std::pair{a, b}, {b, cc}, {cc, d}, {d, a}}) {
        tri(p, q, top, face_n(p, q, top) * -1, c);
        tri(q, p, bot, face_n(q, p, bot) * -1, c);
    }
}

// A sphere that looks round on the screen (the projection is oblique: a true one comes out an egg):
// its points are laid out along the screen's right and up, its normals those of a sphere seen head on
void round_ball(V3 c, float r, Color col, int seg = 22) {
    const V3 D = norm({2, -1, 2});                    // into the picture
    const V3 Ru{4.f / 9, -2.f / 9, -5.f / 9};         // one pixel right (and square to D)
    const V3 Uv{2.f / 9, 8.f / 9, 2.f / 9};           // one pixel up (and square to D)
    const V3 R = norm(Ru), U = norm(Uv - R * (Uv.x * R.x + Uv.y * R.y + Uv.z * R.z)), B = D * -1;
    for (int i = 0; i < seg; i++)
        for (int j = 0; j < seg * 2; j++) {
            auto p = [&](int a, int b) {
                const float th = PI * float(a) / float(seg), ph = 2 * PI * float(b) / float(seg * 2);
                return V3{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
            };
            auto v = [&](V3 q) {  // q: x right, y up, z towards the eye
                vtx(c + (Ru * q.x + Uv * q.y + B * q.z) * r, norm(R * q.x + U * q.y + B * q.z), col);
            };
            const V3 n0 = p(i, j), n1 = p(i + 1, j), n2 = p(i + 1, j + 1), n3 = p(i, j + 1);
            v(n0), v(n1), v(n2), v(n0), v(n2), v(n3);
        }
}

// A band round such a ball, turned with it (spin: the ball's turn, row-major, ball -> room): w its
// half width in radii. (Its far half hides behind the ball.)
void ball_band(V3 c, float r, float w, Color col, const float* spin, int seg = 64) {
    const V3 Ru{4.f / 9, -2.f / 9, -5.f / 9}, Uv{2.f / 9, 8.f / 9, 2.f / 9}, B = norm({-2, 1, -2});
    const V3 R = norm(Ru), U = norm(Uv - R * (Uv.x * R.x + Uv.y * R.y + Uv.z * R.z));
    auto room = [&](V3 p) {  // from the ball's frame into the room's
        return V3{spin[0] * p.x + spin[1] * p.y + spin[2] * p.z, spin[3] * p.x + spin[4] * p.y + spin[5] * p.z,
                  spin[6] * p.x + spin[7] * p.y + spin[8] * p.z};
    };
    auto at = [&](float a, float off) {  // a point of the band (round the ball's equator), a unit vector
        const float k = std::sqrt(std::max(0.0f, 1 - off * off));
        return room({std::cos(a) * k, off, std::sin(a) * k});
    };
    auto v = [&](V3 d) {  // laid out as the round ball is: by the screen's right and up
        const V3 q{d.x * R.x + d.y * R.y + d.z * R.z, d.x * U.x + d.y * U.y + d.z * U.z, d.x * B.x + d.y * B.y + d.z * B.z};
        vtx(c + (Ru * q.x + Uv * q.y + B * q.z) * r, d, col);
    };
    for (int i = 0; i < seg; i++) {
        const float a0 = 2 * PI * float(i) / float(seg), a1 = 2 * PI * float(i + 1) / float(seg);
        const V3 p0 = at(a0, -w), p1 = at(a1, -w), p2 = at(a1, w), p3 = at(a0, w);
        v(p0), v(p1), v(p2), v(p0), v(p2), v(p3);
    }
}

// a cylinder from a to b (axis X or Y), radius r
void cylinder(V3 a, V3 b, float r, Color col, bool along_x, int seg = 20) {
    for (int i = 0; i < seg; i++) {
        const float t0 = 2 * PI * float(i) / float(seg), t1 = 2 * PI * float(i + 1) / float(seg);
        auto off = [&](float t) { return along_x ? V3{0, std::cos(t), std::sin(t)} : V3{std::cos(t), 0, std::sin(t)}; };
        const V3 n0 = off(t0), n1 = off(t1);
        quad(a + n0 * r, b + n0 * r, b + n1 * r, a + n1 * r, norm(n0 + n1), col);
        const V3 axis = norm(b - a);
        tri(a, a + n1 * r, a + n0 * r, axis * -1, col);
        tri(b, b + n0 * r, b + n1 * r, axis, col);
    }
}

// a small sphere (true geometry: at a brick's size its egg shape does not show)
void sphere(V3 c, float r, Color col, int seg = 12) {
    auto p = [&](int a, int b) {
        const float th = PI * float(a) / float(seg), ph = 2 * PI * float(b) / float(seg * 2);
        return V3{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
    };
    for (int i = 0; i < seg; i++)
        for (int j = 0; j < seg * 2; j++) {
            const V3 n0 = p(i, j), n1 = p(i + 1, j), n2 = p(i + 1, j + 1), n3 = p(i, j + 1);
            vtx(c + n0 * r, n0, col), vtx(c + n1 * r, n1, col), vtx(c + n2 * r, n2, col);
            vtx(c + n0 * r, n0, col), vtx(c + n2 * r, n2, col), vtx(c + n3 * r, n3, col);
        }
}

// faces of a solid around centre m: their normals turned outwards whatever the order of the corners
void out_tri(V3 m, V3 a, V3 b, V3 c, Color col) {
    V3 n = face_n(a, b, c);
    const V3 f = (a + b + c) * (1.0f / 3) - m;
    if (n.x * f.x + n.y * f.y + n.z * f.z < 0) n = n * -1;
    tri(a, b, c, n, col);
}
void out_quad(V3 m, V3 a, V3 b, V3 c, V3 d, Color col) { out_tri(m, a, b, c, col), out_tri(m, a, c, d, col); }

Color shade(Color c, float k) {
    return {uint8_t(std::min(255.f, c.r * k)), uint8_t(std::min(255.f, c.g * k)), uint8_t(std::min(255.f, c.b * k)), c.a};
}

float hash(int a, int b) {  // 0..1, the same for the same a, b
    uint32_t x = uint32_t(a) * 374761393u + uint32_t(b) * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u;
    return float((x ^ (x >> 16)) & 0xFFFFFF) / float(0xFFFFFF);
}

}  // namespace

Texture2D bb_load_art(const std::string& base) {
    if (FileExists((base + ".png").c_str())) return LoadTexture((base + ".png").c_str());
    int size = 0, w = 0, h = 0;
    unsigned char* file = FileExists((base + ".jpg").c_str()) ? LoadFileData((base + ".jpg").c_str(), &size) : nullptr;
    if (!file) return {};
    unsigned char* pixels = jpeg_decode(file, size, &w, &h);
    UnloadFileData(file);
    if (!pixels) return {};
    const Image im{pixels, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8};
    const Texture2D tex = LoadTextureFromImage(im);
    jpeg_free(pixels);
    return tex;
}

const BBWorldStyle& bb_world_style(BBWorld w) {
    // name, folder, the lights' colour (shader), their glow (2D, outer and inner), the painting's tint,
    // the shadows' colour, the air's tint, exposure, the bricks' gloss, their cracks' glow, the water
    static const BBWorldStyle STYLES[BB_WORLDS] = {
        {"JUNGLE TEMPLE", "jungle", {1.0f, 0.58f, 0.22f}, {255, 140, 50, 255}, {255, 220, 150, 255}, {168, 160, 146, 255},
         {10, 6, 2, 255}, {1.0f, 1.0f, 1.0f}, 0.78f, 0.4f, {0, 0, 0}, 0},
        {"TOMB OF THE PHARAOH", "tomb", {1.0f, 0.62f, 0.25f}, {255, 150, 60, 255}, {255, 225, 160, 255}, {172, 160, 140, 255},
         {12, 6, 0, 255}, {1.05f, 0.95f, 0.8f}, 0.8f, 0.35f, {0, 0, 0}, 0},
        {"ATLANTIS", "atlantis", {0.3f, 0.95f, 0.85f}, {60, 220, 200, 255}, {180, 255, 240, 255}, {150, 168, 172, 255},
         {2, 10, 16, 255}, {0.72f, 0.92f, 1.0f}, 0.78f, 0.5f, {0, 0, 0}, 1},
        {"ICE CAVERN", "ice", {0.55f, 0.8f, 1.0f}, {110, 180, 255, 255}, {220, 240, 255, 255}, {176, 182, 192, 255},
         {8, 18, 40, 255}, {0.86f, 0.94f, 1.08f}, 0.86f, 0.85f, {0, 0, 0}, 0},
        {"CHERRY GARDEN", "garden", {1.0f, 0.6f, 0.35f}, {255, 150, 80, 255}, {255, 220, 170, 255}, {172, 162, 160, 255},
         {16, 6, 10, 255}, {1.0f, 0.9f, 0.92f}, 0.8f, 0.45f, {0, 0, 0}, 0},
        {"HAUNTED CASTLE", "castle", {0.45f, 1.0f, 0.55f}, {90, 255, 140, 255}, {200, 255, 210, 255}, {150, 158, 170, 255},
         {4, 8, 14, 255}, {0.75f, 0.85f, 1.0f}, 0.74f, 0.35f, {0, 0, 0}, 0},
        {"CLOCKWORK FOUNDRY", "steam", {1.0f, 0.7f, 0.35f}, {255, 170, 80, 255}, {255, 230, 170, 255}, {170, 158, 146, 255},
         {12, 6, 0, 255}, {1.05f, 0.92f, 0.78f}, 0.78f, 0.7f, {0, 0, 0}, 0},
        {"VOLCANO", "volcano", {1.0f, 0.42f, 0.12f}, {255, 90, 20, 255}, {255, 200, 120, 255}, {168, 150, 140, 255},
         {8, 2, 0, 255}, {1.05f, 0.78f, 0.68f}, 0.74f, 0.35f, {1.3f, 0.45f, 0.08f}, 0},
        {"NEON CITY", "cyber", {1.0f, 0.35f, 0.85f}, {255, 60, 200, 255}, {255, 190, 240, 255}, {160, 155, 175, 255},
         {6, 2, 16, 255}, {0.85f, 0.8f, 1.1f}, 0.78f, 0.75f, {0, 0, 0}, 0},
        {"ALIEN WORLD", "alien", {0.55f, 1.0f, 0.5f}, {120, 255, 120, 255}, {230, 255, 220, 255}, {165, 155, 175, 255},
         {8, 2, 16, 255}, {0.95f, 0.85f, 1.1f}, 0.78f, 0.5f, {0.2f, 0.9f, 0.4f}, 0},
    };
    return STYLES[int(w)];
}

BBStudio::BBStudio(const std::string& dir) : dir_(dir) {
    if (FileExists((dir + "/engrave.png").c_str())) {
        engrave_ = LoadTexture((dir + "/engrave.png").c_str());
        GenTextureMipmaps(&engrave_);
        SetTextureFilter(engrave_, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(engrave_, TEXTURE_WRAP_REPEAT);
    }
    set_world(BBWorld::Jungle);
    shader_ = LoadShaderFromMemory(VS, FS);
    loc_light_ = GetShaderLocation(shader_, "lightDir");
    loc_metal_ = GetShaderLocation(shader_, "metal");
    loc_gloss_ = GetShaderLocation(shader_, "gloss");
    loc_emit_ = GetShaderLocation(shader_, "emit");
    loc_flat_ = GetShaderLocation(shader_, "flatMode");
    loc_torch_ = GetShaderLocation(shader_, "torch");
    loc_stone_ = GetShaderLocation(shader_, "stone");
    loc_stone_tex_ = GetShaderLocation(shader_, "stoneTex");
    loc_engrave_ = GetShaderLocation(shader_, "engrave");
    loc_engrave_tex_ = GetShaderLocation(shader_, "engraveTex");
    loc_spin_ = GetShaderLocation(shader_, "spin");
    loc_exposure_ = GetShaderLocation(shader_, "exposure");
    loc_fire_ = GetShaderLocation(shader_, "fireCol");
    loc_air_ = GetShaderLocation(shader_, "airTint");
    loc_crack_ = GetShaderLocation(shader_, "crackGlow");
    loc_caustics_ = GetShaderLocation(shader_, "caustics");
    loc_time_ = GetShaderLocation(shader_, "time");
    loc_moss_ = GetShaderLocation(shader_, "moss");
}

void BBStudio::set_world(BBWorld w) {
    // its painted room and its bricks, loaded the first time it is needed
    const int i = int(w);
    if (!tried_[i]) {
        tried_[i] = true;
        const std::string d = dir_ + "/" + bb_world_style(w).dir;
        if ((rooms_[i] = bb_load_art(d + "/room")).id) SetTextureFilter(rooms_[i], TEXTURE_FILTER_BILINEAR);
        if ((creatures_[i] = bb_load_art(d + "/creatures")).id) {
            GenTextureMipmaps(&creatures_[i]);
            SetTextureFilter(creatures_[i], TEXTURE_FILTER_TRILINEAR);
        }
        if ((bricks_[i] = bb_load_art(d + "/bricks")).id) {
            GenTextureMipmaps(&bricks_[i]);
            SetTextureFilter(bricks_[i], TEXTURE_FILTER_TRILINEAR);
        }
    }
    world_ = w, room_ = rooms_[i], stone_ = bricks_[i];
}

BBStudio::~BBStudio() {
    if (shader_.id) UnloadShader(shader_);
    if (refl_.id) UnloadRenderTexture(refl_);
    if (shade_.id) UnloadRenderTexture(shade_);
    for (int i = 0; i < BB_WORLDS; i++) {
        if (rooms_[i].id) UnloadTexture(rooms_[i]);
        if (bricks_[i].id) UnloadTexture(bricks_[i]);
        if (creatures_[i].id) UnloadTexture(creatures_[i]);
    }
    if (engrave_.id) UnloadTexture(engrave_);
}

void BBStudio::material(float metal, float gloss, float emit, float flat, float stone, float engrave) {
    if (g_pass == Pass::Shadow) metal = 0, gloss = 0, emit = 0, flat = 1, stone = 0, engrave = 0;  // shadows: unlit
    if (!stone_.id) stone = 0;
    if (!engrave_.id) engrave = 0;
    if (metal == metal_ && gloss == gloss_ && emit == emit_ && flat == flat_ && stone == stone_on_ && engrave == engrave_on_) return;
    // (a flush inside rlBegin would leave the rest drawn as quads: close it, flush, open again)
    rlEnd();
    rlDrawRenderBatchActive();
    metal_ = metal, gloss_ = gloss, emit_ = emit, flat_ = flat;
    SetShaderValue(shader_, loc_metal_, &metal, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_gloss_, &gloss, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_emit_, &emit, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_flat_, &flat, SHADER_UNIFORM_FLOAT);
    stone_on_ = stone;
    SetShaderValue(shader_, loc_stone_, &stone, SHADER_UNIFORM_FLOAT);
    if (stone > 0) SetShaderValueTexture(shader_, loc_stone_tex_, stone_);  // (a flush forgets it)
    engrave_on_ = engrave;
    SetShaderValue(shader_, loc_engrave_, &engrave, SHADER_UNIFORM_FLOAT);
    if (engrave > 0) SetShaderValueTexture(shader_, loc_engrave_tex_, engrave_);
    rlBegin(RL_TRIANGLES);
}

void BBStudio::begin3d(float S, int tw, int th, Vector2 at) {
    // the playfield: across 160 + X - Z, down 118 - X / 2 - Y (times S); deeper along (2, -1, 2)
    if (at.x < 0) at = {160 * S, 118 * S + DROP};
    const float m[16] = {S, -S / 2, -S, 0, 0, -S, S / 2, 0, -S, 0, -S, 0, at.x, at.y, 0, 1};
    rlDrawRenderBatchActive();
    // (not the matrix stack: rlgl keeps one stack for both, and a pushed modelview moves to the CPU)
    saved_projection_ = rlGetMatrixProjection();
    saved_modelview_ = rlGetMatrixModelview();
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, double(BBRenderer::W), double(BBRenderer::H), 0, -6000, 6000);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlMultMatrixf(m);
    rlViewport(0, 0, tw, th);
    rlEnableDepthTest();
    rlDisableBackfaceCulling();
    BeginShaderMode(shader_);
    metal_ = gloss_ = emit_ = flat_ = stone_on_ = engrave_on_ = -1;
    // the ball's turn: its surface as it has rolled (inverse: from the room into the ball)
    const Matrix sp{spin_[0], spin_[3], spin_[6], 0, spin_[1], spin_[4], spin_[7], 0, spin_[2], spin_[5], spin_[8], 0, 0, 0, 0, 1};
    SetShaderValueMatrix(shader_, loc_spin_, sp);
    const BBWorldStyle& ws = bb_world_style(world_);
    const float exposure = room_.id ? ws.exposure : 1.0f, caustics = room_.id ? ws.caustics : 0, time = float(t_);
    const Vector3 air = room_.id ? ws.air : Vector3{1, 1, 1}, crack = room_.id ? ws.crack_glow : Vector3{0, 0, 0};
    SetShaderValue(shader_, loc_exposure_, &exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_fire_, &ws.light, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader_, loc_air_, &air, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader_, loc_crack_, &crack, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader_, loc_caustics_, &caustics, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_time_, &time, SHADER_UNIFORM_FLOAT);
    const float moss = world_ == BBWorld::Jungle || world_ == BBWorld::Atlantis || world_ == BBWorld::Garden ||
                               world_ == BBWorld::Castle ? 1.0f : 0.0f;  // (sea moss: weed)
    SetShaderValue(shader_, loc_moss_, &moss, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_light_, &LIGHT, SHADER_UNIFORM_VEC3);
    float tv[16];
    for (int i = 0; i < 4; i++) {
        const Vector3 f = torch(i);
        tv[i * 4] = f.x, tv[i * 4 + 1] = f.y, tv[i * 4 + 2] = f.z, tv[i * 4 + 3] = torch_glow(i, t_);
    }
    SetShaderValueV(shader_, loc_torch_, tv, SHADER_UNIFORM_VEC4, 4);
    rlBegin(RL_TRIANGLES);
}

void BBStudio::roll(const BBState& s) {
    // turn the ball by how far it went (rolling on the floor: about the axis across its way)
    for (const BBObject& o : s.objects) {
        if (o.slot != 0) continue;
        const Vector3 c = ball_at(o);
        const float dx = c.x - roll_from_.x, dz = c.z - roll_from_.z, d = std::hypot(dx, dz);
        roll_from_ = c;
        if (d < 1e-4f || d > 10) return;  // (still, or a new ball)
        const V3 a{dz / d, 0, -dx / d};
        const float th = d / BB_BALL_RADIUS, cs = std::cos(th), sn = std::sin(th), k = 1 - cs;
        const float r[9] = {cs + a.x * a.x * k, a.x * a.y * k - a.z * sn, a.x * a.z * k + a.y * sn,
                            a.y * a.x * k + a.z * sn, cs + a.y * a.y * k, a.y * a.z * k - a.x * sn,
                            a.z * a.x * k - a.y * sn, a.z * a.y * k + a.x * sn, cs + a.z * a.z * k};
        float m[9];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) m[i * 3 + j] = r[i * 3] * spin_[j] + r[i * 3 + 1] * spin_[3 + j] + r[i * 3 + 2] * spin_[6 + j];
        // keep it a rotation: Gram-Schmidt on the columns
        V3 c0{m[0], m[3], m[6]}, c1{m[1], m[4], m[7]};
        c0 = norm(c0);
        c1 = norm(c1 - c0 * (c0.x * c1.x + c0.y * c1.y + c0.z * c1.z));
        const V3 c2{c0.y * c1.z - c0.z * c1.y, c0.z * c1.x - c0.x * c1.z, c0.x * c1.y - c0.y * c1.x};
        const float out[9] = {c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z};
        std::copy(out, out + 9, spin_);
        return;
    }
}

Vector3 BBStudio::ball_at(const BBObject& o) {
    // the rendered centre (object coordinates are sprite anchors; the height is in sy)
    // (the sprite's centre is 4 over the floor: the sphere's, its radius 6, lest it sink into it)
    const float x = o.x + 5;
    return {x, 118 - x / 2 - (o.sy + 10) + 2, 160 + x - (o.sx + 12)};
}

Vector3 BBStudio::torch(int i) {
    // two on the back wall, two on the left one: the flame's middle, out from the wall
    constexpr float Y = 40, OUT = 3.5f;
    switch (i) {
    case 0: return {ROOM_X - OUT, Y, 44};
    case 1: return {ROOM_X - OUT, Y, 108};
    case 2: return {26, Y, ROOM_Z - OUT};
    default: return {66, Y, ROOM_Z - OUT};
    }
}

float BBStudio::torch_glow(int i, double t) {
    // (slow waves: a quick flicker reads as blinking light)
    return float(1.0 + 0.10 * std::sin(t * 3.7 + i * 1.7) + 0.06 * std::sin(t * 6.1 + i * 4.1) + 0.03 * std::sin(t * 9.4 + i));
}

void BBStudio::end3d() {
    rlEnd();
    rlDrawRenderBatchActive();
    EndShaderMode();
    rlDisableDepthTest();
    rlSetMatrixProjection(saved_projection_);
    rlSetMatrixModelview(saved_modelview_);
}

void BBStudio::things(const BBState& s, const LookFn& look, const BatFn& bat_body, const SlabFn& slab, bool shadows) {
    // the things in the room (bricks, the bat, the ball), in the current pass
    for (const BBObject& o : s.objects) {
        if (o.slot >= 3 && o.sprite >= 0x11 && o.sprite <= 0x1F) continue;  // creatures, explosions: 2D
        if (o.slot == 2) continue;
        if (o.slot == 1) {
            const BBPaddleBody b = bat_body(o);
            if (engrave_.id) {  // engraved steel, a thin neon line along it
                material(0.85f, 0.8f, 0, 0, 0, 2);  // (a fine bevel: a broad one shows a gap where the ball meets it)
                box(b.x0, b.x1, 0, b.height, b.z0, b.z1, Color{196, 200, 210, 255}, 0.45f);
            } else {  // obsidian, a jade light in it
                material(0.35f, 0.9f);
                box(b.x0, b.x1, 0, b.height, b.z0, b.z1, Color{62, 58, 66, 255}, 1.6f);
            }
            if (!shadows) {  // its glowing line on the side towards the room's front
                const Color line = engrave_.id ? Color{70, 200, 255, 255} : Color{60, 235, 170, 255};
                const float half = engrave_.id ? 0.06f : 0.2f, zf = b.z0 - 0.05f;
                const float y0 = b.height * (0.5f - half), y1 = b.height * (0.5f + half);
                material(0, 0.3f, 1.6f);
                quad({b.x0 + 2, y0, zf}, {b.x1 - 2, y0, zf}, {b.x1 - 2, y1, zf}, {b.x0 + 2, y1, zf}, {0, 0, -1}, line);
                // a hit: a short brighter stretch where the ball met it, spreading along and fading
                const double since = t_ - (s.paddle_hit.t + s.render_delay);
                if (s.paddle_hit.id && since >= 0 && since < 0.35) {
                    const float k = float(std::exp(-since * 8.0));
                    const float mid = b.x0 + 2 + std::clamp(s.paddle_hit.along, 0.0f, 1.0f) * (b.x1 - b.x0 - 4);
                    const float reach = 1.5f + float(since) * 14, a = std::max(b.x0 + 2, mid - reach), e = std::min(b.x1 - 2, mid + reach);
                    material(0, 0.3f, 1.6f + 0.9f * k);
                    const Color hot{uint8_t(70 + 120 * k), uint8_t(200 + 45 * k), 255, 255};
                    quad({a, y0, zf - 0.02f}, {e, y0, zf - 0.02f}, {e, y1, zf - 0.02f}, {a, y1, zf - 0.02f}, {0, 0, -1}, hot);
                }
            }
            continue;
        }
        if (o.slot == 0) {
            const Vector3 c = ball_at(o);
            const float x = c.x, y = c.y, z = c.z;
            if (engrave_.id) {  // engraved steel, rolling, a thin neon line round it
                material(1.0f, 0.9f, 0, 0, 0, 1);
                round_ball({x, y, z}, BB_BALL_RADIUS, Color{205, 210, 220, 255});
                if (!shadows) {
                    material(0, 0.3f, 1.6f);
                    ball_band({x, y, z}, BB_BALL_RADIUS * 1.015f, 0.04f, Color{70, 200, 255, 255}, spin_);
                }
            } else {
                material(1.0f, 0.95f);
                round_ball({x, y, z}, BB_BALL_RADIUS, Color{255, 204, 96, 255});  // gold
            }
            continue;
        }
        const Look lk = look(o.sprite);
        const float x0 = o.x, x1 = o.x + CUBE, y0 = o.y, y1 = o.y + CUBE, z0 = o.z - CUBE, z1 = o.z;
        material(0, stone_.id ? (room_.id ? bb_world_style(world_).brick_gloss : 0.4f) : 0.65f, 0, 0, 1);
        const Color c = lk.colour;
        const float xm = (x0 + x1) / 2, ym = (y0 + y1) / 2, zm = (z0 + z1) / 2;
        const V3 mid{xm, ym, zm};
        auto steel = [&] { material(0.6f, 0.6f); };
        const Color iron{150, 160, 175, 255};
        switch (lk.shape) {
        case 1: pyramid(x0, x1, y0, y1, z0, z1, c); break;
        case 4: diamond(x0, x1, y0, y1, z0, z1, c); break;
        case 2: {
            const float lift = std::max(0.0f, o.lift >= 0 ? o.lift : slab(o.sprite));
            box(x0, x1, y0, y0 + 4, z0, z1, c, 0.8f);
            box(x0, x1, y0 + 4 + lift, y0 + 8 + lift, z0, z1, c, 0.8f);
            break;
        }
        case 3:  // a tube along X (bricks side by side make one)
            cylinder({x0, y0 + 6, zm}, {x1, y0 + 6, zm}, 5.5f, c, true);
            break;
        case 5: {  // the bomb: a barrel standing, its fuse burning on top
            cylinder({xm, y0, zm}, {xm, y1 - 1, zm}, 5.0f, c, false);
            steel();
            for (float y : {y0 + 2.0f, y1 - 3.0f}) cylinder({xm, y, zm}, {xm, y + 1, zm}, 5.25f, iron, false);  // its hoops
            cylinder({xm, y1 - 1, zm}, {xm, y1 + 1.5f, zm}, 0.6f, Color{60, 50, 40, 255}, false);
            if (!shadows) {
                material(0, 0.2f, 2.5f + 1.5f * float(std::sin(t_ * 14.0)));
                sphere({xm, y1 + 1.9f, zm}, 0.9f, Color{255, 170, 60, 255}, 8);
            }
            break;
        }
        case 6: {  // a "П" lying flat: the bar at the back, the legs along the floor towards the bat
            const float bar = 5, leg = 4;
            box(x0, x1, y0, y1, z1 - bar, z1, c);
            box(x1 - leg, x1, y0, y1, z0, z1 - bar + 0.5f, c);
            box(x0, x0 + leg, y0, y1, z0, z1 - bar + 0.5f, c);
            break;
        }
        case 7: {  // a "U" open towards the bat and up, the wall at its back breathing (0x35: none)
            static const float FILL[5] = {0, 0.25f, 1, 0.5f, 0.75f};
            const float th = 3, fill = FILL[std::clamp(o.sprite - 0x35, 0, 4)];
            box(x0 + th - 0.3f, x1 - th + 0.3f, y0, y0 + th, z0, z1, c, 0.6f);
            if (fill > 0) box(x0 + th, x1 - th, y0 + th, y0 + th + (y1 - y0 - th) * fill, z1 - th, z1, c, 0.6f);
            box(x1 - th, x1, y0, y1, z0, z1, c, 0.8f);
            box(x0, x0 + th, y0, y1, z0, z1, c, 0.8f);
            break;
        }
        case 8: {  // a triangular bar along X
            const V3 a{x0, y0, z0}, b{x1, y0, z0}, cc{x1, y0, z1}, d{x0, y0, z1}, e{x0, y1, zm}, f{x1, y1, zm};
            out_quad(mid, a, b, f, e, c), out_quad(mid, d, e, f, cc, c), out_quad(mid, a, d, cc, b, c);
            out_tri(mid, a, e, d, c), out_tri(mid, b, cc, f, c);
            break;
        }
        case 9: {  // a ramp rising towards the back
            const V3 a{x0, y0, z0}, b{x1, y0, z0}, cc{x1, y0, z1}, d{x0, y0, z1}, e{x1, y1, z0}, f{x1, y1, z1};
            const V3 m{xm + 2, ym - 2, zm};
            out_quad(m, a, d, f, e, c), out_quad(m, b, e, f, cc, c), out_quad(m, a, b, cc, d, c);
            out_tri(m, a, e, b, c), out_tri(m, d, cc, f, c);
            break;
        }
        case 10: {  // armoured: steel straps round it; the one only a bomb breaks carries a bomb
            box(x0, x1, y0, y1, z0, z1, c);
            steel();
            for (float z : {z0 + 0.8f, z1 - 2.5f}) box(x0 - 0.3f, x1 + 0.3f, y0 + 0.6f, y1 + 0.3f, z, z + 1.7f, iron, 0.3f);
            for (float x : {x0 + 0.8f, x1 - 2.5f}) box(x, x + 1.7f, y0 + 0.6f, y1 + 0.3f, z0 - 0.3f, z1 + 0.3f, iron, 0.3f);
            if (o.sprite == 0x09) {
                material(0.3f, 0.7f);
                sphere({xm, y1 + 2.2f, zm}, 2.4f, Color{40, 40, 46, 255});
                cylinder({xm + 1.2f, y1 + 3.8f, zm}, {xm + 1.2f, y1 + 5.2f, zm}, 0.4f, Color{90, 70, 50, 255}, false);
            }
            break;
        }
        case 11: {  // a frame raised on its faces (gilt)
            box(x0, x1, y0, y1, z0, z1, c);
            material(0.7f, 0.7f);
            const Color g = shade(c, 0.85f);
            const float m = 2.2f, w = 1.2f, out = 0.35f;
            // on the front (X = x0), the side (Z = z0) and the top (Y = y1)
            box(x0 - out, x0 + 0.1f, y0 + m, y0 + m + w, z0 + m, z1 - m, g, 0.2f);
            box(x0 - out, x0 + 0.1f, y1 - m - w, y1 - m, z0 + m, z1 - m, g, 0.2f);
            box(x0 - out, x0 + 0.1f, y0 + m, y1 - m, z0 + m, z0 + m + w, g, 0.2f);
            box(x0 - out, x0 + 0.1f, y0 + m, y1 - m, z1 - m - w, z1 - m, g, 0.2f);
            box(x0 + m, x1 - m, y0 + m, y0 + m + w, z0 - out, z0 + 0.1f, g, 0.2f);
            box(x0 + m, x1 - m, y1 - m - w, y1 - m, z0 - out, z0 + 0.1f, g, 0.2f);
            box(x0 + m, x0 + m + w, y0 + m, y1 - m, z0 - out, z0 + 0.1f, g, 0.2f);
            box(x1 - m - w, x1 - m, y0 + m, y1 - m, z0 - out, z0 + 0.1f, g, 0.2f);
            box(x0 + m, x1 - m, y1 - 0.1f, y1 + out, z0 + m, z0 + m + w, g, 0.2f);
            box(x0 + m, x1 - m, y1 - 0.1f, y1 + out, z1 - m - w, z1 - m, g, 0.2f);
            box(x0 + m, x0 + m + w, y1 - 0.1f, y1 + out, z0 + m, z1 - m, g, 0.2f);
            box(x1 - m - w, x1 - m, y1 - 0.1f, y1 + out, z0 + m, z1 - m, g, 0.2f);
            break;
        }
        case 12: {  // the bat's size: a cage of bars, a light inside
            steel();
            const float b = 1.3f;
            for (float y : {y0, y1 - b})
                for (float z : {z0, z1 - b}) box(x0, x1, y, y + b, z, z + b, iron, 0.3f);
            for (float x : {x0, x1 - b})
                for (float z : {z0, z1 - b}) box(x, x + b, y0, y1, z, z + b, iron, 0.3f);
            for (float x : {x0, x1 - b})
                for (float y : {y0, y1 - b}) box(x, x + b, y, y + b, z0, z1, iron, 0.3f);
            if (!shadows) material(0, 0.4f, 1.2f + 0.4f * float(std::sin(t_ * 3.0)));
            box(xm - 3, xm + 3, ym - 3, ym + 3, zm - 3, zm + 3, c, 0.8f);
            break;
        }
        case 13: {  // a bonus life: the block, a glowing "+" on its faces
            box(x0, x1, y0, y1, z0, z1, c);
            if (shadows) break;
            material(0, 0.4f, 1.0f);
            const Color plus{200, 255, 215, 255};
            box(x0 - 0.3f, x0 + 0.1f, ym - 0.7f, ym + 0.7f, zm - 3, zm + 3, plus, 0.2f);
            box(x0 - 0.3f, x0 + 0.1f, ym - 3, ym + 3, zm - 0.7f, zm + 0.7f, plus, 0.2f);
            box(xm - 3, xm + 3, ym - 0.7f, ym + 0.7f, z0 - 0.3f, z0 + 0.1f, plus, 0.2f);
            box(xm - 0.7f, xm + 0.7f, ym - 3, ym + 3, z0 - 0.3f, z0 + 0.1f, plus, 0.2f);
            box(xm - 3, xm + 3, y1 - 0.1f, y1 + 0.3f, zm - 0.7f, zm + 0.7f, plus, 0.2f);
            box(xm - 0.7f, xm + 0.7f, y1 - 0.1f, y1 + 0.3f, zm - 3, zm + 3, plus, 0.2f);
            break;
        }
        default: box(x0, x1, y0, y1, z0, z1, c); break;
        }
    }
}

void BBStudio::shadows(const BBState& s, const LookFn& look, const BatFn& bat, const SlabFn& slab, double t, bool mask) {
    // flattened along the sun, then away from each torch (swaying slowly with its flame), and a soft
    // spot under the ball. mask: white, the darkness in alpha (overlaps take the darkest: no doubling)
    auto tone = [&](Color c) { return mask ? Color{255, 255, 255, c.a} : c; };
    g_pass = Pass::Shadow;
    g_tri_n = 0;
    g_shadow = tone(Color{30, 20, 10, uint8_t(room_.id ? 190 : 80)});
    g_shadow_y = 0.02f;
    things(s, look, bat, slab, true);
    g_lamp_on = true;
    for (int i = 0; i < TORCHES; i++) {
        const Vector3 f = torch(i);
        const float sway = float(std::sin(t * 1.3 + i * 2.1) * 0.6 + std::sin(t * 2.2 + i) * 0.4);
        g_lamp = {f.x + 0.6f * sway, f.y + 1.5f * float(std::sin(t * 1.7 + i * 1.3)), f.z - 0.6f * sway};
        g_shadow = tone(Color{20, 10, 5, uint8_t((room_.id ? 105 : 70) + 6 * sway)});
        g_shadow_y = 0.05f + 0.03f * float(i);
        things(s, look, bat, slab, true);
    }
    g_lamp_on = false;
    g_pass = Pass::Normal;
    for (const BBObject& o : s.objects)
        if (o.slot == 0) {
            const Vector3 c = ball_at(o);
            const float hgt = std::max(0.0f, c.y - BB_BALL_RADIUS), r = BB_BALL_RADIUS * (1.1f + hgt / 30);
            const uint8_t a = uint8_t(std::clamp(200.0f - hgt * 6, 40.0f, 200.0f));
            material(0, 0, 0, 1);
            const Color in = tone(Color{15, 10, 5, a}), out = tone(Color{15, 10, 5, 0});
            for (int k = 0; k < 24; k++) {
                const float a0 = 2 * PI * float(k) / 24, a1 = 2 * PI * float(k + 1) / 24;
                vtx({c.x, 0.2f, c.z}, {0, 1, 0}, in);
                vtx({c.x + r * std::cos(a1), 0.2f, c.z + r * std::sin(a1)}, {0, 1, 0}, out);
                vtx({c.x + r * std::cos(a0), 0.2f, c.z + r * std::sin(a0)}, {0, 1, 0}, out);
            }
        }
}

void BBStudio::prepare(const BBState& s, float scale, const LookFn& look, const BatFn& bat, const SlabFn& slab) {
    if (room_.id) {  // the painted floor: no mirror (stone), the shadows' mask (half size: soft edges)
        if (!shade_.id) {
            shade_ = LoadRenderTexture(BBRenderer::W / 2, BBRenderer::H / 2);
            SetTextureFilter(shade_.texture, TEXTURE_FILTER_BILINEAR);
        }
        BeginTextureMode(shade_);
        ClearBackground(Color{0, 0, 0, 0});
        begin3d(scale, shade_.texture.width, shade_.texture.height);
        rlEnd();
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        rlSetBlendFactorsSeparate(RL_ONE, RL_ONE, RL_ONE, RL_ONE, RL_MAX, RL_MAX);
        rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
        rlBegin(RL_TRIANGLES);
        shadows(s, look, bat, slab, t_, true);
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetBlendMode(RL_BLEND_ALPHA);
        rlBegin(RL_TRIANGLES);
        end3d();
        EndTextureMode();
        return;
    }
    // the things mirrored under the floor, at half size (a little soft): laid into the floor later
    if (!refl_.id) {
        refl_ = LoadRenderTexture(BBRenderer::W / 2, BBRenderer::H / 2);
        SetTextureFilter(refl_.texture, TEXTURE_FILTER_BILINEAR);
    }
    BeginTextureMode(refl_);
    ClearBackground(Color{0, 0, 0, 0});
    begin3d(scale, refl_.texture.width, refl_.texture.height);
    g_pass = Pass::Reflect;
    things(s, look, bat, slab, false);
    g_pass = Pass::Normal;
    end3d();
    EndTextureMode();
}

void BBStudio::icon(int sprite, Vector2 c, float S, double t, const LookFn& look, const SlabFn& slab) {
    // a cube from (0, 0, 0) to (12, 12, 12): its middle (6, 6, 6) lands at (0, -9) * S from the origin
    BBState s;
    BBObject o{};
    o.sprite = sprite, o.slot = 3, o.z = 12;
    s.objects.push_back(o);
    t_ = t;
    begin3d(S, BBRenderer::W, BBRenderer::H, {c.x, c.y + 9 * S});
    things(s, look, [](const BBObject&) { return BBPaddleBody{}; }, slab, false);
    end3d();
}

void BBStudio::draw(const BBState& s, double t, float scale, const LookFn& look, const BatFn& bat, const SlabFn& slab) {
    const int w = BBRenderer::W, h = BBRenderer::H;
    const float S = scale;
    t_ = t;
    roll(s);
    // the temple: walls of stone courses with a frieze of golden glyphs under a cornice, torches on
    // them, a floor of slabs (the reflection laid in), the shadows, then the things
    begin3d(S, w, h);
    if (!room_.id) {  // (else painted)
        walls(t);
        // the floor block's front faces, under the play
        material(0, 0.2f);
        const Color edge{96, 82, 64, 255};
        box(-2, 0.01f, -LIP, 0, -2, ROOM_Z, shade(edge, 1.1f), 0.8f);
        box(-2, ROOM_X, -LIP, 0, -2, 0.01f, edge, 0.8f);
        // the floor: slabs, the reflection over them by screen position
        material(0, 0, 0, 2);
        rlEnd();
        rlSetTexture(refl_.texture.id);
        rlBegin(RL_TRIANGLES);
        auto fv = [&](float x, float z, Color c) {
            const float u = (160 + x - z) * S / float(w), v = ((118 - x / 2) * S + DROP) / float(h);
            rlTexCoord2f(u, 1 - v);
            vtx({x, 0, z}, {0, 1, 0}, c);
        };
        constexpr float SLAB = 24;
        for (int i = 0; i * SLAB < ROOM_X; i++)
            for (int j = 0; j * SLAB < ROOM_Z; j++) {
                const float x0 = i * SLAB, x1 = std::min(ROOM_X, x0 + SLAB), z0 = j * SLAB, z1 = std::min(ROOM_Z, z0 + SLAB);
                const Color c = shade(Color{168, 148, 116, 255}, 0.84f + 0.2f * hash(i + 40, j + 7));
                fv(x0, z0, c), fv(x1, z0, c), fv(x1, z1, c), fv(x0, z0, c), fv(x1, z1, c), fv(x0, z1, c);
            }
        rlEnd();
        rlSetTexture(0);
        rlBegin(RL_TRIANGLES);
        material(0, 0, 0, 1);
        const Color joint{74, 62, 48, 255};
        for (float x = SLAB; x < ROOM_X; x += SLAB)
            quad({x - 0.3f, 0.01f, 0}, {x + 0.3f, 0.01f, 0}, {x + 0.3f, 0.01f, ROOM_Z}, {x - 0.3f, 0.01f, ROOM_Z}, {0, 1, 0}, joint);
        for (float z = SLAB; z < ROOM_Z; z += SLAB)
            quad({0, 0.01f, z - 0.3f}, {0, 0.01f, z + 0.3f}, {ROOM_X, 0.01f, z + 0.3f}, {ROOM_X, 0.01f, z - 0.3f}, {0, 1, 0}, joint);
    }
    // the shadows: over the painted floor a mask made in prepare (laid on now, before the things);
    // over the 3D floor straight in
    if (room_.id && bb_world_style(world_).caustics > 0) {  // the water's light over the painted floor and walls
        rlEnd();
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        rlBegin(RL_TRIANGLES);
        material(0, 0, 0, 3);
        const Color c = WHITE;
        quad({0, 0.01f, 0}, {ROOM_X, 0.01f, 0}, {ROOM_X, 0.01f, ROOM_Z}, {0, 0.01f, ROOM_Z}, {0, 1, 0}, c);
        quad({ROOM_X, 0, 0}, {ROOM_X, WALL_H, 0}, {ROOM_X, WALL_H, ROOM_Z}, {ROOM_X, 0, ROOM_Z}, {-1, 0, 0}, c);
        quad({0, 0, ROOM_Z}, {ROOM_X, 0, ROOM_Z}, {ROOM_X, WALL_H, ROOM_Z}, {0, WALL_H, ROOM_Z}, {0, 0, -1}, c);
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetBlendMode(RL_BLEND_ALPHA);
        rlEnableDepthTest();
        rlBegin(RL_TRIANGLES);
    }
    if (room_.id && shade_.id) {
        end3d();
        const float sw = float(shade_.texture.width), sh = float(shade_.texture.height);
        DrawTexturePro(shade_.texture, {0, 0, sw, -sh}, {0, 0, float(w), float(h)}, {0, 0}, 0, bb_world_style(world_).shadow);
        begin3d(S, w, h);
    } else {
        rlEnd();
        rlDrawRenderBatchActive();
        // (darkening the colour only: blended alpha would leave holes in the picture)
        rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ZERO, RL_ONE, RL_FUNC_ADD, RL_FUNC_ADD);
        rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
        rlBegin(RL_TRIANGLES);
        shadows(s, look, bat, slab, t, false);
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetBlendMode(RL_BLEND_ALPHA);
        rlBegin(RL_TRIANGLES);
    }
    things(s, look, bat, slab, false);
    end3d();
}

void BBStudio::walls(double t) {
    // a wall's own frame: u along it, v up, d out of it into the room (the back wall X = ROOM_X, the
    // left wall Z = ROOM_Z)
    auto lbox = [](bool back, float u0, float u1, float v0, float v1, float d0, float d1, Color c, float b) {
        if (back) box(ROOM_X - d1, ROOM_X - d0, v0, v1, u0, u1, c, b);
        else box(u0, u1, v0, v1, ROOM_Z - d1, ROOM_Z - d0, c, b);
    };
    const Color sand{186, 160, 118, 255}, dark{70, 58, 44, 255}, gold{222, 170, 72, 255};
    constexpr float COURSE = 8, FRIEZE = 46, FRIEZE_H = 11, CORNICE = FRIEZE + FRIEZE_H;
    for (int wall = 0; wall < 2; wall++) {
        const bool back = wall == 0;
        const float len = back ? ROOM_Z : ROOM_X;
        material(0, 0.08f);
        lbox(back, 0, len, 0, WALL_H, -4, -0.6f, dark, 0);  // behind the blocks: the joints
        // the courses: blocks of different lengths, half a block over the one below, a little proud
        // of the wall one by one, the stone varying
        for (int row = 0; row * COURSE < FRIEZE; row++) {
            const float v0 = row * COURSE, v1 = std::min(FRIEZE, v0 + COURSE);
            float u = row % 2 ? -7.0f : 0.0f;
            for (int k = 0; u < len; k++) {
                const float n = 13 + 11 * hash(row * 31 + wall, k), u0 = std::max(0.0f, u), u1 = std::min(len, u + n);
                const float tone = 0.8f + 0.26f * hash(k + 11 * wall, row + 50);
                Color c = shade(sand, tone);
                if (hash(k * 7 + row, wall + 90) < 0.12f) c = Color{uint8_t(c.r * 0.82f), uint8_t(c.g * 0.92f), uint8_t(c.b * 0.7f), 255};  // moss
                lbox(back, u0 + 0.1f, u1 - 0.1f, v0 + 0.1f, v1 - 0.1f, -1, 0.15f + 0.55f * hash(row + 3, k * 5 + wall), c, 0.9f);
                u += n;
            }
        }
        // the frieze: carved tiles, their raised glyphs gilded
        lbox(back, 0, len, FRIEZE, FRIEZE + 0.8f, -1, 1.0f, shade(sand, 0.75f), 0.4f);
        constexpr float TILE = 12;
        for (int k = 0; k * TILE < len; k++) {
            const float u0 = k * TILE, u1 = std::min(len, u0 + TILE), v0 = FRIEZE + 0.8f, v1 = CORNICE;
            material(0, 0.1f);
            lbox(back, u0 + 0.15f, u1 - 0.15f, v0, v1, -1, 0.4f, shade(Color{168, 132, 98, 255}, 0.9f + 0.12f * hash(k, wall)), 0.6f);
            if (u1 - u0 < TILE - 1) continue;
            material(0.75f, 0.7f);
            const float a = u0 + 1.6f, b = u1 - 1.6f, c0 = v0 + 1.4f, c1 = v1 - 1.4f, d = 1.3f, f = 1.1f;
            switch (int(hash(k + 5, wall + 3) * 3)) {
            case 0:  // a frame and an eye
                lbox(back, a, b, c0, c0 + f, 0.3f, d, gold, 0.3f), lbox(back, a, b, c1 - f, c1, 0.3f, d, gold, 0.3f);
                lbox(back, a, a + f, c0, c1, 0.3f, d, gold, 0.3f), lbox(back, b - f, b, c0, c1, 0.3f, d, gold, 0.3f);
                lbox(back, (a + b) / 2 - 1.3f, (a + b) / 2 + 1.3f, (c0 + c1) / 2 - 1.3f, (c0 + c1) / 2 + 1.3f, 0.3f, d + 0.4f, gold, 0.5f);
                break;
            case 1:  // the steps
                for (int i = 0; i < 4; i++) {
                    const float x0 = a + i * (b - a) / 4, x1 = x0 + (b - a) / 4;
                    lbox(back, x0, x1, c0, c0 + (c1 - c0) * float(i + 1) / 4, 0.3f, d, gold, 0.3f);
                }
                break;
            default:  // the fret: a hook in from each side
                lbox(back, a, a + f, c0, c1, 0.3f, d, gold, 0.3f), lbox(back, a, b - 2.4f, c1 - f, c1, 0.3f, d, gold, 0.3f);
                lbox(back, b - f, b, c0, c1, 0.3f, d, gold, 0.3f), lbox(back, a + 2.4f, b, c0, c0 + f, 0.3f, d, gold, 0.3f);
                lbox(back, (a + b) / 2 - 0.6f, (a + b) / 2 + 0.6f, c0 + 2.2f, c1 - 2.2f, 0.3f, d, gold, 0.3f);
                break;
            }
        }
        // the cornice: two steps out over the frieze
        material(0, 0.12f);
        lbox(back, 0, len, CORNICE, CORNICE + 3, -1, 2.0f, shade(sand, 0.9f), 0.7f);
        lbox(back, 0, len, CORNICE + 3, WALL_H, -1, 3.2f, shade(sand, 1.02f), 0.8f);
        // the plinth along the floor
        lbox(back, 0, len, 0, 2.5f, -1, 1.4f, shade(sand, 0.7f), 0.6f);
    }
    // the torches: an iron bracket and cup, the flame (lit by itself, flickering)
    for (int i = 0; i < TORCHES; i++) {
        const Vector3 f = torch(i);
        const bool back = i < 2;
        const V3 out = back ? V3{-1, 0, 0} : V3{0, 0, -1};
        const V3 c{f.x, f.y, f.z}, wallp = c - out * 3.5f;
        material(0.5f, 0.5f);
        const Color iron{58, 52, 48, 255};
        const V3 lo = wallp + V3{0, -6, 0};
        box(std::min(lo.x, c.x) - 0.6f, std::max(lo.x, c.x) + 0.6f, f.y - 6, f.y - 4.6f, std::min(lo.z, c.z) - 0.6f,
            std::max(lo.z, c.z) + 0.6f, iron, 0.3f);
        box(c.x - 1.5f, c.x + 1.5f, f.y - 5, f.y - 2, c.z - 1.5f, c.z + 1.5f, iron, 0.5f);
        material(0, 0, 0, 1);
        const float g = torch_glow(i, t), lick = 0.6f * float(std::sin(t * 8.0 + i * 2.3));
        diamond(c.x - 1.6f, c.x + 1.6f, f.y - 2.2f, f.y + 3.5f * g + 1.5f, c.z - 1.6f, c.z + 1.6f, Color{255, 120, 30, 255});
        const V3 front = norm({-2, 1, -2}) * 1.2f;  // the core in front of the flame's skin
        diamond(c.x - 0.9f + front.x + lick * 0.3f, c.x + 0.9f + front.x + lick * 0.3f, f.y - 1.8f + front.y,
                f.y + 2.2f * g + front.y, c.z - 0.9f + front.z, c.z + 0.9f + front.z, Color{255, 230, 140, 255});
    }
}
