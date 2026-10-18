#pragma once

// Ball Breaker (CRL Group, 1987, 128K): breakout in an isometric room (addresses:
// notes/ballbreaker.md). In play the HD view (ballbreaker_hd.cpp, 16:9) replaces the picture;
// menus, the hi-score table and the level intro show the original, centred.
//
// Play runs one original game-loop pass per 50 Hz frame, with CPU headroom for
// expensive redraws. Ball updates use the same clock at 25 Hz; the original
// interrupt-driven audio and countdowns keep their rate.
//
// Debug: BB_NO_TURBO=1, BB_TURBO_LOG=1 (turbo); BB_PERF=1 (CPU time of each HD frame);
// BB_STRESS=1 (every brick bursts at once when play starts: effects under load); BB_BALL_LOG=1
// (the ball's path, for the sound effects). Runner: ZX_SFX_LOG=1, ZX_MUSIC_LOG=1 (remix on / off, its steps),
// ZX_AY_LOG=1 (the AY registers every frame), ZX_AUDIO_DUMP=file.wav, ZX_AUDIO_STATS=1 (the output queue,
// muted). Launcher: ZXREMAKES_GAME=ballbreaker goes straight into the game.
//
// Controls are modern: the game is set to its Kempston joystick, and the module drives its
// menus, play and name entry from arrows / WASD / Space / Enter / gamepad.

#include <cstdint>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <utility>
#include <vector>

#include "ballbreaker_hd.h"
#include "bb_engine.h"
#include "ballbreaker_preview.h"
#include "ballbreaker_paddle_motion.h"
#include "ballbreaker_contact_motion.h"
#include "ballbreaker_collision.h"
#include <optional>
#include "game.h"

// Where things are in one of the Ball Breaker games: Ballbreaker II (notes/ballbreaker2.md) is the
// same engine reassembled, with the same sprites and rooms; most of its code sits 0x22 further on.
struct BBGame {
    const char* art;  // assets/<art>: the HD art (shared)
    const char* title;
    const char* publisher;
    uint16_t level_names;                 // the levels' names: strings, last char + 0x80, '*' new line (0: none)
    std::vector<const char*> designers;   // the levels' designers, five levels each (none: not shown)
    // code run once a frame in each part of the game
    uint16_t menu_loop, play_loop, entry_loop, page_loop, ctrl_loop, keys_loop;
    // the item under the menu's bar: its address, and the first item's value (others: no item)
    uint16_t menu_cursor;
    int cursor_base;
    // a menu with an arrow moved freely (Ballbreaker II): its x, y (0: a bar moved item to item);
    // the remake puts it on the item chosen, rows from y = 0x58, 16 apart
    uint16_t menu_pointer;
    uint16_t main_loop, play_exit, pass_end, level_start, play_result;  // play
    uint16_t paddle_hit, creature_lands, creature_caught, bat_eaten;   // events
    uint16_t ball_clock_read, ball_due, serve_countdown;               // pacing
    uint16_t tune_start;  // the music player's start of the tune (0: unmapped, no remix)
    uint16_t tune_vars;   // its variables (BB1 0x80AE: speed, channel pointers at +5 +7 +9...)
    uint16_t tune_tempo;  // its tempo word
    uint16_t lives, control_type, hiscores, instructions, points, sprites;
    uint16_t trainer;  // a tape's trainer patches this DEC (HL) (0: none)
    int levels;
    std::vector<std::vector<int>> rooms;  // the levels (1-based) in each room, A-G
    uint16_t instructions_entry = 0;  // automatic demonstration entry, when mapped
    BBTheme theme = BBTheme::Neon;    // the HD look to start with (T changes it)
    uint16_t bat_move = 0;            // the bat's step (LD HL,0x8061; C: the directions held), 0: none
    // level_start's return from building the level (its objects from 0xF409, the end at 0x804C):
    // before it the object list is the old level's, or (BB2) its card's text buffer
    uint16_t level_built = 0;
    // the ball's batch of moves (a pass when it is due): its start (LD HL,(0x8064)) and the publish of
    // the result after it; the position the batch started from (a brick hit puts the ball back there),
    // the falling flag (set as the ball leaves the floor past the bat), the slow-ball counter
    uint16_t ball_batch = 0, ball_batch_end = 0, ball_start = 0, ball_falling = 0, ball_slow = 0;
    uint16_t sound_request = 0;  // the original's effect to play (0xA91D writes it: 4 the bat, 5 a wall)
    // in a pass, just after the game empties its list of objects to redraw (the pointer at 0x8050;
    // 0x9B7A adds one): the remake's bat, moved, is put on it as the game's own step would
    uint16_t redraw_list = 0;
    // the remake's own play (bb_engine): the bricks' points and kinds (3 bytes a sprite: points / 100,
    // handler), and a JR $ (inside another instruction) to park the CPU at while it plays a level
    uint16_t brick_table = 0, park = 0;
    // the game's own drawing, run from the park so the original pictures follow the remake's play:
    // the objects on the redraw list (0x8445), the result to the screen (0x81AB), the panel's score,
    // missiles and lives
    uint16_t draw_dirty = 0, draw_blit = 0, hud_score = 0, hud_missiles = 0, hud_lives = 0;
};
extern const BBGame BALL_BREAKER, BALL_BREAKER_2;

