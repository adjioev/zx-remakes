#pragma once

// Ball Breaker in HD, Tron style: the room, bricks and ball drawn in code as violet neon on black
// glass, from the game's own 3D coordinates; anything else from the game's sprites, tinted. 16:9:
// the 256 x 128 playfield fills 1920 x 960, a new status bar takes the 120 px below.
//
// Game coordinates (notes/ballbreaker.md): X runs from the front to the back wall (screen +1, -1/2
// per unit), Z from the right lip to the left wall (screen -1), Y up. A ground point is at
// playfield pixel (160 + X - Z, 118 - X/2 - Y). Floor: X 0..92, Z 0..148; walls 64 high.

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include <raylib.h>

struct BBObject {
    float sx, sy;      // playfield pixel of the sprite's top left (smoothed: not whole)
    int sprite;        // sprite number
    float x, y, z;     // game 3D coordinates
    int slot;          // 0 ball, 1 bat, 2 missile, 3+ bricks
};

struct BBState {
    int level = 0;  // 0-based
    int room = 0;   // 0-6: A-G
    int score = 0, missiles = 0, lives = 0;
    std::vector<BBObject> objects;
};

class BBRenderer {
public:
    static constexpr int W = 1920, H = 1080, FIELD_H = 960;
    static constexpr float SCALE = 7.5f;  // playfield pixel -> canvas

    BBRenderer();
    ~BBRenderer();
    BBRenderer(const BBRenderer&) = delete;
    BBRenderer& operator=(const BBRenderer&) = delete;

    // The game's sprites (60 x 120 bytes from 0xB9D5), once.
    void load_sprites(const uint8_t* data);
    bool has_sprites() const { return sprites_.id != 0; }

    // A brick is gone: a flash, sparks and its points where it was.
    void burst(const BBObject&, int points);

    void prepare(const BBState&, double t);  // effects, the glow layer
    void draw(const BBState&, double t);     // into the current target (the canvas)
    void debug_dump(const char* dir);

private:
    void scene(const BBState&, double t, bool glow_pass);
    void room(int room, double t, bool glow_pass);
    void brick(const BBObject&, double t, bool glow_pass);
    void ball(const BBObject&, double t, bool glow_pass);
    void sprite(const BBObject&, Color tint);
    void status_bar(const BBState&);
    void effects(bool glow_pass);

    struct Particle {
        Vector2 p, v;
        float life, max, size;
        Color c;
    };
    struct Popup {
        Vector2 p;
        float life;
        int points;
        Color c;
    };
    std::vector<Particle> parts_;
    std::vector<Popup> popups_;
    std::deque<Vector2> trail_;
    double last_t_ = -1;
    std::mt19937 rng_{1987};

    RenderTexture2D glow_{}, half_a_{}, half_b_{};
    Shader blur_{};
    int blur_dir_loc_ = -1;
    Texture2D sprites_{};
};
