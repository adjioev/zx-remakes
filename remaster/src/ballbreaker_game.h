#pragma once

// Ball Breaker (CRL Group, 1987, 128K): breakout in an isometric room (addresses:
// notes/ballbreaker.md). In play the HD view (ballbreaker_hd.cpp, 16:9) replaces the picture;
// menus, the hi-score table and the level intro show the original, centred.
//
// Play runs one original game-loop pass per 50 Hz frame, with CPU headroom for
// expensive redraws. Ball updates use the same clock at 25 Hz; the original
// interrupt-driven audio and countdowns keep their rate.
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

    // Where the game is, from the code it ran last frame.
    enum class Mode { Boot, Menu, Play, NameEntry };
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
    void smooth_apply(BBState&, double t) const;
    static void track_pos(const Track&, double t, float* out);

    std::unique_ptr<BBRenderer> hd_;
    BBState state_, latest_;
    std::map<int, Track> tracks_;
    std::vector<std::pair<BBObject, int>> bursts_;
    uint64_t passes_ = 0;
    bool paced_ = false;
    int gain_ = 0;           // score gained lately, for the next burst's "+points"
    double gain_t_ = -1;  // bricks gone, points, for the renderer
    double game_t_ = 0;
    Mode mode_ = Mode::Boot;
    uint64_t frame_ = 0;
    uint64_t seen_menu_ = 0, seen_play_ = 0, seen_entry_ = 0;  // frame + 1 of the last sighting
};
