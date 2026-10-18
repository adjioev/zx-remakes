#pragma once

// The remake's own Ball Breaker play (Even): a level as the game builds it, played by the rules of the
// original (notes/ballbreaker.md, "The remake's own play") with a ball and a bat of the remake's - one
// speed, any angle, the ball a sphere and the bat a box as the HD view draws them. The emulator runs
// everything else (the menus, the level cards, the hi-scores, the tune) and is parked meanwhile.
//
// A frame here is a pass of the original's play (it runs one a frame at its even pace). The bricks sit
// in the game's grid: 8 columns of 12 (X), 13 rows of 12 (a brick's Z is (row + 1) * 12), layers of
// 12 (Y). The ball and the missiles meet the floor layer only. Bricks above come down only when
// something makes them (a hit under them, a bomb, a slide, a creature walking out), one layer at a
// time, as in the original; while they fall that cell lets the ball through.

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "ballbreaker_hd.h"
#include "ballbreaker_paddle.h"

struct BBEngineRules {
    std::array<uint8_t, 64> points{};   // a kind's points / 100 (0xB893 + 3 kind)
    std::array<uint8_t, 64> handler{};  // its handler number (0xB894 + 3 kind)
    std::array<uint8_t, 64> sprite{};   // its sprite (0xB892 + 3 kind); 0x80 and up: an animation
    // the bat as the HD view draws it, its centre and width given (floor X / Z, height)
    std::function<BBPaddleBody(float centre, int width)> bat_body;
    std::function<void(float centre, int width, BBObject& o)> bat_object;  // its object for the HD view
};

struct BBEngineInput {
    int dir = 0;         // the keys / pad: -1 left, 1 right
    float target = -1;   // the mouse: where the bat's centre goes (X; -1 none)
    bool fire = false;
};

// A level's brick as the game built it: its grid place and kind (object +6), the sprite drawn, its
// object's number (0xF3EB + 10 n)
struct BBEngineBrick {
    int col, row, layer, kind, sprite, obj;
};

// An object as the game would have it (for its own drawing, the original pictures): its number, the
// sprite's place (sx; 0: none) and number, its X, Y, Z (the depth order)
struct BBEngineDraw {
    int obj, sx, sy, sprite, x, y, z;
};

class BBEngine {
public:
    enum class Result { None, Cleared, Over };
    struct Event {
        // Sound: the original's effect number (its 0xA91D calls) for its own sound
        enum Kind { Bat, Wall, Lost, Sound } kind;
        float along = 0.5f;  // Bat: where on it, 0..1
        int sound = 0;
    };

    void set_rules(BBEngineRules rules) { rules_ = std::move(rules); }
    // a level as built; fast: after the last level the original moves its ball twice as often
    void start_level(const std::vector<BBEngineBrick>& bricks, int level, bool fast);
    void step(const BBEngineInput& in);  // a frame
    bool active() const { return active_; }
    void stop() { active_ = false; }
    Result result() const { return result_; }

    int score = 0, lives = 3, missiles = 10;  // carried from level to level (the game's at the start)
    bool unlimited_lives = false, unlimited_missiles = false;
    bool speed_up = false;  // option G

    void objects(std::vector<BBObject>& out) const;  // for the HD view, slots stable
    void draws(std::vector<BBEngineDraw>& out) const;  // for the game's own drawing
    std::vector<Event> take_events() { auto e = std::move(events_); events_.clear(); return e; }
    // a bounce's point between frames, for the picture (see BallBreakerView's tracks)
    bool contact = false;
    float contact_at = 1, contact_x = 0, contact_z = 0;

