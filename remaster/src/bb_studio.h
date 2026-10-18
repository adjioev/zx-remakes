#pragma once

// The Studio look (T): the play as a lit 3D scene - bevelled bricks with gloss, a chrome ball, shadows
// on the floor and the floor reflecting what stands on it, a light tiled room with glowing trim. It
// is drawn with a projection matrix that puts every point exactly where the other looks put it (and
// where the play is reckoned): 160 + X - Z across, 118 - X / 2 - Y down, in playfield pixels.
// The menus, the effects (shards, sparks, points) and the panel stay the 2D renderer's.

#include <functional>
#include <string>

#include <raylib.h>

#include "ballbreaker_hd.h"

struct BBWorldStyle {
    const char* name;
    const char* dir;           // its art: <adventure>/<dir>/room.png, bricks.png
    Vector3 light;             // its four lights' colour (torches, shells, crystals, vents)
    Color glow_out, glow_in;   // their glow over the picture
    Color room_tint;           // the painting, darkened
    Color shadow;              // the shadows' colour
    Vector3 air;               // the light's tint on the things
    float exposure, brick_gloss;
    Vector3 crack_glow;        // the bricks' cracks glowing (the volcano)
    float caustics;            // the water's light playing (Atlantis)
};
const BBWorldStyle& bb_world_style(BBWorld w);
// a picture of the art: <base>.png, else <base>.jpg (none: id 0)
Texture2D bb_load_art(const std::string& base);

class BBStudio {
public:
    struct Look {
        Color colour;
        // 0 box, 1 pyramid, 2 slabs, 3 lying cylinder, 4 diamond, 5 barrel (the bomb), 6 a lying "П",
        // 7 a "U" with a breathing wall, 8 a prism, 9 a ramp, 10 armoured, 11 framed, 12 the bat-size cage,
        // 13 a bonus life
        int shape;
    };
    explicit BBStudio(const std::string& dir);  // dir: the art (engrave.png, a folder per world)
    void set_world(BBWorld w);
    BBWorld world() const { return world_; }
    ~BBStudio();
    BBStudio(const BBStudio&) = delete;
    BBStudio& operator=(const BBStudio&) = delete;

    // the play into the current target (W x H canvas); look: a brick sprite's colour and shape;
    // bat: the bat's box as drawn; slab: the bobbing slab's lift by sprite (-1 none)
    using LookFn = std::function<Look(int sprite)>;
    using BatFn = std::function<BBPaddleBody(const BBObject&)>;
    using SlabFn = std::function<float(int sprite)>;
    // before the frame is drawn (outside any render target): the reflection under the floor
    void prepare(const BBState& s, float scale, const LookFn& look, const BatFn& bat, const SlabFn& slab);
    void draw(const BBState& s, double t, float scale, const LookFn& look, const BatFn& bat, const SlabFn& slab);
    // one brick by itself, its middle at centre (the instructions' pictures); scale: canvas px per unit
    void icon(int sprite, Vector2 centre, float scale, double t, const LookFn& look, const SlabFn& slab);

    // the scene sits this much lower than the other looks (room for the panel on top)
    static constexpr float DROP = 90;
    // the torches: their flames (playfield units) and how bright they burn at t
    static constexpr int TORCHES = 4;
    static Vector3 torch(int i);
    static float torch_glow(int i, double t);
    // the ball's centre in the room
    static Vector3 ball_at(const BBObject& o);
    // the room painted (walls, floor, what is round it; drawn first, flat), if there is the art
    const Texture2D* room() const { return room_.id ? &room_ : nullptr; }
    // the world's creatures: six cells (four steps of the walker, two frames of the other), if there
    const Texture2D* creatures() const { return creatures_[int(world_)].id ? &creatures_[int(world_)] : nullptr; }

private:
    Shader shader_{};
    std::string dir_;
    BBWorld world_ = BBWorld::Jungle;
    Texture2D rooms_[BB_WORLDS]{}, bricks_[BB_WORLDS]{}, creatures_[BB_WORLDS]{};
    bool tried_[BB_WORLDS]{};
    Texture2D room_{}, stone_{}, engrave_{};  // (room_, stone_: the world's, owned by the arrays)
    int loc_moss_ = -1, loc_fire_ = -1, loc_air_ = -1, loc_crack_ = -1, loc_caustics_ = -1, loc_time_ = -1;
    int loc_stone_ = -1, loc_stone_tex_ = -1, loc_engrave_ = -1, loc_engrave_tex_ = -1, loc_spin_ = -1, loc_exposure_ = -1;
    float spin_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // the ball's turn (row-major)
    Vector3 roll_from_{};
    RenderTexture2D refl_{};
    RenderTexture2D shade_{};  // the shadows over the painted floor (alpha: how dark)  // the things mirrored under the floor (half size)
    int loc_torch_ = -1, loc_light_ = -1, loc_metal_ = -1, loc_gloss_ = -1, loc_emit_ = -1, loc_flat_ = -1;
    double t_ = 0;
    Matrix saved_projection_{}, saved_modelview_{};
    float metal_ = -1, gloss_ = -1, emit_ = -1, flat_ = -1, stone_on_ = -1, engrave_on_ = -1;
    void material(float metal, float gloss, float emit = 0, float flat = 0, float stone = 0, float engrave = 0);
    void roll(const BBState& s);
    // at: where the room's origin lands on the canvas (the play's place by default)
    void begin3d(float scale, int width, int height, Vector2 at = {-1, -1});
    void end3d();
    void walls(double t);
    void shadows(const BBState& s, const LookFn& look, const BatFn& bat, const SlabFn& slab, double t, bool mask);
    void things(const BBState& s, const LookFn& look, const BatFn& bat, const SlabFn& slab, bool shadows);
};
