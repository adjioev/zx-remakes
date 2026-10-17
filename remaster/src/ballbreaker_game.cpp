#include "ballbreaker_game.h"
#include "music_remix.h"

#include <algorithm>

#include <rlgl.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

// Ball Breaker (notes/ballbreaker.md). Code run once a frame: menu_loop the main menu and the
// control choice, play_loop the sprite compositor (every frame of play), entry_loop typing a name
// into the hi-score table, page_loop a page up (hi-scores, instructions, the name entry),
// ctrl_loop the control choice (REDEFINE KEYS), keys_loop LIST KEYS waiting for a key.
// menu_cursor: the item under the menu's bar (what ENTER takes), 0-5, the menu's own variable on
// its stack (0x8077 is its arrow, which can be an item ahead). main_loop: entry to a complete play
// pass; pass_end its end (LD A,(0x806C), loop again if 0); level_start the game's own start of a
// level (at the top: SP = 0); play_result acts on the play reason, the bat's sprite restored.
// paddle_hit: collision accepted, outgoing Z direction stored; creature_lands: a creature lands on
// the bat (IX = its object); creature_caught: just after the bat, eaten up, set the reason to 4;
// bat_eaten: a creature is eating the bat (its sprite saved at 0x5B95). tune_start: the music
// player, the tune from the top. hiscores: 9 x [12 name, word score / 100]; instructions:
// [sprite][text, last char + 0x80, '*' new line] .. 0xFF; points: 3 bytes a sprite, points / 100
// first. trainer: DEC (HL) (0x35), a life lost.
const BBGame BALL_BREAKER = {
    "ballbreaker", "BALL BREAKER", "CRL GROUP PLC 1987", 0, {},
    0x9DA7, 0x854C, 0xABC0, 0x86A5, 0x990B, 0xA086,
    0xFFF8, 0, 0,
    0x8F57, 0x8BD3, 0x9138, 0x8B78, 0x8BE8,
    0x8A73, 0x96DC, 0x8F97, 0x808C,
    0x9030, 0x8074, 0x8083,
    0xA73B, 0x80AE, 0x8096,
    0x807F, 0x8078, 0x80CB, 0xB228, 0xB893, 0xB9D5,
    0x8C00,
    50,
    {{1, 4, 8, 10, 26, 27, 29, 34, 37, 45, 47, 49}, {2, 7, 13, 16, 25, 30, 33, 35, 39, 40, 42, 46, 50},
     {3, 17, 23, 32, 36, 44}, {6, 11, 18, 21, 38, 41}, {5, 12, 22, 31, 43}, {9, 14, 19, 28, 48}, {15, 20, 24}},
    0xAE11,
    BBTheme::Neon,
    0x89D4,
    0x8B92,
    0x90B2, 0x90D8, 0x80A2, 0x807A, 0x808E,
};

// Ballbreaker II (notes/ballbreaker2.md): its main menu moves an arrow; the item under it is at
// 0x80A4, 1-6 (0: none). The same tune and player (data at the same places, its variables 4 on);
// its tape has no trainer.
const BBGame BALL_BREAKER_2 = {
    "ballbreaker", "BALLBREAKER II", "CRL GROUP PLC 1988", 0xB3CA,
    {"SIMON ROCKMAN", "JEREMY SANDS", "IAN ANDREWS", "THE ZEN ROOM", "CLEMENT CHAMBERS", "ADRIAN TURNER"},
    0x9CD8, 0x856E, 0xACA5, 0x86C7, 0x992D, 0xA1BB,
    0x80A4, 1, 0x80A5,
    0x8F79, 0x8BF5, 0x915A, 0x8B9A, 0x8C0A,
    0x8A95, 0x96FE, 0x8FB9, 0x808B,
    0x9052, 0x8074, 0x8082,
    0xA820, 0x80B2, 0x8095,
    0x807E, 0x8077, 0x80CF, 0xB864, 0xBFB9, 0xC0FB,
    0,
    30,
    {{2, 5, 6, 9, 11, 15, 17, 19, 21, 23, 25, 26, 27, 28, 29, 30}, {1, 16, 22}, {4, 14}, {3, 8, 10, 13, 20}, {}, {},
     {7, 12, 18, 24}},
    0,                   // instructions demonstration: not mapped
    BBTheme::ClassicHD,  // the sequel looks its own: solid, brighter (T: Neon)
    0x89F6,
    0x8BB7,
    0x90D4, 0x90FA, 0x80A1, 0x8079, 0x808D,
};

namespace {

// The same in both games.
constexpr uint16_t BAT_CENTRE = 0x8061, BAT_Z = 0x8062, BAT_WIDTH = 0x8063, BALL_X = 0x8064;
constexpr uint16_t ANIMATIONS_END = 0x804E;   // word: the end of the animation list (explosions...)
constexpr uint16_t SOUND_FX = 0x8076;         // 0: effects in play, else the tune (the menu's last item)
constexpr uint16_t CREATURES = 0x5C3F;        // the creatures: [state, -, -, object word], 0 the end
constexpr uint64_t LEAVE_FRAMES = 45;     // the menu's wipe after PLAY GAME (the original's, ~0.9 s)
constexpr uint64_t RECENT = 10;           // frames a sighting counts for
constexpr uint16_t PLAY_REASON = 0x806C;  // why play stops: 0 on, 2 ball lost, 3 restart, 4 quit; else next level
constexpr double CLEARED_WAIT = 1.0;      // s after the last explosion before the next level (the shards fall)
constexpr float PLAY_CPU = 16;  // headroom only: yield prevents extra game passes
constexpr uint8_t KEMPSTON = 2;  // control type: 0 keyboard, 1 cursor, 2 Kempston, 3 Sinclair
constexpr uint16_t LEVEL = 0x8070, SCORE = 0x8044, MISSILES = 0x8071;
constexpr uint16_t OBJECTS = 0xF3EB, OBJECTS_END = 0x804C;  // 10-byte objects, end pointer
constexpr uint8_t START_LIVES = 3, START_MISSILES = 10;  // what a new game gives (0x8B5C)

// Menu keys and the game's own keyboard controls.
constexpr int MENU_UP = 'a', MENU_DOWN = 'z', MENU_SELECT = ' ';

}  // namespace

// The room (0-6 = A-G) of a level.
int BallBreakerView::room_of(int level) const {
    for (size_t r = 0; r < g_.rooms.size(); r++)
        for (int l : g_.rooms[r])
            if (l == level + 1) return int(r);
    return 0;
}

BBPaddleBody BallBreakerView::modern_body(const Spectrum& s) const {
    uint8_t mask[120];
    int sprite=s.peek(0xf3f8);
    for(int i=0;i<120;++i)mask[i]=s.peek(uint16_t(g_.sprites+std::clamp(sprite,0,59)*120+i));
    float width=std::max(1,int(s.peek(BAT_WIDTH)));
    float left=bb_even_paddle_left(s.peek(BAT_CENTRE),s.peek(BAT_WIDTH));
    return bb_visible_paddle_body(left,width,s.peek(BAT_Z),bb_even_paddle_inset(int(width)),bb_paddle_shape(mask));
}

BBPaddleHit BallBreakerView::resolve_modern_hit(Spectrum& s, BBPaddleSweep hit,
                                              BBPaddleBody body, float paddle_dx, bool step) {
    const float native_width=std::max(1,int(s.peek(BAT_WIDTH)));
    const float inset=bb_even_paddle_inset(int(native_width));
    BBPaddleBody impact=body;impact.x0+=paddle_dx*hit.fraction;impact.x1+=paddle_dx*hit.fraction;
    // Exact impact point, including recovery from integer-coordinate overlap.
    float qx=std::clamp(hit.x,impact.x0,impact.x1)+BB_BALL_RADIUS*hit.nx;
    float qz=std::clamp(hit.z,impact.z0,impact.z1)+BB_BALL_RADIUS*hit.nz;
    int dx=int8_t(s.peek(0x8067)),dz=int8_t(s.peek(0x8066));
    int out_x=dx,out_z=dz;
    if(hit.nz>.999f && dz<0) {
        out_x=bb_paddle_aim(qx,impact.x0,impact.x1-impact.x0);out_z=1;
    } else {
        // Reflect relative to a moving paddle, then choose a separating native
        // direction. Keeping the game's legal increments preserves its clock.
        float vx=(step?float(dx):0.f)-paddle_dx,vz=step?float(dz):0.f;
        float dot=vx*hit.nx+vz*hit.nz;
        float rx=vx-2*dot*hit.nx+paddle_dx,rz=vz-2*dot*hit.nz;
        float len=std::hypot(rx,rz);if(len<1e-5f)rx=hit.nx,rz=hit.nz,len=1;
        float best=-1e10f;
        for(int x=-2;x<=2;++x)for(int z:{-1,1}) {
            if(x*hit.nx+z*hit.nz<=1e-4f)continue;
            float score=(x*rx+z*rz)/(std::hypot(float(x),float(z))*len);
            if(z==dz)score+=.00001f;
            if(score>best)best=score,out_x=x,out_z=z;
        }
    }
    float ex=qx+(1-hit.fraction)*(step?float(out_x):paddle_dx);
    float ez=qz+(1-hit.fraction)*(step?float(out_z):0.f);
    BBPaddleBody final_body=body;final_body.x0+=paddle_dx;final_body.x1+=paddle_dx;
    // RAM is integer. Round towards separation rather than back into the body.
    auto round_out=[](float p,float normal){return normal>1e-5f?std::ceil(p):normal<-.00001f?std::floor(p):std::round(p);};
    ex=round_out(ex,hit.nx);ez=round_out(ez,hit.nz);
    float cx=std::clamp(ex,final_body.x0,final_body.x1),cz=std::clamp(ez,final_body.z0,final_body.z1);
    if(std::hypot(ex-cx,ez-cz)<BB_BALL_RADIUS-.001f) {
        ex=round_out(cx+BB_BALL_RADIUS*hit.nx,hit.nx);
        ez=round_out(cz+BB_BALL_RADIUS*hit.nz,hit.nz);
    }
    // At a rail there may be no room for a sphere between paddle and wall.
    // Resolve towards the playfield, never wrap an unsigned RAM coordinate.
    if(ex<5 || ex>91 || ez<0) {
        ex=std::clamp(ex,5.f,91.f);ez=std::max(ez,std::ceil(final_body.z1+BB_BALL_RADIUS));out_z=1;
    }
    s.poke(0x8067,uint8_t(out_x));s.poke(0x8066,uint8_t(out_z));
    s.poke(BALL_X,uint8_t(int(ex)-(step?out_x:0)));
    s.poke(0x8065,uint8_t(int(ez)-(step?out_z:0)));
    s.poke(0x807a,0); // only a same-step drain mark can be recovered
    BBPaddleHit event;
    event.t=double(frame_+1)/Spectrum::FRAME_HZ;event.geometric=true;
    event.x=qx;event.y=4;event.z=qz;event.nx=hit.nx;event.nz=hit.nz;
    event.bat_left=impact.x0-inset;event.bat_width=native_width;event.bat_z=impact.z1;
    event.along=std::clamp((qx-BB_BALL_RADIUS*hit.nx-impact.x0)/(impact.x1-impact.x0),0.f,1.f);
    return event;
}

