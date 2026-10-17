#pragma once

// Ball Breaker in HD: Neon / Tron and Classic HD share geometry from the game's own
// 3D coordinates. Classic uses solid bright materials; Neon uses dark glass and bloom.
// Effects (the ball rolling, trails, bounces, landings, shards, lights) are presentation only:
// they read the game, never change it. 16:9:
// the 256 x 128 playfield fills 1920 x 960, a new status bar takes the 120 px below.
//
// Game coordinates (notes/ballbreaker.md): X runs from the front to the back wall (screen +1, -1/2
// per unit), Z from the right lip to the left wall (screen -1), Y up. A ground point is at
// playfield pixel (160 + X - Z, 118 - X/2 - Y). Floor: X 0..92, Z 0..148; walls 64 high.

#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include <raylib.h>
#include "ballbreaker_paddle.h"

struct BBObject {
    float sx, sy;      // playfield pixel of the sprite's top left (smoothed: not whole)
    int sprite;        // sprite number
    float x, y, z;     // game 3D coordinates
    int slot;          // 0 ball, 1 bat, 2 missile, 3+ bricks
    float lift = -1;   // a double slab's upper plate, pixels above the lower one (smoothed; -1: from the sprite)
    float bat_width = 0;  // slot 1: collision width from 0x8063, not the sprite mask
};

// The double slab bricks bob their upper plate 0-5 pixels (one sprite per height); -1 for other sprites.
float slab_lift(int sprite);

// What the menus show (the original runs them; the remake draws them).
struct BBMenu {
    enum class Screen { None, Loading, Main, Controls, Keys, HiScores, Instructions, LevelCard };
    Screen screen = Screen::None;
    int cursor = 0;  // main menu item 0-5
    std::string title = "BALL BREAKER", publisher = "CRL GROUP PLC 1987";  // the game's
    std::string level_name, designer;  // the level card's (Ballbreaker II), if the game has them
    float slide = 1;  // the level card: the room coming in from the left as the original's (0 not yet, 1 in place)
    double since = 0;  // seconds this screen has been up
    float leave = 1;   // PLAY GAME taken: the main menu going (0 -> 1), as the original wipes it, then the card
    struct Score {
        std::string name;
        int score;
    };
    std::vector<Score> scores;  // the hi-score table, best first
    int entry_row = -1;         // the row a name is being typed into
    struct Item {               // the instructions: a brick (sprite), what it does, its points
        int sprite;
        std::string text;
        int points;
    };
    std::vector<Item> items;
    bool instructions_auto = false;
    int instructions_page = -1;  // -1: automatic; otherwise a manually selected page
    static constexpr int INSTRUCTIONS_PER_PAGE = 9;
};

// Presentation follows the simulation by one ball step (25 Hz).
inline constexpr double BB_RENDER_DELAY = 0.04;
struct BBPaddleHit {
    uint64_t id = 0;
    double t = -10;  // simulation time of the confirmed collision
    float along = 0;  // contact along the paddle, 0..1
};

struct BBState {
    double render_delay = BB_RENDER_DELAY;  // how far the picture runs behind the game (s)
    int level = 0;  // 0-based
    int room = 0;   // 0-6: A-G
    int score = 0, missiles = 0, lives = 0;
    bool unlimited_lives = false, unlimited_missiles = false;  // the remake's options
    std::vector<BBObject> objects;
    BBMenu menu;
    BBPaddleHit paddle_hit;
};

enum class BBTheme { Neon, ClassicHD };

class BBRenderer {
public:
    static constexpr int W = 1920, H = 1080, FIELD_H = 960;
    static constexpr float BALL_RADIUS = BB_BALL_RADIUS;  // playfield units; shared by sphere and paddle contact
    static constexpr float SCALE = 7.5f;  // playfield pixel -> canvas

    // fonts_dir: Orbitron.ttf; intro: the title picture (optional)
    // intro: the loading screen's art; creatures: the aliens' painted frames (tools/bb_creatures.py)
    BBRenderer(const std::string& fonts_dir, const std::string& intro, const std::string& creatures);
    ~BBRenderer();
    BBRenderer(const BBRenderer&) = delete;
    BBRenderer& operator=(const BBRenderer&) = delete;

    BBTheme theme() const { return theme_; }
    void set_theme(BBTheme theme) { theme_ = theme; }
    const char* theme_name() const { return classic() ? "Classic HD" : "Neon / Tron"; }

    // The game's sprites (60 x 120 bytes from 0xB9D5), once.
    void load_sprites(const uint8_t* data);
    bool has_sprites() const { return sprites_.id != 0; }

    // A brick is gone: a flash, sparks and its points where it was.
    void burst(const BBObject&, int points);