class BallBreakerView : public OriginalView {
public:
    explicit BallBreakerView(const BBGame& game = BALL_BREAKER) : g_(game) {}
    void start(Spectrum&, const std::string& assets) override;
    void input(Spectrum&, const Controls&) override;
    void after_frame(Spectrum&) override;
    std::string help() const override;
    PauseStyle pause_style(bool original) const override;
    Vector2 canvas_size() const override { return {float(BBRenderer::W), float(BBRenderer::H)}; }
    void prepare(const Spectrum&, bool original, double t) override;
    void draw(const Spectrum&, bool original, double t) override;

    bool letters_free() const override { return mode_ != Mode::NameEntry; }
    bool hides_pointer() const override { return mode_ == Mode::Play && !shown_original_; }
    bool steers() const override { return mode_ == Mode::Play; }
    bool take_options() override { return std::exchange(options_taken_, false); }
    bool take_exit() override { return std::exchange(exit_taken_, false); }
    Escape escape() override;
    std::string on_key(Spectrum&, int key) override;
    int levels() const override { return g_.levels; }
    int level(const Spectrum& s) const override;
    bool can_jump() const override { return mode_ == Mode::Play; }
    void jump_to(Spectrum&, int level) override;
    std::unique_ptr<MusicRemix> make_remix(int sample_rate) const override;
    void take_sfx(std::vector<SfxEvent>& out) override;
    void set_remake(bool on) override { remake_ = on; }
    std::string view_changed(Spectrum& s, bool original) override;
    bool layout_quiet(const Spectrum& s) const;
    void switch_rules(Spectrum& s);
    void restore_layout(Spectrum& s);
    void set_original_speed(bool on);
    bool music_restarted() override { return std::exchange(music_restarted_, false); }

    // Where the game is, from the code it ran last frame.
    enum class Mode { Boot, Menu, Play, NameEntry, Screen };  // Screen: a menu page waiting for a key
    Mode mode() const { return mode_; }

private:
    const BBGame& g_;
    int room_of(int level) const;
    bool hd_now(bool original) const;
    // A game loaded from tape by its own loader starts in its menu, with no loading screen waiting
    // for a key: the remake's title is shown first then, until a key (taken by it, not the menu)
    bool title_hold_ = true;
    static constexpr size_t HISCORES_SIZE = 9 * 14;  // 9 x [12 name, word score / 100]
    std::string hiscores_file_;                    // where they are kept ("": not kept)
    std::vector<uint8_t> hiscores_saved_;          // as last read / written
    bool hiscores_restored_ = false;
    bool title_now() const { return title_hold_ && mode_ == Mode::Menu && screen_ == BBMenu::Screen::Main; }
    BBState read_state(const Spectrum&) const;