void BallBreakerView::publish_modern_hit(Spectrum& s, BBPaddleHit event, std::optional<uint64_t> tick) {
    event.id=++paddle_hit_id_;paddle_hit_=event;modern_episode_=true;
    modern_last_contact_tick_=tick.value_or(s.video_ticks());
    modern_event_clock_=std::pair{event.id,modern_last_contact_tick_};
    if(preview_on())contact_motion_.observe_contact(s,event.x,event.z,event.bat_left,event.bat_width,true,modern_last_contact_tick_);
}

void BallBreakerView::modern_step(Spectrum& s, const zxc_regs_t& r) {
    // Native CALL8A0A owns one return address. RET8A77 resumes at9146, which
    // performs the coordinate add. The sound path at8A73 also returns there.
    const bool active=!modern_was_falling_ && !s.peek(0x809a) && s.peek(0x8083)<=1;
    if(active) {
        auto body=modern_body(s);
        float x=s.peek(BALL_X),z=s.peek(0x8065);
        if(std::hypot(x-std::clamp(x,body.x0,body.x1),z-std::clamp(z,body.z0,body.z1))>BB_BALL_RADIUS+.01f)modern_episode_=false;
        auto hit=bb_sweep_paddle(x,z,x+int8_t(s.peek(0x8067)),z+int8_t(s.peek(0x8066)),body);
        if(hit.hit) {
            auto event=resolve_modern_hit(s,hit,body,0,true);
            if(modern_episode_) {
                if(preview_on())contact_motion_.observe_contact(s,event.x,event.z,event.bat_left,event.bat_width,false);
                s.jump_at(0x8a0a,0x8a77,r.sp);
            } else {
                modern_pending_hit_=event;
                s.jump_at(0x8a0a,0x8a73,r.sp);
            }
            return;
        }
    }
    if(preview_on())contact_motion_.observe(s,false,true);
    s.jump_at(0x8a0a,0x8a77,r.sp);
}

// Even: the remake flies the ball. The original moves it in whole units, 2-4 small steps a batch along
// one of five directions, so its speed and angles jump and it can step past the bat's line. Here the
// ball keeps one speed in any direction, with fractional positions: walls and the bat (its angle from
// where it is hit, the crossing of its line checked) are the remake's; bricks stay the game's (0x8A78
// tests the ball's cell each pass: it flips 0x8066 or 0x8067 and puts the ball back where the batch
// began), and the remake follows: a flipped direction flips its velocity, a moved ball is taken
// where the game put it. The batch is then skipped to its publish (the sprite from 0x8064 / 0x8065).
void BallBreakerView::hybrid_step(Spectrum& s, bool batch) {
    constexpr float SPEED = 2.15f;           // units a frame: the original's ~4.3 a batch, a batch every 2
    constexpr float MAX_ANGLE = 1.05f;       // off the bat's edge (rad, ~60 degrees)
    constexpr float X_MIN = 5, X_MAX = 0x5B, Z_MAX = 0x94, Z_OUT = 4;
    const int ram_x = s.peek(BALL_X), ram_z = s.peek(uint16_t(BALL_X + 1));
    const int ram_dx = int8_t(s.peek(0x8067)), ram_dz = int8_t(s.peek(0x8066));
    HybridBall& b = hb_;
    // a serve, or back from a fall / the original pace: taken from the game at its batch
    const bool fresh = !b.live || frame_ - b.batch > 8;
    if (fresh && !batch) return;
    if (batch) b.batch = frame_;
    float back = 0;
    if (fresh) {
        b.x = float(ram_x), b.z = float(ram_z);
        float dx = float(ram_dx), dz = ram_dz < 0 ? -1.0f : 1.0f, n = std::hypot(dx, dz);
        b.vx = SPEED * dx / n, b.vz = SPEED * dz / n;
        b.frame = frame_ - 1, b.live = true;
    } else {
        if (ram_x != b.wx || ram_z != b.wz) {  // put back (a brick): the way back is part of this batch's
            back = std::hypot(b.x - float(ram_x), b.z - float(ram_z));
            b.x = float(ram_x), b.z = float(ram_z);
        }
        if (b.wdz && ram_dz == -b.wdz) b.vz = -b.vz;
        if (b.wdx && ram_dx == -b.wdx) b.vx = -b.vx;
        else if (!b.wdx && ram_dx) {  // a brick that turns a straight ball aside (0xA689)
            b.vx = SPEED * 0.3f * float(ram_dx > 0 ? 1 : -1);
            b.vz = std::copysign(std::sqrt(SPEED * SPEED - b.vx * b.vx), b.vz);
        }
    }
    const float slow = s.peek16(g_.ball_slow) ? 0.55f : 1.0f;
    const int frames = int(std::min<uint64_t>(frame_ - b.frame, 3));  // a frame a pass; 0: moved this pass
    b.frame = frame_;
    const float dist = std::max(0.0f, SPEED * slow * float(frames) - back);
    const int steps = frames ? std::max(1, int(std::ceil(dist))) : 0;
    const float h = steps ? dist / SPEED / float(steps) : 0;  // velocity is a frame's: a step is h frames
    const float bat_z = s.peek(BAT_Z), centre = s.peek(BAT_CENTRE), half = float(s.peek(BAT_WIDTH) / 2);
    bool out = false;
    for (int i = 0; i < steps && !out; i++) {
        const float pz = b.z;
        b.x += b.vx * h, b.z += b.vz * h;
        if (b.x < X_MIN) b.x = 2 * X_MIN - b.x, b.vx = std::fabs(b.vx);
        if (b.x > X_MAX) b.x = 2 * X_MAX - b.x, b.vx = -std::fabs(b.vx);
        if (b.z > Z_MAX) b.z = 2 * Z_MAX - b.z, b.vz = -std::fabs(b.vz);
        // the bat: the original's interval (ball X + 3 past its left end, X - 6 short of its right),
        // the ball coming down onto its line now or already below it (beside the bat, as the original)
        if (b.vz < 0 && b.z < bat_z) {
            const float t = pz > bat_z && pz != b.z ? (pz - bat_z) / (pz - b.z) : 1.0f;
            const float xc = b.x - b.vx * h * (1 - t);
            if (xc + 3 >= std::max(0.0f, centre - half) && xc - 6 < centre + half) {
                const float along = std::clamp((xc - (centre + 1.5f)) / (half + 4.5f), -1.0f, 1.0f);
                const float a = along * MAX_ANGLE;
                b.vx = SPEED * std::sin(a), b.vz = SPEED * std::cos(a);
                if (pz > bat_z) b.z = bat_z + (bat_z - b.z);
                b.x = xc;
                paddle_hit_ = {++paddle_hit_id_, double(frame_ + 1) / Spectrum::FRAME_HZ, (along + 1) / 2};
            }
        }
        if (b.z < Z_OUT) out = true;
    }
    b.x = std::clamp(b.x, X_MIN, X_MAX), b.z = std::clamp(b.z, 0.0f, Z_MAX);
    b.wx = int(std::lround(b.x)), b.wz = int(std::lround(b.z));
    b.wdz = b.vz < 0 ? -1 : 1;
    b.wdx = std::abs(b.vx) < SPEED * 0.12f ? 0 : std::clamp(int(std::lround(b.vx / SPEED * 2)), -2, 2);
    if (!b.wdx && std::abs(b.vx) >= SPEED * 0.12f) b.wdx = b.vx < 0 ? -1 : 1;
    if (frames) s.poke(g_.ball_start, uint8_t(ram_x)), s.poke(uint16_t(g_.ball_start + 1), uint8_t(ram_z));
    s.poke(BALL_X, uint8_t(b.wx)), s.poke(uint16_t(BALL_X + 1), uint8_t(b.wz));
    s.poke(0x8067, uint8_t(int8_t(b.wdx))), s.poke(0x8066, uint8_t(int8_t(b.wdz)));
    if (out) s.poke(g_.ball_falling, 1), b.live = false;  // past the bat: the game lets it fall
}

void BallBreakerView::modern_paddle_move(Spectrum& s) {
    if(!codex_even()) {modern_previous_body_.reset();modern_episode_=false;return;}
    auto body=modern_body(s);
    if(modern_previous_body_ && !s.peek(0x807a) && !s.peek(0x809a) && s.peek(0x8083)<=1 &&
       std::abs((body.x1-body.x0)-(modern_previous_body_->x1-modern_previous_body_->x0))<.001f) {
        float x=s.peek(BALL_X),z=s.peek(0x8065);
        const auto& old=*modern_previous_body_;
        if(std::hypot(x-std::clamp(x,old.x0,old.x1),z-std::clamp(z,old.z0,old.z1))>BB_BALL_RADIUS+.01f)modern_episode_=false;
        float move=body.x0-modern_previous_body_->x0;
        if(std::abs(move)<=2.01f && std::abs(move)>.001f) {
            auto hit=bb_sweep_paddle(s.peek(BALL_X),s.peek(0x8065),s.peek(BALL_X),s.peek(0x8065),*modern_previous_body_,move);
            if(hit.hit) {
                auto event=resolve_modern_hit(s,hit,*modern_previous_body_,move,false);
                const uint64_t now=s.video_ticks();
                const double hz=s.model()==128?3546894.0:3500000.0;
                // A native paddle move is two units over one 20ms pass. After
                // a rest do not stretch that movement over the whole idle gap.
                uint64_t duration=uint64_t(hz*.02);
                if(even_previous_paddle_clock_)duration=std::min(duration,now-even_previous_paddle_clock_->second);
                uint64_t impact_tick=now-uint64_t((1-hit.fraction)*duration);
                impact_tick=std::min(now,std::max({impact_tick,modern_last_ball_tick_,modern_last_contact_tick_}));
                if(!modern_episode_) {
                    publish_modern_hit(s,event,impact_tick);
                    // A91D's inline sound04 without bypassing the bat projector.
                    if(!s.peek(0x8076))s.poke(0x808d,4);
                } else if(preview_on())contact_motion_.observe_contact(s,event.x,event.z,event.bat_left,event.bat_width,false,impact_tick);
                modern_last_contact_tick_=impact_tick;
            }
        }
    }
    modern_previous_body_=body;
    if(preview_on()) {
        if(s.peek(0x807a))contact_motion_.observe_falling_paddle(s);
        else contact_motion_.observe(s,false,true);
    }
}

