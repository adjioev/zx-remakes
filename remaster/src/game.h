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
#include <vector>

#include <raylib.h>

#include "gamefile.h"
#include "sfx.h"
#include "spectrum.h"

class MusicRemix;

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
    bool mouse = false;          // the mouse steers: moved or clicked since the last direction key
    Vector2 pointer{-1, -1};     // where it points, in canvas pixels
    bool click = false;          // its left button, held
    bool any() const { return dirs || fire || ok || pad_b || pad_x || click || !keys.empty(); }
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
    // The module's own menu items, taken: OPTIONS (the runner opens its options, the pause screen),
    // EXIT (back to the game list).
    virtual bool take_options() { return false; }
    virtual bool take_exit() { return false; }
    // Esc (gamepad Start, right click), the module first: Pause (in play), Options (the runner's
    // options, from the game's menu), Done (the module took it, e.g. back from a menu page).
    enum class Escape { Pause, Options, Done };
    virtual Escape escape() { return Escape::Pause; }
    // The module shows where the mouse aims itself now: the runner hides the system pointer.
    virtual bool hides_pointer() const { return false; }
    // Size of the picture the module draws into (the runner fits it into the window).
    virtual Vector2 canvas_size() const { return {1920, 1536}; }
    // Draw the picture into the canvas (already the render target); original: the R view,
    // the 1987 graphics only. t: game time in seconds, continuous (frames run + the part of the
    // next one already due), for smoothing and effects.
    virtual void draw(const Spectrum&, bool original, double t) = 0;
    // The controls, shown on the pause screen (lines).
    virtual std::string help() const;
    // How the pause screen and the toasts look (the module's look; original: the R view is shown)
    struct PauseStyle {
        Color panel{17, 22, 31, 255}, accent{255, 214, 96, 255}, text{245, 245, 245, 255}, muted{155, 166, 183, 255};
        Color key{36, 44, 58, 255}, rule{49, 59, 74, 255}, hover{48, 58, 78, 255};
        const Font* font = nullptr;  // null: raylib's own
    };
    virtual PauseStyle pause_style(bool /*original*/) const { return {}; }
    // A remake key (a letter, while letters_free(), or on the pause screen) not taken by the
    // runner: a game option. Returns what to show (a toast), empty if not one of the game's.
    virtual std::string on_key(Spectrum&, int /*raylib key*/) { return ""; }
    // The game reads no letters right now: the remake's letter keys (R) are free to use.
    virtual bool letters_free() const { return false; }
    // Where the original picture sits in the canvas (the R view), for the R transition.
    virtual Rectangle original_rect() const;
    // Levels the pause screen can jump to (0: none), the current one, and the jump itself.
    virtual int levels() const { return 0; }
    virtual int level(const Spectrum&) const { return 0; }
    virtual bool can_jump() const { return false; }
    virtual void jump_to(Spectrum&, int /*level*/) {}
    // The remake's sound: the game's tune re-arranged (with the remake's picture; R: the original's),
    // and the sound effects it asked for since the last call (played with the remastered sound).
    virtual std::unique_ptr<MusicRemix> make_remix(int sample_rate) const;  // none by default (games.cpp)
    virtual void take_sfx(std::vector<SfxEvent>&) {}
    // The remake (R: or the original 1987 game): its picture, its sound, and the module's own
    // touches (e.g. the tune from the top on a new game, the next level once one is cleared).
    virtual void set_remake(bool) {}
    // The game started its tune from the top since the last call (the remix starts over with it).
    virtual bool music_restarted() { return false; }

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
// The machine with the game's file in: a snapshot loaded, or a tape loaded (Spectrum::from_tape,
// progress as there). cache_dir set: a tape's machine is kept there once loaded (<sha1>.state) and
// taken from there the next times, at once.
std::unique_ptr<Spectrum> load_machine(const LoadedGame& game, const std::string& cache_dir = "",
                                       const Spectrum::TapeProgress& progress = {});
// The file the machine of a tape would be kept in, if there is one (cached_machine: it exists).
bool cached_machine(const LoadedGame& game, const std::string& cache_dir);

GameExit run_game(const LoadedGame& game, const std::string& assets, const std::string& test_dir,
                  const std::string& script);