    // Changed positions with their sample times. Render 40 ms behind simulation so
    // the ball's 25 Hz steps have both endpoints, without guessing the next step.
    struct Sample {
        double t;
        float pos[5];  // sx sy x y z
    };
    struct Track { std::deque<Sample> samples; };
    void smooth_sample(const Spectrum&);
    void sounds(const BBState& before, size_t bursts_before);
    void check_cleared();  // the remake goes on to the next level once the last brick is gone  // effects for what happened this frame
    void smooth_apply(BBState&, double t) const;
    static void track_pos(const Track&, double t, float* out);

    std::unique_ptr<BBRenderer> hd_;
    BBState state_, latest_;
    BBPaddleBody modern_body(const Spectrum&) const;
    void modern_step(Spectrum&, const zxc_regs_t&);
    // Even: the ball flown by the remake (see hybrid_step); the game keeps the bricks, bonuses, creatures
    bool hybrid() const { return hybrid_ && !original_speed_ && g_.ball_batch && !engine_mode(); }
    // Even: the remake plays the level itself (bb_engine), the game parked meanwhile (BB_ENGINE=0: the
    // hybrid instead); decided as a level is built, for that level
    bool engine_mode() const { return engine_enabled_ && !original_speed_ && g_.park; }
    bool engine_play() const { return engine_level_ && engine_.active(); }
    void engine_start(Spectrum&);
    void engine_frame(Spectrum&);
    BBEngine engine_;
    bool engine_enabled_ = true, engine_level_ = false, engine_fire_ = false, engine_fast_ = false;
    bool parked_ = false;  // the CPU is at the park: the game's own level start (its card, the room) is done
    uint16_t engine_then_ = 0;  // where the parked CPU goes next (the level start, the play's result)
    std::vector<uint16_t> engine_calls_;  // the game's routines to run from the park (its drawing)
    int drawn_score_ = -1, drawn_missiles_ = -1, drawn_lives_ = -1;
    void engine_draw(Spectrum&);
    BBPaddleShape bat_shape_;
    bool codex_even() const { return !original_speed_ && !hybrid(); }  // the earlier Even (BB1, BB_HYBRID=0)
    bool preview_on() const { return preview_enabled_ && !hybrid(); }
    void hybrid_step(Spectrum&, bool batch);
    struct HybridBall {
        float x = 0, z = 0, vx = 0, vz = 0;  // the centre (0x8064 / 0x8065) and the velocity, units a frame
        int wx = -1, wz = -1, wdx = 0, wdz = 0;  // what was last written: position, directions
        uint64_t frame = 0, batch = 0;           // the frames of the last move, the last batch
        bool live = false;                       // in flight (from a batch on, until it falls)
        int bounces = 0;                         // off walls and bricks since the bat: a loop is nudged
        float boost = 1;                         // option G: faster a little with each bat hit
        // a bounce off the bat or a wall within the last move: where, and how far through that move
        // (the HD view puts it in the ball's track: between frames it would cut the corner)
        // a brick touched by the remake's ball: it has bounced already; the game is shown the ball in
        // that brick (this point) for its own hit (score, the brick's kind), and its turn is skipped
        bool probe = false;
        float probe_x = 0, probe_z = 0;
        bool contact = false;
        float contact_at = 1, contact_x = 0, contact_z = 0;
    } hb_;
    bool hybrid_ = true;
    bool speed_up_ = false;  // option G (Even): the ball gains speed as the bat hits it, until it is lost
    // Even: the bat moved by the remake too (the game gets no directions): a fractional centre, sped up
    // from the keys (or the pad), straight after the mouse
    void hybrid_bat(Spectrum&);
    float bat_centre(const Spectrum&) const;  // the remake's if it has the bat, else the game's
    BBPaddleBody hybrid_body(const Spectrum&) const;  // the bat as the HD view draws it (floor X / Z)
    BBPaddleBody body_at(const Spectrum&, float centre) const;  // the same with its centre there
    float bat_x_ = -1, bat_v_ = 0;
    int bat_written_ = -1, bat_dir_ = 0;
    uint64_t bat_frame_ = 0;
    int bat_drawn_ = -1;  // the centre the game last redrew the bat at (original pictures)  // the frame of its last move (a game pass can come twice in a frame, or not)
    float bat_target_ = -1;
    void modern_paddle_move(Spectrum&);
    void publish_modern_hit(Spectrum&, BBPaddleHit, std::optional<uint64_t> tick = {});
    BBPaddleHit resolve_modern_hit(Spectrum&, BBPaddleSweep, BBPaddleBody, float paddle_dx, bool step);
    bool modern_was_falling_ = false, modern_episode_ = false;
    std::optional<BBPaddleBody> modern_previous_body_;
    std::optional<int> even_pending_paddle_centre_;
    std::optional<BBPaddleHit> modern_pending_hit_;
    std::optional<std::pair<uint64_t,uint64_t>> modern_event_clock_; // event id, machine tick
    uint64_t modern_last_ball_tick_=0,modern_last_contact_tick_=0;
    std::optional<std::pair<float,uint64_t>> even_previous_paddle_clock_;
    std::optional<std::pair<float,uint64_t>> even_paddle_clock_; // native left, machine tick
    BBPaddleHit paddle_hit_;
    uint64_t paddle_hit_id_ = 0, sounded_paddle_hit_ = 0;
    std::map<int, Track> tracks_;
    // double slabs: the upper plate glides from height to height over the time the last step took
    struct Lift {
        float from, to;
        double t, dur;
        float at(double now) const { return from + (to - from) * float(std::clamp((now - t) / dur, 0.0, 1.0)); }
    };
    std::map<int, Lift> lifts_;
    std::vector<std::pair<BBObject, int>> bursts_;
    uint64_t passes_ = 0;
    double perf_t0_ = 0;
    bool paced_ = false;
    int pending_level_ = -1;
    bool building_ = false;  // a level start before its objects are built: none are read
    bool bat_eaten_ = false;  // unlimited lives: the bat eaten up, the game told it was a lost ball
    uint16_t eater_ = 0;      // the object of the creature that landed on the bat
    // how far the picture runs behind the game: BB_RENDER_DELAY at the even pace; at the original's,
    // the ball's longest recent step (its slowdowns), so every move is a glide, never a wait
    double delay_ = BB_RENDER_DELAY, ball_moved_t_ = -1;
    std::deque<double> ball_steps_;
    // at the original's pace each object glides from where it is shown to where the game has put it,
    // over the last step duration. The ball may coast up to half a step beyond
    // the endpoint when its surrounding space is clear; other objects never predict.
    struct Glide {
        float from[5], to[5];
        float coast = 0;  // maximum predicted fraction of a step
        double t0 = 0, dur = 0.04, changed = -1;  // the glide's start and length; the last new position
        void at(double t, float* out) const {
            float k = float(std::max((t - t0) / dur, 0.0));
            if (k > 1) k = coast > 0 ? 1 + coast * (1 - std::exp(-(k - 1) / coast)) : 1;
            for (int i = 0; i < 5; i++) out[i] = from[i] + (to[i] - from[i]) * k;
        }
    };
    std::map<int, Glide> glides_;
    BBOriginalPreview preview_;
    BBPaddleMotion paddle_motion_;
    BBContactMotion contact_motion_;
    bool preview_enabled_ = false;  // BB1 presentation; BB_ORIGINAL_PREVIEW=0 opts out
    bool original_speed_ = false;  // option O: the original's own pace (its slowdowns, its ball clock), no pacing
    bool unlimited_lives_ = false, unlimited_missiles_ = false;  // options (L, K); off = the original  // a jump asked for: done as the CPU gets to the next play pass
    int gain_ = 0;           // score gained lately, for the next burst's "+points"
    double gain_t_ = -1;  // bricks gone, points, for the renderer
    double game_t_ = 0;
    Mode mode_ = Mode::Boot;
    uint64_t frame_ = 0;
    bool instructions_active_ = false;
    int instructions_page_ = -1, instructions_dir_ = 0;
    uint64_t seen_menu_ = 0, seen_play_ = 0, seen_entry_ = 0;  // frame + 1 of the last sighting
    uint64_t seen_ctrl_ = 0, seen_keys_ = 0, seen_page_ = 0;
    BBMenu::Screen screen_ = BBMenu::Screen::None;  // which menu page is up (None: play, boot)
    BBMenu::Screen input_screen_ = BBMenu::Screen::None;
    bool wait_release_ = false;  // a new page: keys held from the last one don't count
    int cursor_ = 0;                                // main menu item, as last seen
    int menu_dir_ = 0, menu_held_ = 0;              // the arrow menu: the direction held, for how many frames
    // the remake's own items under the game's last (HD): OPTIONS (the remake's settings), EXIT
    int extra_ = 0;                                    // the one under the bar (1, 2; 0: the game's)
    bool options_taken_ = false, exit_taken_ = false;  // chosen
    uint8_t menu_dirs_ = 0;                            // the directions held last frame
    int back_frames_ = 0;                              // Esc on a menu page: frames its "any key" is held
    uint16_t instructions_sp_ = 0;                     // BB1's instructions: the stack as they began
    uint64_t screen_since_ = 0;                     // frame the screen came up
    uint64_t left_menu_ = 0;                        // frame + 1 PLAY GAME was taken (the menu's wipe)
    uint64_t slide_start_ = 0;                      // frame + 1 the level card's room started coming in
    int seen_level_ = -1;                           // the level number last frame
    uint64_t level_changed_ = 0;                    // frame + 1 it changed (its card until play is back)
    bool slide_armed_ = false;                      // the card's playfield seen blank (the menu's gone)
    std::set<int> seen_slots_;                      // the bricks of the last frame
    std::map<int, double> drops_;                   // bricks dropping in: when each starts its fall
    void drop_in(BBState&, double t);
    BBMenu::Screen last_screen_ = BBMenu::Screen::None;
    std::string fonts_dir_, intro_path_, creatures_path_;
    // sound effects: asked for, not yet taken; the ball as last heard (its last step's direction)
    std::vector<SfxEvent> sfx_;
    struct HeardBall {
        bool have = false;
        float x = 0, z = 0, h = 0;
        int dx = 0, dz = 0;
        double hit_t = -1, land_t = -1, brick_t = -1;  // (a brick: its own sound, no bounce)
    } heard_;
    int lives_ = 0;
    bool remake_ = false, music_restarted_ = false;
    int cleared_level_ = -1;
    bool had_bricks_ = false, advance_ = false;  // a level's bricks seen; the next level asked for
    double cleared_t_ = -1;                      // when the last brick's explosion ended  // a new game: the tune from the top
    void read_menu(const Spectrum&, BBMenu&) const;
    // The mouse: the bat is steered to the point of its rail nearest the pointer (the joystick held
    // that way until it is there, so it keeps the original's speed); the HD view marks that point
    int pick_ = -1;  // a menu item clicked (tapped), the bar on its way to it
    int walk_ = -1;
    int pending_rules_ = -1;  // R in play: the rules to switch to (1 the 1987 ones, 0 the remake's)
    std::vector<uint8_t> carry_;  // the screen's objects as they stood, put back once it is built again
    uint16_t carry_end_ = 0;
    int carry_level_ = -1;
    float drag_target_ = -1;  // a finger steering: where the bat goes (-1 none)
    bool drag_on_ = false;  // the item a swipe steps to, the bar on its way
    bool shown_original_ = false;  // the R view (the pointer is the system's there)
    float aim_ = -1;               // the bat centre's X the mouse asks for (-1: not steering)
    bool click_was_ = false;       // the mouse button, as it was at the last input (a click is its press)
    bool fast_mouse_ = false;      // option U (a cheat): the bat keeps up with the mouse
    int bat_last_ = -1, bat_step_ = 2;  // the bat centre last frame, its last move
    float rail_x(const Spectrum&, Vector2 pointer) const;
};