void BallBreakerView::start(Spectrum& s, const std::string& game_assets) {
    const char* preview = std::getenv("BB_ORIGINAL_PREVIEW");
    preview_enabled_ = g_.paddle_hit == 0x8a73 && (!preview || std::string(preview) != "0");
    // Keep the normal Even-speed default. Explicit opt-in retains its historical
    // Original-speed launch shortcut; O always selects the verified smoothing.
    if (preview_enabled_ && preview) original_speed_ = true;
    if (const char* h = std::getenv("BB_HYBRID")) hybrid_ = std::string(h) != "0";
    // the HD art is the first game's, shared (assets/<g_.art>)
    const std::string assets = game_assets.substr(0, game_assets.find_last_of('/') + 1) + g_.art;
    fonts_dir_ = assets + "/../fonts";
    // the title picture: the game's own if it has one (assets/<key>/intro.png), else the shared one
    intro_path_ = FileExists((game_assets + "/intro.png").c_str()) ? game_assets + "/intro.png" : assets + "/intro.png";
    creatures_path_ = assets + "/creatures/frames.png";
    // the hi-score table, kept between runs: <data>/<key>.hiscores (not for test runs)
    if (!std::getenv("ZXREMAKES_TEST")) {
        hiscores_file_ = user_data_dir("ZX Remakes") + game_assets.substr(game_assets.find_last_of('/') + 1) + ".hiscores";
        std::ifstream in(hiscores_file_, std::ios::binary);
        hiscores_saved_.assign(std::istreambuf_iterator<char>(in), {});
        if (hiscores_saved_.size() != HISCORES_SIZE) hiscores_saved_.clear();
    }
    s.on_exec(g_.ctrl_loop, [this](const zxc_regs_t&) { seen_ctrl_ = frame_ + 1; });
    s.on_exec(g_.keys_loop, [this](const zxc_regs_t&) { seen_keys_ = frame_ + 1; });
    s.on_exec(g_.page_loop, [this](const zxc_regs_t&) { seen_page_ = frame_ + 1; });
    s.on_exec(g_.menu_loop, [this, &s](const zxc_regs_t& r) {
        seen_menu_ = frame_ + 1;
        instructions_active_ = false;
        instructions_page_ = -1, instructions_dir_ = 0;
        // BB1's menu painter receives the selection in A. Its stack slot is
        // reused as soon as a page opens, sometimes with another valid index.
        int c = g_.cursor_base == 0 ? int(r.af >> 8) : int(s.peek(g_.menu_cursor)) - g_.cursor_base;
        if (c >= 0 && c <= 5) cursor_ = c;
    });
    if (g_.instructions_entry) s.on_exec(g_.instructions_entry, [this](const zxc_regs_t& r) {
        instructions_sp_ = r.sp;  // its return to the menu on top
        instructions_active_ = true;
        instructions_page_ = -1, instructions_dir_ = 0;
        left_menu_ = 0;
    });
    s.on_exec(g_.play_loop, [this](const zxc_regs_t&) { seen_play_ = frame_ + 1; });
    s.on_exec(g_.entry_loop, [this](const zxc_regs_t&) { seen_entry_ = frame_ + 1; });
    Spectrum* sp = &s;
    if (g_.paddle_hit == 0x8a73) s.on_exec(0x9cbb, [this, sp](const zxc_regs_t&) {
        // BB1 has just chosen the new ball's direction, before projecting its
        // drop animation. Even always serves along the same diagonal; the native
        // rule instead changes X depending on whether the bat is centred.
        // Set this once at creation, never steer an already moving ball.
        if (codex_even()) sp->poke(0x8067, uint8_t(0xff));
    });
    if (g_.paddle_hit == 0x8a73) s.on_exec(0x886b, [this, sp](const zxc_regs_t&) {
        if(codex_even() && !sp->peek(0x809a) && sp->peek(0x8083)<=1)modern_last_ball_tick_=sp->video_ticks();
        if (preview_on()) {
            if (original_speed_) preview_.microstep(*sp);
            contact_motion_.observe(*sp, false, !original_speed_, g_.paddle_hit == 0x8a73);
        }
    });
    if (g_.paddle_hit == 0x8a73) {
        s.on_exec(0x9140, [this, sp](const zxc_regs_t&) { modern_was_falling_ = sp->peek(0x807a) != 0; });
        s.on_exec(0x88ad, [this, sp](const zxc_regs_t&) {
            if(codex_even()) {
                if(even_pending_paddle_centre_) {
                    sp->poke(BAT_CENTRE,uint8_t(*even_pending_paddle_centre_));
                    even_pending_paddle_centre_.reset();
                }
                // Also handle width changes and switching from Original at a rail.
                sp->poke(BAT_CENTRE, uint8_t(bb_even_paddle_centre(sp->peek(BAT_CENTRE),sp->peek(BAT_WIDTH))));
                float left=bb_even_paddle_left(sp->peek(BAT_CENTRE),sp->peek(BAT_WIDTH));
                if(!even_paddle_clock_ || std::abs(left-even_paddle_clock_->first)>.001f) {
                    even_previous_paddle_clock_=even_paddle_clock_;
                    even_paddle_clock_=std::pair{left,sp->video_ticks()};
                }
            } else { even_paddle_clock_.reset(); even_pending_paddle_centre_.reset(); }
            modern_paddle_move(*sp);
        });
        s.on_exec(0x8a0a, [this, sp](const zxc_regs_t& r) {
            if (codex_even()) modern_step(*sp, r);
            else if (preview_on()) contact_motion_.observe(*sp, false, false, true);
        });
    }
    // option U (a cheat): the bat keeps up with the mouse. Before the game's own step (2 a pass, after
    // its checks: the bat not eaten, a direction held), up to 3 more of the same, by the same rule
    // (left while centre > width / 2 + 1, right while centre + width / 2 < 0x5F); its step ends there
    if (g_.bat_move)
        s.on_exec(g_.bat_move, [this, sp](const zxc_regs_t& r) {
            if (codex_even() && g_.paddle_hit == 0x8a73) {
                int centre = sp->peek(BAT_CENTRE), width = sp->peek(BAT_WIDTH);
                int steps = 1;
                const int dir = r.bc & 4 ? -1 : r.bc & 2 ? 1 : 0;
                if (fast_mouse_ && aim_ >= 0 && (aim_ - centre) * dir > 0)
                    steps = std::clamp(int(std::fabs(aim_ - centre)) / 2, 1, 4);
                for (int i = 0; i < steps; ++i) {
                    if (r.bc & 4) centre = bb_even_paddle_centre(centre - 2, width);
                    if (r.bc & 2) centre = bb_even_paddle_centre(centre + 2, width);
                }
                // Let LD HL,8061 and the entire native routine execute normally.
                // Apply our rail-limited position at its projector (88AD), after
                // the native writes and before collision/presentation observation.
                even_pending_paddle_centre_ = centre;
                return;
            }
            if (!fast_mouse_ || aim_ < 0) return;
            const int dir = r.bc & 4 ? -1 : r.bc & 2 ? 1 : 0;  // C: bit 2 left, bit 1 right
            int c = sp->peek(BAT_CENTRE);
            const int half = sp->peek(BAT_WIDTH) / 2;
            const int steps = std::min(int(std::fabs(aim_ - float(c))) / 2, 4);
            if (!dir || (aim_ - float(c)) * float(dir) <= 0) return;
            for (int i = 1; i < steps; i++) {
                if (dir < 0 ? c <= half + 1 : c + half >= 0x5F) break;
                c += 2 * dir;
            }
            sp->poke(BAT_CENTRE, uint8_t(c));
        });
    if (g_.ball_batch)
        s.on_exec(g_.ball_batch, [this, sp](const zxc_regs_t& r) {
            // the ball moves every pass (see main_loop); its batch only checks in, then is skipped
            if (!hybrid()) { hb_.live = false; return; }
            hybrid_step(*sp, true);
            sp->jump_at(g_.ball_batch, g_.ball_batch_end, r.sp);
        });
    // Observe the original's confirmed collision; never infer a paddle hit from
    // a render-frame velocity change (shallow turns and frame rate made that unreliable).
    s.on_exec(g_.paddle_hit, [this, sp](const zxc_regs_t&) {
        if (modern_pending_hit_) {
            publish_modern_hit(*sp, *modern_pending_hit_);
            modern_pending_hit_.reset();
            return;
        }
        float width = std::max(1, int(sp->peek(BAT_WIDTH)));
        float left = float(sp->peek(BAT_CENTRE) - sp->peek(BAT_WIDTH) / 2);
        if (codex_even() && g_.paddle_hit == 0x8a73) {
            // The original has accepted contact and reversed Z. Even replaces
            // only its additive X response: aim follows the visible hit position,
            // independent of incoming X. Do not widen the collision interval.
            const float inset = bb_even_paddle_inset(int(width));
            sp->poke(0x8067, uint8_t(bb_paddle_aim(sp->peek(BALL_X), left + inset, width - inset)));
        }
        paddle_hit_ = {++paddle_hit_id_, double(frame_ + 1) / Spectrum::FRAME_HZ,
                       std::clamp((sp->peek(BALL_X) - left - (g_.paddle_hit == 0x8a73 ? 3 : 0)) /
                                  std::max(1.0f, width - (g_.paddle_hit == 0x8a73 ? 3 : 0)), 0.0f, 1.0f)};
        if (preview_on()) {
            if (original_speed_) preview_.contact(paddle_hit_.t, sp->peek(BALL_X), sp->peek(BAT_Z));
            contact_motion_.observe(*sp, true, !original_speed_, g_.paddle_hit == 0x8a73);
        }
    });
    // a creature that lands on the bat eats it a few pixels a pass (0xAC50); the bat gone, the
    // original ends the whole game, whatever the lives (0x8F8A). With unlimited lives it costs a
    // life as a lost ball does (code 2: the bricks stay, the ball is served again) and, once the
    // game has restored the bat, the eating stops (the game itself clears it only at a level start)
    // and the creature is blown up (the game never removes it: it would stay on the bat's line)
    s.on_exec(g_.creature_lands, [this](const zxc_regs_t& r) { eater_ = r.ix; });
    s.on_exec(g_.creature_caught, [this, sp](const zxc_regs_t&) {
        if (unlimited_lives_ && sp->peek(PLAY_REASON) == 4) sp->poke(PLAY_REASON, 2), bat_eaten_ = true;
    });
    s.on_exec(g_.play_result, [this, sp](const zxc_regs_t&) {
        // blown up as a missile does it (0x9B4A): an entry in the game's animation list
        auto blow_up = [sp](uint16_t obj) {
            uint16_t at = uint16_t(sp->peek16(ANIMATIONS_END) - 1);
            const uint8_t entry[6] = {4, 5, uint8_t(obj), uint8_t(obj >> 8), 0x18, 0};
            for (int i = 0; i < 6; i++) sp->poke(uint16_t(at + i), entry[i]);
            sp->poke(ANIMATIONS_END, uint8_t(at + 6)), sp->poke(ANIMATIONS_END + 1, uint8_t((at + 6) >> 8));
        };
        if (bat_eaten_) {
            sp->poke(g_.bat_eaten, 0), bat_eaten_ = false;
            if (eater_) blow_up(eater_), eater_ = 0;
        }
        // unlimited lives, the ball lost (or the bat eaten): every creature about goes too, as if
        // hit by a missile; else they keep coming for the bat and the ball is never served again.
        // Their table (both games: 0x5C3F, 5 bytes each: state (0 the end, 0xFF dead), ..., object)
        if (unlimited_lives_ && sp->peek(PLAY_REASON) == 2)
            for (uint16_t e = CREATURES; sp->peek(e) != 0 && e < CREATURES + 5 * 16; e = uint16_t(e + 5)) {
                uint8_t state = sp->peek(e);
                uint16_t obj = sp->peek16(uint16_t(e + 3));
                if (state == 0xFF || state == 0xFE || !obj || sp->peek(uint16_t(obj + 1)) == 0) continue;
                sp->poke(e, 0xFF);  // dead, as the missile's hit leaves it (0x9679)
                blow_up(obj);
            }
    });
    s.on_exec(g_.main_loop, [this, sp](const zxc_regs_t&) {
        if (pending_level_ >= 0) {  // the jump (jump_to): the level number, then the level start
            sp->poke(LEVEL, uint8_t(pending_level_));
            pending_level_ = -1;
            paced_ = false;
            sp->set_turbo(1);
            return;
        }
        static const bool off = std::getenv("BB_NO_TURBO") != nullptr;
        passes_++;
        // Even: the remake's ball in flight moves a frame each pass, before the game tests the bricks
        if (hybrid() && hb_.live && frame_ - hb_.batch <= 4 && !sp->peek(g_.ball_falling)) hybrid_step(*sp, false);
        // The serve/death countdown contains HALT and already waits on interrupts.
        // Adding a frame wait there would double those intentional pauses.
        paced_ = !off && !original_speed_ && sp->peek(g_.serve_countdown) <= 1;
        sp->set_turbo(paced_ ? PLAY_CPU : 1);
        if (paced_) sp->yield_frame();
    });
    s.on_exec(g_.ball_clock_read, [this, sp](const zxc_regs_t&) {
        // Read-side override: the original video interrupt still runs audio and
        // countdowns, but its ~49.86 Hz clock must not drift against our 50 Hz
        // simulation. Permit a ball update every other simulation frame. Doing
        // this immediately before LD A,(8074) avoids an ISR overwriting the flag.
        if (paced_) sp->poke(g_.ball_due, (frame_ % 2) == 0 ? 1 : 0);
    });
    // The tune: the game starts it over (the menu); with the remastered sound a new game does too
    // (the original plays on from the menu), as the player's own start does: the channels' data
    // from the top, their counters cleared. The player runs in the interrupt: never half done here.
    if (g_.tune_start) s.on_exec(g_.tune_start, [this](const zxc_regs_t&) { music_restarted_ = true; });
    // The level cleared (the remake): at the end of a play pass the game is told to stop with a reason
    // that is none of its own (2 ball lost, 3 restart, 4 quit): it then goes on to the next level
    // (0x8C0B), as the original does - which waits for the ball to fall first.
    s.on_exec(g_.pass_end, [this, sp](const zxc_regs_t&) {
        if (!advance_) return;
        advance_ = false;
        sp->poke(PLAY_REASON, 1);
    });
    if (g_.level_built) s.on_exec(g_.level_built, [this](const zxc_regs_t&) { building_ = false; });
    s.on_exec(g_.level_start, [this, sp](const zxc_regs_t&) {
        paddle_hit_ = {};
        building_ = g_.level_built != 0;
        if (!remake_ || !g_.tune_start || sp->peek16(SCORE) != 0 || sp->peek(LEVEL) != 0) return;
        // as the player's own start of the tune does (g_.tune_start): its channels' data from the top
        const uint16_t v = g_.tune_vars;
        sp->poke16(uint16_t(v + 5), 0x5E96), sp->poke16(uint16_t(v + 7), 0x5FFC), sp->poke16(uint16_t(v + 9), 0x6143);
        for (uint16_t a : {uint16_t(v + 1), uint16_t(v + 2), uint16_t(v + 3), uint16_t(0x5B21), uint16_t(0x5B25),
                           uint16_t(0x5B29), uint16_t(v + 0x19), uint16_t(v + 0x1A), uint16_t(v + 0x1B)})
            sp->poke(a, 0);
        sp->poke(v, 0xE0), sp->poke16(g_.tune_tempo, 0x0707), sp->poke(uint16_t(v + 0x18), 0xF0);
        music_restarted_ = true;
    });
    s.on_exec(g_.play_exit, [this, sp](const zxc_regs_t&) {
        paddle_hit_ = {};
        paced_ = false;
        sp->set_turbo(1);
    });
}

