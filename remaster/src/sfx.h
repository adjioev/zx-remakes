#pragma once

// The remake's own sound effects (a game's module asks for them; the runner mixes them in), made
// in code: short, soft-attacked, in tune with the music (A minor). Made on the game loop's thread.

#include <cstdint>
#include <vector>

enum class Sfx { BatHit, Bounce, Land, Brick, Bomb, Missile, LifeLost, Bonus, LevelStart };

struct SfxEvent {
    Sfx kind;
    float pan = 0;   // -1 left .. 1 right
    float gain = 1;
    int pitch = 0;   // semitones up (e.g. by the brick's points)
};

class SfxBank {
public:
    explicit SfxBank(int sample_rate) : sr_(float(sample_rate)) {}
    void play(const SfxEvent& e);
    // add the sounds playing into n stereo frames (interleaved)
    void mix(float* out, int n);

private:
    struct Voice {
        SfxEvent e;
        float t = 0, ph = 0, ph2 = 0, lp = 0, lp2 = 0, hp = 0;
    };
    float noise();
    float sr_;
    std::vector<Voice> voices_;
    uint32_t rng_ = 98765;
};