    float ball_x() const { return ball_.x; }
    float bat_x() const { return bat_x_; }
    BBPaddleBody body() const { return bat_body(); }

private:
    enum class State { Rest, Falling, Sliding, Exploding, Gone };
    struct Brick {
        int col = 0, row = 0, layer = 0;  // its grid place (while falling: where it came from)
        int kind = 0, sprite = 0;
        State state = State::Rest;
        float dy = 0, dz = 0;   // falling: down so far (px); sliding: towards the bat so far
        int t = 0;              // the frame of its fall / slide / explosion
        int fall_start = 0;     // falling: the frame it starts moving (a brick on layer a: 3 (a - 1))
        int fall_layers = 1;    // falling: one layer, or two after a bomb
        bool slid = false;      // the sliding brick and the bonus box: hit once already
        int spawn = 0;          // a creature brought back on a stack: materialising frames left
        int obj = -1;           // the game's object it is (-1: none)
        float x() const { return float(col * 12); }
        float z() const { return float((row + 1) * 12); }
    };
    struct Creature {
        float x = 0, y = 0, z = 0;
        int type = 1;            // 1 walks (sprites 0x11-0x14), 2 one frame (0x15)
        int frame = 0, timer = 4;
        int spawn = 0;           // materialising: frames left (the explosion played backwards)
        int home_col = 0, home_row = 0;
        int id = 0, burst = 0;   // blown up: frames left of its explosion
        bool on_bat = false;     // landed on the bat: frozen there
        int obj = -1;            // the game's object it is
    };
    struct Ball {
        float x = 0x2A, z = 0x14, h = 0, vx = 0, vz = 0, vh = 0, boost = 1;
        bool flying = false;
        int drop = 0;            // the serve: bounces left of its drop
        int falling = 0;         // past the bat: frames of its fall
        int bounces = 0;
    };
    struct Missile {
        bool flying = false;
        float x = 0, z = 0;
        int frame = 0, burst = 0;
    };
    struct Hold {                // a stack a creature walked out from under: it falls after a while
        int col, row, t;
    };

    void serve();
    void step_bat(const BBEngineInput& in);
    void step_ball();
    void step_bricks();
    void step_creatures();
    void step_missile(const BBEngineInput& in);
    void step_exit();
    // a floor brick met by the ball (vz: its Z velocity before) or a missile. Returns false when it
    // refuses (a missile then bursts)
    bool hit(size_t i, bool missile, float vz);
    void explode(size_t i, bool score_it);
    bool fall_from(int col, int row, int layer, int layers, int steps_table);  // false: busy
    bool busy(int col, int row) const;
    int solid(int layer, int col, int row) const;  // the brick at rest there, or -1
    int stack_height(int col, int row) const;
    void deflect();
    void lose_ball();
    bool level_left() const;
    void become_creature(size_t i);
    void respawn(const Creature& from);
    int free_object();  // a gone brick's object, for a creature brought back
    void release(const Creature& c);  // a creature gone: its object free again
    BBPaddleBody bat_body() const;

    BBEngineRules rules_;
    std::vector<Brick> bricks_;
    std::vector<Creature> creatures_;
    struct Returning {
        Creature who;
        int wait;  // frames until it comes back
    };
    std::vector<Returning> returning_;
    std::vector<Hold> holds_;
    Ball ball_;
    Missile missile_;
    float bat_x_ = 42, bat_v_ = 0;
    int bat_width_ = 12;
    int level_ = 0, frame_ = 0, cooldown_ = 0, eating_ = 0, eat_total_ = 0;
    int slow_ = 0;           // the slow ball: frames left
    int drain_ = 0, drain_t_ = 0;  // the bonus box: deductions left, frames to the next
    int exit_ = 0, exit_wait_ = 0; // the exit brick: on; frames from the clear floor to the level's end
    int cleared_wait_ = 0;   // all done: frames left of the last explosions
    bool fast_ = false;
    int next_id_ = 0;
    int ignore_ = -1;        // a brick being hit: no support for what stood on it any more
    unsigned rng_ = 12345;
    bool active_ = false;
    Result result_ = Result::None;
    std::vector<Event> events_;
    unsigned random() { rng_ = rng_ * 1103515245u + 12345u; return (rng_ >> 16) & 0x7FFF; }
    void sound(int n) { events_.push_back({Event::Sound, 0.5f, n}); }
};