void BallBreakerView::after_frame(Spectrum& s) {
    auto recent = [&](uint64_t seen) { return seen && frame_ + 1 - seen < RECENT; };
    using Sc = BBMenu::Screen;
    Mode before = mode_;
    uint64_t latest = std::max({seen_menu_, seen_play_, seen_ctrl_, seen_keys_});
    // PLAY GAME taken: the original wipes the menu away (passing through the menu's code again)
    // before the level's card; that is still the way out, not the menu back
    bool leaving = left_menu_ && frame_ + 1 - left_menu_ < LEAVE_FRAMES;
    if (leaving && latest == seen_menu_) latest = std::max({seen_play_, seen_ctrl_, seen_keys_});
    if (instructions_active_) mode_ = Mode::Screen, screen_ = Sc::Instructions;
    else if (recent(seen_entry_)) mode_ = Mode::NameEntry, screen_ = Sc::HiScores;
    else if (leaving && !(latest && recent(latest))) mode_ = Mode::Play, screen_ = Sc::LevelCard;
    else if (latest && recent(latest)) {
        if (latest == seen_menu_) {
            mode_ = Mode::Menu, screen_ = Sc::Main;
        } else if (latest == seen_ctrl_) mode_ = Mode::Menu, screen_ = Sc::Controls;  // same keys as the menu
        else if (latest == seen_keys_) mode_ = Mode::Screen, screen_ = Sc::Keys;
        else mode_ = Mode::Play, screen_ = Sc::None;
    } else if (mode_ != Mode::Boot) {
        // none of the loops: a page the menu opened, the hi-scores after a game, or the level intro
        if (screen_ == Sc::Main) {
            const Sc pages[6] = {Sc::LevelCard, Sc::Controls, Sc::Keys, Sc::HiScores, Sc::Instructions, Sc::Main};
            screen_ = pages[cursor_];
            if (cursor_ == 0) left_menu_ = frame_ + 1;
        } else if (before == Mode::Play && screen_ == Sc::None)
            screen_ = s.peek(g_.lives) == 0 ? Sc::HiScores : Sc::LevelCard;
        mode_ = screen_ == Sc::LevelCard || screen_ == Sc::None ? Mode::Play : Mode::Screen;
    }
    // a new level (cleared, or a jump): its card at once, the room hidden, until play is back - the
    // game changes the number before it clears the old bricks and builds the new ones
    const int level_now = s.peek(LEVEL);
    if (mode_ == Mode::Play && seen_level_ >= 0 && level_now != seen_level_) level_changed_ = frame_ + 1;
    seen_level_ = level_now;
    if (level_changed_ && seen_play_ > level_changed_ + 1) level_changed_ = 0;  // playing the new level
    else if (level_changed_ && mode_ == Mode::Play) screen_ = Sc::LevelCard;
    if (mode_ == Mode::Boot) title_hold_ = false;  // its own loading screen is the title
    // the hi-scores kept: put back into the game the first time its menu is up, then (in the menu:
    // a name typed in is done) written out whenever the table changes
    if (!hiscores_file_.empty() && mode_ == Mode::Menu) {
        if (!hiscores_restored_) {
            for (size_t i = 0; i < hiscores_saved_.size(); i++) s.poke(uint16_t(g_.hiscores + i), hiscores_saved_[i]);
            hiscores_restored_ = true;
        }
        std::vector<uint8_t> now(HISCORES_SIZE);
        for (size_t i = 0; i < now.size(); i++) now[i] = s.peek(uint16_t(g_.hiscores + i));
        if (now != hiscores_saved_) {
            std::ofstream(hiscores_file_, std::ios::binary).write(reinterpret_cast<const char*>(now.data()), std::streamsize(now.size()));
            hiscores_saved_ = std::move(now);
        }
    }
    if (screen_ != last_screen_) last_screen_ = screen_, screen_since_ = frame_;
    // the level card: the original slides the room in from the left (its far end first) once the
    // "SCREEN" text is gone; it has begun when the playfield's left column, blank under the text
    // (what the menu left there goes first), shows something
    if (screen_ != Sc::LevelCard) slide_start_ = 0, slide_armed_ = false;
    else if (!slide_start_) {
        int rows = 0;
        for (int y = 0; y < 128; y++)
            rows += s.peek(uint16_t(0x4000 | (y & 0xC0) << 5 | (y & 7) << 8 | (y & 0x38) << 2)) != 0;
        if (rows == 0) slide_armed_ = true;
        // the original holds its card a while on black before the room comes: the remake brings
        // the room in at once, under the card, the wait spent with it in place (the bricks drop in
        // when the game builds the level: see prepare)
        if (rows == 0) {
            slide_start_ = frame_ + 1;
            if (std::getenv("BB_MODE_LOG")) std::fprintf(stderr, "frame %llu the room slides in (under the card)\n", (unsigned long long)frame_);
        }
        else if (slide_armed_ && rows > 16) {
            slide_start_ = frame_ + 1;
            if (std::getenv("BB_MODE_LOG")) std::fprintf(stderr, "frame %llu the room slides in\n", (unsigned long long)frame_);
        }
    }
    static const bool mode_log = std::getenv("BB_MODE_LOG") != nullptr;
    static int last_logged = -1;
    int sig = int(mode_) * 100 + int(screen_) * 10 + cursor_;
    if (mode_log && sig != last_logged) {
        std::fprintf(stderr, "frame %llu mode %d screen %d cursor %d (8077=%d)\n", (unsigned long long)frame_, int(mode_),
                     int(screen_), cursor_, s.peek(g_.menu_cursor));
        last_logged = sig;
    }
    // the arrows are the Kempston joystick: keep the game on it (choosing another in its
    // menu would leave the player without controls)
    if (mode_ == Mode::Menu && s.peek(g_.control_type) != KEMPSTON) s.poke(g_.control_type, KEMPSTON);
    // options: the counters kept topped up (a life is lost from 3 to 2, back to 3: never 0)
    if (mode_ == Mode::Play && s.peek(g_.lives) < lives_)
        sfx_.push_back({Sfx::LifeLost, 0, 0.9f});  // (unlimited: lost, then given back)
    lives_ = s.peek(g_.lives);
    // the tape's trainer (T on its loading screen: POKE 35840,0 NOPs the DEC (HL) that takes a life,
    // 0x8C00) is undone: lives are the original's, or the L option's
    if (g_.trainer && s.peek(g_.trainer) == 0x00) s.poke(g_.trainer, 0x35);
    if (mode_ == Mode::Play) {
        if (unlimited_lives_ && s.peek(g_.lives) < START_LIVES) s.poke(g_.lives, START_LIVES);
        if (unlimited_missiles_ && s.peek(MISSILES) < START_MISSILES) s.poke(MISSILES, START_MISSILES);
    }
    frame_++;
    game_t_ = double(frame_) / Spectrum::FRAME_HZ;
    if (modern_event_clock_) {
        // Geometric contacts can precede further ball steps in this frame.
        // Finalize with the same peripheral-time mapping as the paired samples,
        // before deciding when to release either the sound or the flash.
        if (paddle_hit_.id == modern_event_clock_->first) {
            const double hz=s.model()==128 ? 3546894.0 : 3500000.0;
            paddle_hit_.t=game_t_-double(s.video_ticks()-modern_event_clock_->second)/hz;
        }
        modern_event_clock_.reset();
    }
    if (paddle_hit_.id && sounded_paddle_hit_ != paddle_hit_.id &&
        game_t_ + 1e-8 >= paddle_hit_.t + delay_) {
        sounded_paddle_hit_ = paddle_hit_.id;
        float screen_x = 160.0f + s.peek(BALL_X) - s.peek(0x8065);
        sfx_.push_back({Sfx::BatHit, std::clamp(screen_x / 128.0f - 1, -1.0f, 1.0f) * 0.7f, 0.9f});
    }
    if (mode_ == Mode::Play) smooth_sample(s), check_cleared();
    else { tracks_.clear(); preview_.clear(); paddle_motion_.clear(); contact_motion_.clear(); }
    if (std::getenv("BB_TURBO_LOG"))
        std::fprintf(stderr, "frame %llu paced %d passes %llu ball %u,%u score %u lives %u delay %.0f ms\n",
                     (unsigned long long)frame_, paced_, (unsigned long long)passes_,
                     s.peek(0x8064), s.peek(0x8065), s.peek16(SCORE), s.peek(g_.lives), delay_ * 1000);
}

