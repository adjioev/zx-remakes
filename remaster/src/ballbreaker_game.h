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
#include <deque>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "ballbreaker_hd.h"
#include "game.h"

class BallBreakerView : public OriginalView {
public:
    void start(Spectrum&, const std::string& assets) override;
    void input(Spectrum&, const Controls&) override;
    void after_frame(Spectrum&) override;
    std::string help() const override;
    Vector2 canvas_size() const override { return {float(BBRenderer::W), float(BBRenderer::H)}; }
    void prepare(const Spectrum&, bool original, double t) override;
    void draw(const Spectrum&, bool original, double t) override;

    bool letters_free() const override { return mode_ != Mode::NameEntry; }
    std::string on_key(Spectrum&, int key) override;
    int levels() const override { return 50; }
    int level(const Spectrum& s) const override;
    bool can_jump() const override { return mode_ == Mode::Play; }
    void jump_to(Spectrum&, int level) override;
    std::unique_ptr<MusicRemix> make_remix(int sample_rate) const override;
    void take_sfx(std::vector<SfxEvent>& out) override;
    void set_remake(bool on) override { remake_ = on; }
    bool music_restarted() override { return std::exchange(music_restarted_, false); }

    // Where the game is, from the code it ran last frame.
    enum class Mode { Boot, Menu, Play, NameEntry, Screen };  // Screen: a menu page waiting for a key
    Mode mode() const { return mode_; }

private:
    bool hd_now(bool original) const;
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
    bool bat_eaten_ = false;  // unlimited lives: the bat eaten up, the game told it was a lost ball
    uint16_t eater_ = 0;      // the object of the creature that landed on the bat
    bool unlimited_lives_ = false, unlimited_missiles_ = false;  // options (L, K); off = the original  // a jump asked for: done as the CPU gets to the next play pass
    int gain_ = 0;           // score gained lately, for the next burst's "+points"
    double gain_t_ = -1;  // bricks gone, points, for the renderer
    double game_t_ = 0;
    Mode mode_ = Mode::Boot;
    uint64_t frame_ = 0;
    uint64_t seen_menu_ = 0, seen_play_ = 0, seen_entry_ = 0;  // frame + 1 of the last sighting
    uint64_t seen_ctrl_ = 0, seen_keys_ = 0, seen_page_ = 0;
    BBMenu::Screen screen_ = BBMenu::Screen::None;  // which menu page is up (None: play, boot)
    BBMenu::Screen input_screen_ = BBMenu::Screen::None;
    bool wait_release_ = false;  // a new page: keys held from the last one don't count
    int cursor_ = 0;                                // main menu item, as last seen
    uint64_t screen_since_ = 0;                     // frame the screen came up
    uint64_t left_menu_ = 0;                        // frame + 1 PLAY GAME was taken (the menu's wipe)
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
};