    void prepare(const BBState&, double t);  // effects, the glow layer
    void draw(const BBState&, double t);     // into the current target (the canvas)
    void debug_dump(const char* dir);

private:
    int room_level_ = -1;  // the level the room's size (its walls) is for
    BBTheme theme_ = BBTheme::Neon;
    bool classic() const { return theme_ == BBTheme::ClassicHD; }
    void scene(const BBState&, double t, bool glow_pass);
    void room(int room, double t, bool glow_pass);
    void brick(const BBObject&, double t, bool glow_pass);
    void bat(const BBObject&, double t, bool glow_pass);
    void bat_lane(const BBObject&, double t, bool glow_pass);  // the floor in front of the bat, lit
    void ball(const BBObject&, double t, bool glow_pass);
    void sprite(const BBObject&, Color tint, double t, bool glow_pass);  // the game's sprite as neon vectors
    void missile(const BBObject&, double t, bool glow_pass);
    void status_bar(const BBState&);
    void effects(double t, bool glow_pass);
    // the menus (end of ballbreaker_hd.cpp)
    void menu_scene(const BBState&, double t, bool glow_pass);
    void level_card(const BBState&, double t, bool glow_pass);
    void text(const char* s, Vector2 centre, float size, Color c, bool big = false) const;
    void text_left(const char* s, Vector2 at, float size, Color c, bool big = false) const;
    void thumbnail(int sprite, Vector2 centre, float scale, double t, bool glow_pass);
    Font font_big_{}, font_ui_{};
    Texture2D intro_{};
    Texture2D creatures_{};  // 0x11-0x15, a cell each: the sprite's box plus CREATURE_PAD all round
    void floor_marks(double t, bool glow_pass);
    void update_ball(const BBObject*, double t, float dt);
    // light on a point (game coordinates) from the ball and the explosions, 0..
    Color lit(Color base, float x, float y, float z) const;

    // Effects. All in canvas pixels / seconds of game time; presentation only.
    struct Spark {  // a bright streak
        Vector2 p, v;
        float life, max;
        Color c;
    };
    struct Shard {  // an angular fragment of a broken brick, tumbling
        Vector2 p, v;
        float ang, spin, size, life, max;
        Color c;
    };
    struct Flash {  // a short light pulse at a point
        Vector2 p;
        float radius, life, max;
        Color c;
    };
    struct FloorRing {  // a ring spreading over the floor (game X / Z)
        float x, z, radius, life, max;
        Color c;
    };
    struct Light {  // lights nearby faces (game coordinates)
        float x, y, z, radius, intensity, life, max;
        Color c;
    };
    struct Popup {
        Vector2 p;
        float life;
        int points;
    };
    struct TrailPoint {
        Vector2 p;
        double t;
    };
    std::vector<Spark> sparks_;
    std::vector<Shard> shards_;
    std::vector<Flash> flashes_;
    std::vector<FloorRing> rings_;
    std::vector<Light> lights_;
    std::vector<Popup> popups_;

    // The ball as drawn: rolling (its orientation turns with the distance it travels), its trail
    // in the air and on the floor, landings and bounces.
    struct BallFx {
        bool have = false;
        float x = 0, z = 0, h = 0;     // game coordinates, h = height above the floor
        Vector2 floor{};               // the point on the floor right under it (canvas)
        float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        float wx = 0, wy = 0, wz = 0;  // spin (rad/s), kept in the air, fading
        float vx = 0, vz = 0;          // ground velocity (units/s), smoothed
        double land_t = -10, bounce_t = -10;
        std::deque<TrailPoint> trail, floor_trail;
    } ball_;
    struct BatWave {
        float along, life;  // where on the bat (0..1), seconds
    };
    std::vector<BatWave> bat_waves_;
    // The bat's box, from its sprite (it changes with the "bat size" bricks): its length along
    // the rail (X, what the ball can hit), its width (Z), its height; where its sprite's box starts.
    using BatShape = BBPaddleShape;
    BatShape bat_shape(int sprite) const;
    using BatBody = BBPaddleBody;
    BatBody bat_body(const BBObject&) const;
    std::vector<const BBObject*> scene_order(const BBState&) const;
    std::vector<uint8_t> sprite_data_;
    uint64_t shown_paddle_hit_ = 0;  // consume each confirmed collision once
    Vector2 bat_edge_a_{}, bat_edge_b_{};     // its striking edge: drawn crisp over the glow
    bool bat_edge_ = false;

    double last_t_ = -1;
    std::mt19937 rng_{1987};

    RenderTexture2D glow_{}, half_a_{}, half_b_{};
    Shader blur_{};
    int blur_dir_loc_ = -1;
    Texture2D classic_sprites_{};  // opaque paper and ink, using the original masks
    Texture2D sprites_{};          // the sprites' solid parts as dark glass (ink darker), for the fill
    // The sprites as vectors (sprite pixels): the silhouette's outlines, smoothed (closed), and
    // the ink strokes inside (open), drawn as crisp neon lines at any size.
    struct SpriteVec {
        std::vector<std::vector<Vector2>> outlines, strokes;
    };
    std::vector<SpriteVec> vec_;
    void sprite_at(int sprite, Rectangle dst, Color tint, double t, bool glow_pass);
    // the aliens drawn by hand as vectors (sprite pixels in dst): false if not one of them
    bool creature(int sprite, Rectangle dst, Color tint, double t, bool glow_pass);
};