void BallBreakerView::input(Spectrum& s, const Controls& c) {
    std::set<int> keys;
    uint8_t joy = 0;
    // a key that chose a menu item is still down when the page it opens comes up: it must not
    // count there (it would close the page, or choose again). Nothing until all is let go.
    if (screen_ != input_screen_) {
        input_screen_ = screen_;
        wait_release_ = screen_ != BBMenu::Screen::None;
    }
    if (wait_release_) {
        if (c.any()) {
            apply(s, keys, joy);
            return;
        }
        wait_release_ = false;
    }
    if (title_now()) {  // our title over a game that started in its menu: a key only takes it away
        if (c.any()) title_hold_ = false, wait_release_ = true;
        apply(s, keys, joy);
        return;
    }
    switch (mode_) {
    case Mode::Boot:  // the loading screen waits for any key
        if (c.any()) keys.insert(' ');
        break;
    case Mode::Menu: {
        // OPTIONS and EXIT, the remake's items under the last: down from that one (a new press) and
        // back up; the game sees none of it (it would wrap round, or take the item it has)
        const uint8_t pressed = uint8_t(c.dirs & ~menu_dirs_);
        menu_dirs_ = c.dirs;
        if (screen_ != BBMenu::Screen::Main || shown_original_) extra_ = 0;
        else if (extra_) {
            if (pressed & ZXC_JOY_UP) extra_--, wait_release_ = !extra_;  // (back on the game's: not up again)
            else if (pressed & ZXC_JOY_DOWN) extra_ = 2;
            else if (c.ok || c.fire) (extra_ == 1 ? options_taken_ : exit_taken_) = wait_release_ = true;
            break;
        } else if (cursor_ == 5 && (c.dirs & ZXC_JOY_DOWN)) {
            if (pressed & ZXC_JOY_DOWN) extra_ = 1;
            break;
        }
        if (g_.menu_pointer && screen_ == BBMenu::Screen::Main) {
            // the arrow menu: up / down step the item (again every 8 frames while held), the arrow is
            // put on it (its hit test, 0x9EFC: x 0x40-0xBF, y + 1 within a row 0x58 + 16 n .. + 7)
            int dir = c.dirs & ZXC_JOY_UP ? -1 : c.dirs & ZXC_JOY_DOWN ? 1 : 0;
            menu_held_ = dir && dir == menu_dir_ ? menu_held_ + 1 : 0;
            menu_dir_ = dir;
            if (dir && (menu_held_ == 0 || (menu_held_ >= 20 && menu_held_ % 8 == 0))) cursor_ = std::clamp(cursor_ + dir, 0, 5);
            s.poke(g_.menu_pointer, 0x80), s.poke(uint16_t(g_.menu_pointer + 1), uint8_t(0x58 + 16 * cursor_ + 2));
            if (c.ok || c.fire) keys.insert(MENU_SELECT);
            break;
        }
        if (c.dirs & ZXC_JOY_UP) keys.insert(MENU_UP);
        if (c.dirs & ZXC_JOY_DOWN) keys.insert(MENU_DOWN);
        if (c.ok || c.fire) keys.insert(MENU_SELECT);
        break;
    }
    case Mode::NameEntry:  // the keyboard as it is; the gamepad can only finish
        keys = c.keys;
        if (c.ok && !c.keys.count(' ')) keys.insert(0x0D);
        break;
    case Mode::Screen:  // a page waiting for any key
        if (back_frames_ > 0 && instructions_active_ && instructions_sp_ && g_.instructions_entry == 0xAE11) {
            // BB1's instructions take no key: they list the bricks (waiting at the HALT 0xAE2A between
            // them), then play a demonstration. Esc while they list: back to the menu, as their end
            // would be (their caller's return, the stack as it was: the menu calls them through
            // CALL 0x9F6F, JP (HL), and goes on to redraw itself)
            uint16_t back = s.peek16(instructions_sp_);
            if (s.peek(uint16_t(back - 3)) == 0xCD) {
                s.jump_at(0xAE2A, back, uint16_t(instructions_sp_ + 2));
                back_frames_ = 0, instructions_sp_ = 0, wait_release_ = true;
                break;
            }
        }
        if (back_frames_ > 0) {  // Esc: its key, held long enough for the game to see it
            if (--back_frames_ == 0) wait_release_ = true;
            keys.insert(' ');
            break;
        }
        if (screen_ == BBMenu::Screen::Instructions) {
            int dir = bool(c.dirs & ZXC_JOY_RIGHT) - bool(c.dirs & ZXC_JOY_LEFT);
            if (dir && dir != instructions_dir_) {
                BBMenu menu;
                read_menu(s, menu);
                int pages = std::max(1, int((menu.items.size() + BBMenu::INSTRUCTIONS_PER_PAGE - 1) /
                                            BBMenu::INSTRUCTIONS_PER_PAGE));
                int page = instructions_page_ >= 0 ? instructions_page_ : int(menu.since / 6) % pages;
                instructions_page_ = (page + dir + pages) % pages;
            }
            instructions_dir_ = dir;
            if (c.dirs & (ZXC_JOY_LEFT | ZXC_JOY_RIGHT)) break;
        }
        if (c.ok || c.fire || c.dirs) keys.insert(' ');
        break;
    case Mode::Play: {
        // the bat runs along the right-hand edge, lower left to upper right: up is right
        if (c.dirs & (ZXC_JOY_LEFT | ZXC_JOY_DOWN)) joy |= ZXC_JOY_LEFT;
        if (c.dirs & (ZXC_JOY_RIGHT | ZXC_JOY_UP)) joy |= ZXC_JOY_RIGHT;
        if (c.fire || c.click) joy |= ZXC_JOY_FIRE;
        const int centre = s.peek(BAT_CENTRE);
        // (a jump, e.g. to the start of a level, isn't a step)
        if (bat_last_ >= 0 && centre != bat_last_ && std::abs(centre - bat_last_) <= 6) bat_step_ = std::abs(centre - bat_last_);
        bat_last_ = centre;
        aim_ = -1;
        if (c.mouse) {
            // held while that brings it nearer: within half a step it stays
            aim_ = std::max(0.0f, rail_x(s, c.pointer));  // (beyond the rail's ends: its end)
            float d = aim_ - float(centre);
            if (std::fabs(d) > (fast_mouse_ ? 1 : bat_step_ * 0.5f)) joy |= d > 0 ? ZXC_JOY_RIGHT : ZXC_JOY_LEFT;
        }
        break;
    }
    }
    apply(s, keys, joy);
}

