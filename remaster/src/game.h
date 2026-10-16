#pragma once

// What a game module adds on top of the original running in the emulator. The runner
// (runner.cpp) does everything common to all games: the emulator at 50 Hz, reading the
// keyboard and gamepad, sound, pause screen, test scripts. A module turns the player's
// (modern) controls into what the original reads, reads the game's state from RAM after
// every emulated frame, and draws the picture; the default one passes the keyboard through
// and draws the original screen.

#include <cstdint>
#include <memory>
#include <set>
#include <string>

#include <raylib.h>

#include "gamefile.h"
#include "spectrum.h"

enum class GameExit { Quit, Back };  // Back: to the game list

// The player's input in a frame, as the remake sees it.
struct Controls {
    uint8_t dirs = 0;       // ZXC_JOY_LEFT/RIGHT/UP/DOWN: arrows, WASD, gamepad stick / d-pad
    uint8_t arrows = 0;     // the same without WASD (on the Spectrum those are letters)
    bool fire = false;      // Space, Ctrl, gamepad A
    bool trigger = false;   // Ctrl, gamepad A (fire that is not also a Spectrum key)
    bool ok = false;        // Enter, Space, gamepad A
    bool pad_b = false, pad_x = false;
    std::set<int> keys;     // the PC keyboard as the Spectrum's, held now (chips codes:
                            // letters, digits, ' ', 0x0D Enter, 0x0E CAPS, 0x0F SYMBOL SHIFT)
    bool any() const { return dirs || fire || ok || pad_b || pad_x || !keys.empty(); }
};

class GameView {
public:
    virtual ~GameView() = default;

    // Every emulated frame, before it runs: turn the controls into keys / joystick for the
    // original (through apply()). Default: arrows + Ctrl / gamepad A are a Kempston
    // joystick, the keyboard is the Spectrum's, gamepad B / X are Space / Enter.
    virtual void input(Spectrum&, const Controls&);

    // Once, with the snapshot loaded and nothing run yet: hooks go in here.
    virtual void start(Spectrum&, const std::string& /*assets*/) {}
    // After every emulated frame.
    virtual void after_frame(Spectrum&) {}
    // Before draw, outside the canvas: render offscreen layers (e.g. glow) here.
    virtual void prepare(const Spectrum&, bool /*original*/, double /*t*/) {}
    // Size of the picture the module draws into (the runner fits it into the window).
    virtual Vector2 canvas_size() const { return {1920, 1536}; }
    // Draw the picture into the canvas (already the render target); original: the R view,
    // the 1987 graphics only. t: game time in seconds, continuous (frames run + the part of the
    // next one already due), for smoothing and effects.
    virtual void draw(const Spectrum&, bool original, double t) = 0;
    // The controls, shown on the pause screen (lines).
    virtual std::string help() const;

protected:
    // Hold exactly these Spectrum keys and this joystick (ZXC_JOY_* bits) from now on.
    void apply(Spectrum&, const std::set<int>& keys, uint8_t joy);

private:
    std::set<int> held_;
    uint8_t joy_ = 0;
};

// The original picture, border included, scaled to fill the canvas.
class OriginalView : public GameView {
public:
    OriginalView();
    ~OriginalView() override;
    void draw(const Spectrum&, bool original, double t) override;

protected:
    void draw_original(const Spectrum&, Rectangle dest);
    // The original picture, as large as fits the canvas, centred.
    void draw_original_fit(const Spectrum&);

private:
    Texture2D tex_{};
    std::unique_ptr<uint32_t[]> rgba_;
};

// The module for a game (OriginalView for games without HD art yet).
std::unique_ptr<GameView> make_view(GameId id);

GameExit run_game(const LoadedGame& game, const std::string& assets, const std::string& test_dir,
                  const std::string& script);