GameView::Escape BallBreakerView::escape() {
    if (mode_ == Mode::Screen) return back_frames_ = 6, Escape::Done;  // a menu page: back to the menu
    if (mode_ == Mode::Menu && screen_ == BBMenu::Screen::Main && !title_now()) return Escape::Options;
    return Escape::Pause;
}

float BallBreakerView::rail_x(const Spectrum& s, Vector2 p) const {
    // the pointer as a playfield pixel (u, v): the HD view's (BBRenderer's P), or the original's
    float u = p.x / BBRenderer::SCALE, v = p.y / BBRenderer::SCALE;
    if (shown_original_) {
        Rectangle r = original_rect();
        u = (p.x - r.x) / r.width * Spectrum::FB_W - Spectrum::BORDER;
        v = (p.y - r.y) / r.height * Spectrum::FB_H - Spectrum::BORDER;
    }
    // the rail: the floor at the bat's Z, (160 + X - Z, 118 - X / 2); its point nearest (u, v)
    float a = u - 160 + float(s.peek(BAT_Z)), b = v - 118;
    return (2 * a - b) / 2.5f;
}

std::string BallBreakerView::help() const {
    return std::string("O  Speed: ") + (original_speed_ ? "Original" : "Even") +
           "\nT  Theme: " + (hd_ ? hd_->theme_name() : "Neon / Tron") +
           "\nL  Cheats: " + (unlimited_lives_ && unlimited_missiles_ ? "On" : "Off") +
           "\nU  Mouse: " + (fast_mouse_ ? "Fast (cheat)" : "Original speed") +
           "\n\nMove: Arrows / WASD / gamepad / mouse\nFire missile: Space / Ctrl / gamepad A / click";
}

std::string BallBreakerView::on_key(Spectrum&, int key) {
    if (key == KEY_T && hd_) {
        hd_->set_theme(hd_->theme() == BBTheme::Neon ? BBTheme::ClassicHD : BBTheme::Neon);
        return std::string("THEME: ") + hd_->theme_name();
    }
    if (key == KEY_L) {
        const bool enabled = !(unlimited_lives_ && unlimited_missiles_);
        unlimited_lives_ = unlimited_missiles_ = enabled;
        return enabled ? "UNLIMITED LIVES + MISSILES: ON" : "UNLIMITED LIVES + MISSILES: OFF";
    }
    if (key == KEY_U) {
        fast_mouse_ = !fast_mouse_;
        return fast_mouse_ ? "MOUSE: FAST (CHEAT)" : "MOUSE: ORIGINAL SPEED";
    }
    if (key == KEY_O) {
        // the remake's even pace (a game pass a frame, CPU headroom where the original slows down,
        // the ball on a steady 25 Hz) or the original's own, slowdowns and all
        original_speed_ = !original_speed_;
        modern_previous_body_.reset(); even_pending_paddle_centre_.reset(); modern_pending_hit_.reset(); modern_event_clock_.reset(); even_paddle_clock_.reset(); even_previous_paddle_clock_.reset(); modern_last_ball_tick_=modern_last_contact_tick_=0; modern_episode_=false;
        preview_.clear();
        paddle_motion_.clear(); contact_motion_.clear();
        return original_speed_ ? (preview_on() ? "SPEED: ORIGINAL / SMOOTH" : "SPEED: ORIGINAL") : "SPEED: EVEN";
    }
    return "";
}

BBState BallBreakerView::read_state(const Spectrum& s) const {
    BBState st;
    st.paddle_hit = paddle_hit_;
    st.level = s.peek(LEVEL) % g_.levels;
    st.room = room_of(st.level);
    st.score = s.peek16(SCORE) * 100;
    st.missiles = s.peek(MISSILES);
    st.lives = s.peek(g_.lives);
    st.unlimited_lives = unlimited_lives_, st.unlimited_missiles = unlimited_missiles_;
    uint16_t end = building_ ? OBJECTS : s.peek16(OBJECTS_END);
    int slot = 0;
    for (uint16_t a = OBJECTS; a + 10 <= end && slot < 200; a += 10, slot++) {
        BBObject o{float(s.peek(uint16_t(a + 1))), float(s.peek(uint16_t(a + 2))), s.peek(uint16_t(a + 3)),
                   float(s.peek(uint16_t(a + 7))), float(s.peek(uint16_t(a + 8))), float(s.peek(uint16_t(a + 9))), slot};
        if (slot == 1) {
            // The sprite-sort X is left - 7 (clipped near the end of the rail).
            // Rendering that coordinate as the physical paddle displaced its hit face.
            o.x = float(s.peek(BAT_CENTRE) - s.peek(BAT_WIDTH) / 2);
            if(codex_even() && g_.paddle_hit==0x8a73)
                o.x=bb_even_paddle_left(s.peek(BAT_CENTRE),s.peek(BAT_WIDTH));
            else if(g_.paddle_hit==0x8a73)
                o.x=bb_original_paddle_left(o.x,s.peek(BAT_WIDTH));
            o.z = float(s.peek(BAT_Z));
            o.bat_width = float(s.peek(BAT_WIDTH));
            o.bat_inset = g_.paddle_hit == 0x8a73 ? bb_even_paddle_inset(int(o.bat_width)) : 0;
            if(g_.paddle_hit==0x8a73) {
                o.sx=160+o.x-o.z;
                o.sy=100-o.x/2;
            }
        }
        if (slot == 0 && hybrid() && hb_.live && frame_ - hb_.batch <= 4 && o.sx != 0) {
            // the remake's ball, as the game would publish it (0x886C) but to the fraction
            o.x = std::max(0.0f, hb_.x - 5), o.z = hb_.z + 5, o.y = 0;
            o.sx = 148 + hb_.x - hb_.z, o.sy = 104 - hb_.x / 2;
        }
        if (o.sx == 0) continue;  // a free slot or a brick that is gone
        st.objects.push_back(o);
    }
    return st;
}

bool BallBreakerView::hd_now(bool original) const {
    if (original) return false;
    if (mode_ == Mode::Boot || title_now()) return FileExists(intro_path_.c_str());  // the title picture, if made
    // (frame_ is one on since after_frame's check: the same window, so no frame falls between them)
    bool playing = mode_ == Mode::Play && seen_play_ && frame_ - seen_play_ < RECENT;
    return playing || screen_ != BBMenu::Screen::None;
}

void BallBreakerView::read_menu(const Spectrum& s, BBMenu& m) const {
    m.screen = mode_ == Mode::Boot || title_now() ? BBMenu::Screen::Loading : screen_;
    if (mode_ == Mode::Play && seen_play_ && frame_ + 1 - seen_play_ < RECENT && !level_changed_) m.screen = BBMenu::Screen::None;
    m.cursor = extra_ ? 5 + extra_ : cursor_;
    m.instructions_auto = g_.instructions_entry != 0;
    m.instructions_page = instructions_page_;
    m.title = g_.title, m.publisher = g_.publisher;
    m.sound_fx = s.peek(SOUND_FX) == 0;
    if (g_.level_names) {  // the level's name: the level-th string of the table
        int level = s.peek(LEVEL) % g_.levels;
        uint16_t a = g_.level_names;
        for (int n = 0; n < level; n++)
            while (!(s.peek(a++) & 0x80)) {}
        std::string name;
        for (int k = 0; k < 40; k++) {
            uint8_t c = s.peek(a++);
            char ch = char(c & 0x7F);
            name += ch == '*' ? ' ' : ch;
            if (c & 0x80) break;
        }
        m.level_name = name;
        if (!g_.designers.empty()) m.designer = g_.designers[size_t(std::min(level / 5, int(g_.designers.size()) - 1))];
    }
    m.since = double(frame_ - screen_since_) / Spectrum::FRAME_HZ;
    // the slide takes the original about 68 frames (4 pixels a frame), at an even speed
    m.slide = m.screen != BBMenu::Screen::LevelCard ? 1.0f
              : slide_start_ ? std::min(1.0f, float(frame_ + 1 - slide_start_) / 68.0f) : 0.0f;
    m.leave = left_menu_ && m.screen == BBMenu::Screen::LevelCard ? std::min(1.0f, float(frame_ + 1 - left_menu_) / LEAVE_FRAMES) : 1.0f;
    for (int i = 0; i < 9; i++) {
        uint16_t a = uint16_t(g_.hiscores + i * 14);
        std::string name;
        for (int k = 0; k < 12; k++) {
            uint8_t c = s.peek(uint16_t(a + k));
            name += c >= 32 && c < 127 ? char(c) : ' ';
        }
        while (!name.empty() && name.back() == ' ') name.pop_back();
        m.scores.push_back({name, s.peek16(uint16_t(a + 12)) * 100});
    }
    if (mode_ == Mode::NameEntry) {  // the row being typed into: the one with this game's score
        int score = s.peek16(SCORE) * 100;
        for (int i = 0; i < 9; i++)
            if (m.scores[size_t(i)].score == score) {
                m.entry_row = i;
                break;
            }
    }
    uint16_t a = g_.instructions;
    for (int n = 0; n < 64; n++) {
        uint8_t sprite = s.peek(a++);
        if (sprite == 0xFF) break;
        std::string text;
        for (int k = 0; k < 80; k++) {
            uint8_t c = s.peek(a++);
            char ch = char(c & 0x7F);
            if (ch != ' ' || !text.empty()) text += ch;
            if (c & 0x80) break;
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        m.items.push_back({sprite, text, s.peek(uint16_t(g_.points + sprite * 3)) * 100});
    }
}

void BallBreakerView::prepare(const Spectrum& s, bool original, double t) {
    shown_original_ = original;
    if (!hd_now(original)) return;
    if (!hd_) {
        hd_ = std::make_unique<BBRenderer>(fonts_dir_, intro_path_, creatures_path_);
        hd_->set_theme(g_.theme);
    }
    if (!hd_->has_sprites()) {
        std::vector<uint8_t> data(60 * 120);
        for (size_t i = 0; i < data.size(); i++) data[i] = s.peek(uint16_t(g_.sprites + i));
        hd_->load_sprites(data.data());
    }
    for (const auto& [o, points] : bursts_) hd_->burst(o, points);
    bursts_.clear();
    static const bool stress = std::getenv("BB_STRESS") != nullptr;  // every brick bursts at once (perf check)
    static bool stressed = false;
    if (stress && !stressed && !latest_.objects.empty()) {
        for (const BBObject& o : latest_.objects)
            if (o.slot >= 3) hd_->burst(o, 100);
        stressed = true;
    }
    perf_t0_ = GetTime();
    state_ = latest_.objects.empty() || mode_ != Mode::Play ? read_state(s) : latest_;
    state_.render_delay = delay_;
    state_.aim = mode_ == Mode::Play && !fast_mouse_ ? aim_ : -1;  // fast: the bat is the mark
    read_menu(s, state_.menu);
    // the room's slide by the render clock (smooth at any display rate), from the frame it began
    if (state_.menu.screen == BBMenu::Screen::LevelCard && slide_start_)
        state_.menu.slide = float(std::clamp((t - double(slide_start_ - 1) / Spectrum::FRAME_HZ) / (68.0 / Spectrum::FRAME_HZ), 0.0, 1.0));
    if (state_.menu.screen != BBMenu::Screen::None && state_.menu.screen != BBMenu::Screen::LevelCard)
        state_.objects.clear();  // the menus: no play objects behind them
    else
        smooth_apply(state_, t), drop_in(state_, t);
    hd_->prepare(state_, t);
    if (const char* d = std::getenv("BB_GLOW_DUMP")) hd_->debug_dump(d);
}

void BallBreakerView::draw(const Spectrum& s, bool original, double t) {
    if (hd_now(original) && hd_) {
        hd_->draw(state_, t);
        static const bool perf = std::getenv("BB_PERF") != nullptr;  // CPU time of a HD frame
        if (perf) {
            rlDrawRenderBatchActive();
            std::fprintf(stderr, "hd frame %.2f ms\n", (GetTime() - perf_t0_) * 1000);
        }
    } else
        draw_original_fit(s);
}

namespace {
constexpr float TELEPORT = 24;  // playfield pixels: bigger jumps are taken at once
}  // namespace

void BallBreakerView::track_pos(const Track& tr, double t, float* out) {
    const auto& samples = tr.samples;
    const Sample* left = &samples.front();
    const Sample* right = left;
    for (const auto& sample : samples) {
        right = &sample;
        if (sample.t >= t) break;
        left = right;
    }
    double span = right->t - left->t;
    float a = span > 0 ? float(std::clamp((t - left->t) / span, 0.0, 1.0)) : 0;
    for (int i = 0; i < 5; i++) out[i] = left->pos[i] + (right->pos[i] - left->pos[i]) * a;
}

void BallBreakerView::smooth_sample(const Spectrum& s) {
    BBState before = std::move(latest_);
    size_t bursts_before = bursts_.size();
    latest_ = read_state(s);
    // bricks hit since the last frame: the game turns them into its explosion (sprites 0x19-0x1F)
    // or drops them; effects for those (not when a whole level goes or comes)
    auto exploding = [](int sprite) { return sprite >= 0x19 && sprite <= 0x1f; };
    if (!before.objects.empty() && before.level == latest_.level) {
        std::vector<BBObject> hit;
        for (const BBObject& o : before.objects) {
            if (o.slot < 3 || exploding(o.sprite)) continue;
            const BBObject* now = nullptr;
            for (const BBObject& n : latest_.objects)
                if (n.slot == o.slot) now = &n;
            if (!now || exploding(now->sprite)) hit.push_back(o);
        }
        // the score goes up a frame or two before the explosion starts
        if (latest_.score > before.score) gain_ += latest_.score - before.score, gain_t_ = game_t_;
        if (game_t_ - gain_t_ > 0.3) gain_ = 0;
        int points = hit.empty() ? 0 : gain_ / int(hit.size());
        if (!hit.empty()) gain_ = 0;
        if (hit.size() <= 6)
            for (const BBObject& o : hit) bursts_.push_back({o, points});
    }
    sounds(before, bursts_before);
    if (before.level != latest_.level) {
        modern_previous_body_.reset(); even_pending_paddle_centre_.reset(); modern_pending_hit_.reset(); modern_event_clock_.reset(); even_paddle_clock_.reset(); even_previous_paddle_clock_.reset(); modern_last_ball_tick_=modern_last_contact_tick_=0; modern_episode_=false;
        tracks_.clear(); lifts_.clear(); glides_.clear(); preview_.clear(); paddle_motion_.clear(); contact_motion_.clear();
    }
    double t = game_t_;
    if (preview_on() && original_speed_) {
        bool found = false;
        for (const auto& o : latest_.objects) if (o.slot == 1) {
            paddle_motion_.sample(o.x, o.bat_width, t, false);
            found = true;
        }
        if (!found) { paddle_motion_.clear(); contact_motion_.clear(); }
    } else { paddle_motion_.clear(); if (!preview_on()) contact_motion_.clear(); }
    std::map<int, Lift> lifts;
    for (const BBObject& o : latest_.objects) {
        float h = slab_lift(o.sprite);
        if (h < 0) continue;
        auto it = lifts_.find(o.slot);
        Lift l = it != lifts_.end() ? it->second : Lift{h, h, t, 0.1};
        if (h != l.to) l = {l.at(t), h, t, std::clamp(t - l.t, 0.02, 0.15)};
        lifts[o.slot] = l;
    }
    lifts_ = std::move(lifts);
    std::map<int, Track> next;
    for (const BBObject& o : latest_.objects) {
        float v[5] = {o.sx, o.sy, o.x, o.y, o.z};
        Track tr;
        if (auto it = tracks_.find(o.slot); it != tracks_.end()) tr = it->second;
        double sample_t=t;
        if(codex_even() && g_.paddle_hit==0x8a73 && o.slot==1 && even_paddle_clock_ &&
           std::abs(o.x-even_paddle_clock_->first)<.001f &&
           (tr.samples.empty() || std::abs(o.x-tr.samples.back().pos[2])>.001f)) {
            const double hz=s.model()==128?3546894.0:3500000.0;
            double actual=t-double(s.video_ticks()-even_paddle_clock_->second)/hz;
            if(tr.samples.empty() || actual>tr.samples.back().t+1e-8)sample_t=actual;
        }
        bool moved = tr.samples.empty();
        if (!tr.samples.empty()) {
            const auto& last = tr.samples.back();
            bool teleport = std::abs(v[0] - last.pos[0]) > TELEPORT ||
                            std::abs(v[1] - last.pos[1]) > TELEPORT;
            for (int i = 0; i < 5; i++) moved |= v[i] != last.pos[i];
            if (teleport) tr.samples.clear();
            // After a rest, keep a stationary endpoint immediately before movement.
            else if (moved && sample_t - last.t > delay_ + 0.001) {
                Sample rest = last;
                rest.t = sample_t - 1.0 / Spectrum::FRAME_HZ;
                tr.samples.push_back(rest);
            }
        }
        if (moved) {
            Sample sample{sample_t, {v[0], v[1], v[2], v[3], v[4]}};
            tr.samples.push_back(sample);
            if (o.slot == 0) {  // the ball's steps: how long the game takes between its moves
                if (ball_moved_t_ >= 0 && t - ball_moved_t_ < 0.3) {
                    ball_steps_.push_back(t - ball_moved_t_);
                    if (ball_steps_.size() > 8) ball_steps_.pop_front();
                }
                ball_moved_t_ = t;
            }
        }
        while (tr.samples.size() > 2 && tr.samples[1].t <= t - delay_ - 0.02)
            tr.samples.pop_front();
        next[o.slot] = std::move(tr);
    }
    tracks_ = std::move(next);
    std::map<int, Glide> glides;
    for (const BBObject& o : latest_.objects) {
        const float v[5] = {o.sx, o.sy, o.x, o.y, o.z};
        Glide g;
        auto it = glides_.find(o.slot);
        if (it == glides_.end()) {
            std::copy(v, v + 5, g.from), std::copy(v, v + 5, g.to), g.t0 = t, g.changed = t;
        } else {
            g = it->second;
            if (!std::equal(v, v + 5, g.to)) {
                bool jump = std::abs(v[0] - g.to[0]) > TELEPORT || std::abs(v[1] - g.to[1]) > TELEPORT;
                // from where it is shown (drawn a frame behind: see smooth_apply), on from there
                const double shown_t = t - 1.0 / Spectrum::FRAME_HZ;
                float shown[5];
                g.at(shown_t, shown);
                std::copy(jump ? v : shown, (jump ? v : shown) + 5, g.from);
                std::copy(v, v + 5, g.to);
                g.dur = std::clamp(t - g.changed, 0.02, 0.15);
                g.t0 = shown_t, g.changed = t;
            }
        }
        g.coast = 0;
        if (original_speed_ && o.slot == 0) {
            // Conservative swept envelope of the rendered sphere. Predict only
            // through empty space; do not invent a bounce or enter a solid object.
            auto centre = [](const float* v, float* c) {
                c[0] = v[2] + 5;
                c[1] = 118 - c[0]/2 - (v[1] + 10);
                c[2] = 160 + c[0] - (v[0] + 12);
            };
            float start[3], end[3], predicted[5];
            for (int i=0; i<5; ++i) predicted[i] = g.to[i] + (g.to[i]-g.from[i])*0.5f;
            centre(g.to, start); centre(predicted, end);
            float lo[3], hi[3];
            for (int i=0; i<3; ++i) {
                lo[i] = std::min(start[i], end[i]) - BB_BALL_RADIUS;
                hi[i] = std::max(start[i], end[i]) + BB_BALL_RADIUS;
            }
            bool clear = lo[0] > 0 && hi[0] < 92 && lo[1] > 0 && hi[1] < 64 && lo[2] > 0 && hi[2] < 148;
            for (const auto& obstacle : latest_.objects) {
                if (obstacle.slot == 0 || obstacle.slot == 2 ||
                    (obstacle.sprite >= 0x19 && obstacle.sprite <= 0x1f)) continue;
                float x0=obstacle.x, x1=x0+12, y0=obstacle.y, y1=y0+24;
                float z0=obstacle.z-12, z1=obstacle.z;
                if (obstacle.slot == 1) {
                    x1=x0+obstacle.bat_width; y0=0; y1=12; z0=z1-7;
                }
                if (lo[0] <= x1 && hi[0] >= x0 && lo[1] <= y1 && hi[1] >= y0 && lo[2] <= z1 && hi[2] >= z0)
                    clear = false;
            }
            if (clear) g.coast = 0.5f;
        }
        glides[o.slot] = g;
    }
    glides_ = std::move(glides);
    double want = BB_RENDER_DELAY;
    if (original_speed_)
        for (double step : ball_steps_) want = std::max(want, std::min(step, 0.16));
    delay_ += (want - delay_) * 0.1;  // eased: a change of delay must not jump the picture
    if (preview_on()) {
        delay_ = BB_RENDER_DELAY;  // both objects and the contact event share this buffer
        bool ball_found = false;
        for (const auto& ball : latest_.objects) if (ball.slot == 0) {
            if (original_speed_) preview_.sample(s, ball, t, g_.paddle_hit, g_.serve_countdown, LEVEL);
            for (const auto& bat : latest_.objects) if (bat.slot == 1) {
                BBObject seed_ball=ball,seed_bat=bat;
                if(!original_speed_) {
                    for(BBObject* object:{&seed_ball,&seed_bat})
                        if(auto it=tracks_.find(object->slot);it!=tracks_.end()) {
                            float v[5];track_pos(it->second,t-BB_RENDER_DELAY,v);
                            object->sx=v[0];object->sy=v[1];object->x=v[2];object->y=v[3];object->z=v[4];
                        }
                }
                auto baseline_bat=[&](double time) {
                    BBObject p=bat;
                    if(auto it=tracks_.find(1);it!=tracks_.end()) {
                        float v[5];track_pos(it->second,time,v);
                        p.sx=v[0];p.sy=v[1];p.x=v[2];p.y=v[3];p.z=v[4];
                    }
                    return p;
                };
                std::vector<double> paddle_times;
                if(!original_speed_)if(auto it=tracks_.find(1);it!=tracks_.end())
                    for(const auto& sample:it->second.samples)paddle_times.push_back(sample.t);
                contact_motion_.sample(s,ball,bat,t,!original_speed_,&seed_ball,&seed_bat,baseline_bat,paddle_times);
            }
            ball_found = true;
        }
        if (!ball_found) { preview_.clear(); contact_motion_.clear(); }
    }
}

void BallBreakerView::smooth_apply(BBState& st, double t) const {
    auto apply_contact = [&] {
        if (preview_on()) {
            BBObject ball{}, bat{};
            if (contact_motion_.at(t - BB_RENDER_DELAY, ball, bat)) {
                // Blend into the shared confirmed timeline before the ball can
                // touch the paddle. At the face, no independent filter remains.
                float k = std::clamp((40.0f - (ball.z - 5)) / 16.0f, 0.0f, 1.0f);
                k = k*k*(3-2*k);
                for (auto& o : st.objects) if (o.slot == 0 || o.slot == 1) {
                    const auto& target = o.slot == 0 ? ball : bat;
                    o.x += (target.x-o.x)*k; o.y += (target.y-o.y)*k; o.z += (target.z-o.z)*k;
                    o.sx += (target.sx-o.sx)*k; o.sy += (target.sy-o.sy)*k;
                }
            }
        }
    };
    if (original_speed_) {  // the glides (see Glide); lifts as always
        for (BBObject& o : st.objects) {
            const BBObject raw = o;
            if (auto it = glides_.find(o.slot); it != glides_.end()) {
                float v[5];
                it->second.at(t - 1.0 / Spectrum::FRAME_HZ, v);  // (t runs up to a frame ahead of the last sample)
                o.sx = v[0], o.sy = v[1], o.x = v[2], o.y = v[3], o.z = v[4];
            }
            // A lost ball must not fall back to an older chasing position.
            if (o.slot == 0 && preview_on() && !preview_.at(t - BB_RENDER_DELAY, o)) o = raw;
            if (o.slot == 1 && preview_on() && paddle_motion_.valid()) {
                o.x = paddle_motion_.at(t);
                o.sx = 160 + o.x - o.z;
                o.sy = 100 - o.x/2;
            }
            if (auto it = lifts_.find(o.slot); it != lifts_.end()) o.lift = it->second.at(t);
        }
        apply_contact();
        return;
    }
    for (BBObject& o : st.objects) {
        auto it = tracks_.find(o.slot);
        if (it == tracks_.end()) continue;
        float v[5];
        track_pos(it->second, t - delay_, v);
        o.sx = v[0], o.sy = v[1], o.x = v[2], o.y = v[3], o.z = v[4];
    }
    for (BBObject& o : st.objects)
        if (auto it = lifts_.find(o.slot); it != lifts_.end()) o.lift = it->second.at(t);
    apply_contact();
}

void BallBreakerView::sounds(const BBState& before, size_t bursts_before) {
    auto pan = [](const BBObject& o) { return std::clamp((o.sx + 12) / 128.0f - 1, -1.0f, 1.0f) * 0.7f; };
    auto find = [](const BBState& st, int slot) -> const BBObject* {
        for (const BBObject& o : st.objects)
            if (o.slot == slot) return &o;
        return nullptr;
    };
    if (!before.objects.empty() && before.level != latest_.level) sfx_.push_back({Sfx::LevelStart, 0, 0.8f});
    // bricks: a crunch with a note, higher for more points; a bomb booms
    if (bursts_.size() > bursts_before) heard_.brick_t = game_t_;
    for (size_t i = bursts_before; i < bursts_.size(); i++) {
        const auto& [o, points] = bursts_[i];
        static const int STEPS[6] = {0, 3, 5, 7, 10, 12};  // A minor pentatonic
        int pitch = STEPS[std::clamp(points / 100 - 1, 0, 5)];
        sfx_.push_back({o.sprite == 0x06 ? Sfx::Bomb : Sfx::Brick, pan(o), 1, pitch});
    }
    if (const BBObject* m = find(latest_, 2); m && !find(before, 2)) sfx_.push_back({Sfx::Missile, pan(*m), 0.8f});
    // the ball: a turn in its path is a bounce (off the bat: near it), coming down on the floor a landing
    const BBObject* b = find(latest_, 0);
    if (!b) {
        heard_.have = false;
        return;
    }
    float h = (100 - b->x / 2.0f) - b->sy;
    if (std::getenv("BB_BALL_LOG") && (b->x != heard_.x || b->z != heard_.z)) {
        const BBObject* bat = find(latest_, 1);
        std::fprintf(stderr, "t %.2f ball x %.0f y %.0f z %.0f h %.1f bat x %.0f z %.0f\n", game_t_, b->x, b->y, b->z, h,
                     bat ? bat->x : -1, bat ? bat->z : -1);
    }
    if (heard_.have && (b->x != heard_.x || b->z != heard_.z)) {
        float dx = b->x - heard_.x, dz = b->z - heard_.z;
        if (std::abs(dx) > 12 || std::abs(dz) > 12) heard_.dx = heard_.dz = 0;  // a jump: a new ball
        else {
            int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sz = dz > 0 ? 1 : dz < 0 ? -1 : 0;
            bool turned = (sx && heard_.dx && sx != heard_.dx) || (sz && heard_.dz && sz != heard_.dz);
            if (turned && game_t_ - heard_.hit_t > 0.1 && game_t_ - paddle_hit_.t > 0.12) {
                heard_.hit_t = game_t_;
                if (game_t_ - heard_.brick_t > 0.12) sfx_.push_back({Sfx::Bounce, pan(*b), 0.8f});
            }
            if (sx) heard_.dx = sx;
            if (sz) heard_.dz = sz;
        }
    }
    if (heard_.have && heard_.h > 3 && h <= 1.2f && game_t_ - heard_.land_t > 0.2) {
        heard_.land_t = game_t_;
        sfx_.push_back({Sfx::Land, pan(*b), 0.6f});
    }
    heard_.have = true, heard_.x = b->x, heard_.z = b->z, heard_.h = h;
}

std::unique_ptr<MusicRemix> BallBreakerView::make_remix(int sample_rate) const {
    if (!g_.tune_start) return nullptr;  // the game's own music player is not mapped: its own sound
    return std::make_unique<MusicRemix>(sample_rate);
}

void BallBreakerView::take_sfx(std::vector<SfxEvent>& out) {
    if (mode_ == Mode::Play) out.insert(out.end(), sfx_.begin(), sfx_.end());
    sfx_.clear();
}

void BallBreakerView::check_cleared() {
    // the remake: no breakable brick left, the last one's explosion over (and its shards fallen):
    // on to the next level, without waiting for the ball to fall
    if (latest_.level != cleared_level_) cleared_level_ = latest_.level, had_bricks_ = false, cleared_t_ = -1;
    int left = 0;
    bool exploding = false;
    for (const BBObject& o : latest_.objects) {
        if (o.slot < 3) continue;
        if (o.sprite >= 0x19 && o.sprite <= 0x1f) exploding = true;
        else if (!(o.sprite >= 0x11 && o.sprite <= 0x18) && o.sprite != 0x10) left++;  // not aliens, missiles, the unbreakable
    }
    if (left > 0 || exploding || !remake_) {
        had_bricks_ |= left > 0;
        cleared_t_ = -1;
        return;
    }
    if (!had_bricks_) return;  // (a level being built)
    if (cleared_t_ < 0) cleared_t_ = game_t_;
    else if (game_t_ - cleared_t_ >= CLEARED_WAIT) advance_ = true, had_bricks_ = false;
}

int BallBreakerView::level(const Spectrum& s) const { return s.peek(LEVEL) % g_.levels; }

void BallBreakerView::jump_to(Spectrum& s, int level) {
    // At the start of the next play pass (nothing half done) the level number is set and the CPU
    // goes into the game's level start, as after a cleared level: the room, its bricks, the
    // "SCREEN" intro; score, lives and missiles stay.
    pending_level_ = std::clamp(level, 0, g_.levels - 1);
    s.jump_at(g_.main_loop, g_.level_start, 0x0000);
}

void BallBreakerView::drop_in(BBState& st, double t) {
    // a whole level appearing at once (many new bricks in a frame): they drop onto their places from
    // above, left to right, instead of popping up; bricks that appear in play (falling) as they are
    std::set<int> now;
    int fresh = 0;
    for (const BBObject& o : st.objects)
        if (o.slot >= 3) now.insert(o.slot), fresh += !seen_slots_.count(o.slot);
    if (fresh >= 5)
        for (const BBObject& o : st.objects)
            if (o.slot >= 3 && !seen_slots_.count(o.slot)) drops_[o.slot] = t + (o.x + (148 - o.z)) * 0.0025;
    seen_slots_ = std::move(now);
    for (BBObject& o : st.objects) {
        auto it = drops_.find(o.slot);
        if (it == drops_.end()) continue;
        double k = (t - it->second) / 0.35;  // 0.35 s a brick, eased in: it lands
        if (k >= 1) {
            drops_.erase(it);
            continue;
        }
        float fall = float(1 - std::max(0.0, k));
        o.y += 60 * fall * fall;
    }
}
