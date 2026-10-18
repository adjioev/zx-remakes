#include "bb_engine.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float SPEED = 2.15f;      // the ball: units a frame (the original's ~4.3 a batch of moves, a batch every 2)
constexpr float MAX_ANGLE = 1.05f;  // off the bat's ends (rad, ~60 degrees); never flatter after any bounce
constexpr float X_MIN = 5, X_MAX = 0x5B, Z_MAX = 0x94, Z_OUT = 2;  // the ball's centre: walls, past the bat
constexpr float R = BB_BALL_RADIUS;
constexpr float REACH = 0;          // the bat catches past its drawn ends (0: where it is drawn, no gap)
constexpr float BAT_VMAX = 3.2f, BAT_ACC = 0.8f, BAT_MOUSE = 8;  // the bat: keys / pad, the mouse (a frame)
constexpr int WIDE = 12, NARROW = 6;  // the bat's widths (0x8063; the bat-size brick toggles them)
constexpr float SERVE_X = 0x2A, SERVE_Z = 0x14;
constexpr float DROP_H = 40, DROP_G = 0.55f, DROP_KEEP = 0.75f;  // the serve: dropped in, six bounces
constexpr int DROP_BOUNCES = 6, FALL_FRAMES = 30;  // ... and the lost ball's fall past the bat
constexpr float SLOW = 0.55f;       // the slow ball's speed (the original: 2/2/1 moves a batch for 4/3/2)
constexpr int SLOW_FRAMES = 2000;   // 0x808E
constexpr float MISSILE_V = 3;      // a missile: Z a frame (0x9BE6), from Z 12
constexpr int MISSILE_WALL = 156;   // it bursts at the far wall
constexpr int COOLDOWN_BURST = 12, COOLDOWN_CREATURE = 2;
constexpr int MAX_CREATURES = 8;
// (the remake's: one that walks off the front comes back alone, after a while, with at most a few
// about; the original sends one, or two by chance, at once, and they crowd the floor)
constexpr int RESPAWN_WAIT = 200;   // 4 s
constexpr int MAX_ABOUT = 3;
constexpr int HOLD_UNDER = 14;      // a stack a creature walks out from under falls this much later
constexpr int EXIT_WAIT = 120;      // the exit brick: from the clear floor to the next level
constexpr int CLEARED_WAIT = 50;    // all done: the last explosions and their shards, then the next level
// a fall: px a frame (0xB41B; 8 entries = one layer, 11 = two); a slide: Z a frame (0xB426, two rows)
constexpr int FALL[11] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5};
constexpr int SLIDE[7] = {6, 5, 4, 3, 3, 2, 1};
// the bobbing double slabs (their table sprite 0x81-0x86 = their starting phase), the breathing brick
constexpr int SLAB[6] = {0x2F, 0x2E, 0x2D, 0x2C, 0x0E, 0x30};
constexpr int BREATH[8] = {0x36, 0x38, 0x39, 0x37, 0x37, 0x37, 0x39, 0x38};

bool creature_kind(int k) { return k == 0x11 || k == 0x15; }
bool long_kind(int k) { return k == 0x25 || k == 0x37 || k == 0x38 || k == 0x39; }
void partner(int kind, int& col, int& row) {  // a long brick's other half
    if (kind == 0x25) col++;
    else if (kind == 0x37) col--;
    else if (kind == 0x38) row++;
    else if (kind == 0x39) row--;
}

}  // namespace

BBPaddleBody BBEngine::bat_body() const { return rules_.bat_body(bat_x_, bat_width_); }

void BBEngine::start_level(const std::vector<BBEngineBrick>& bricks, int level, bool fast) {
    bricks_.clear(), creatures_.clear(), holds_.clear(), returning_.clear();
    for (const BBEngineBrick& b : bricks) {
        if (creature_kind(b.kind) && b.layer == 0) {  // a creature on the floor: it walks
            Creature c;
            c.x = float(b.col * 12), c.z = float((b.row + 1) * 12);
            c.type = b.kind == 0x15 ? 2 : 1;
            c.home_col = b.col, c.home_row = b.row;
            c.id = next_id_++, c.obj = b.obj;
            creatures_.push_back(c);
            continue;
        }
        Brick br;
        br.col = b.col, br.row = b.row, br.layer = b.layer, br.kind = b.kind & 63, br.sprite = b.sprite, br.obj = b.obj;
        bricks_.push_back(br);
    }
    level_ = level, fast_ = fast, frame_ = 0;
    bat_width_ = WIDE, bat_x_ = 42, bat_v_ = 0;
    missile_ = {}, cooldown_ = 0, eating_ = 0, slow_ = 0, drain_ = 0, exit_ = 0, exit_wait_ = 0, cleared_wait_ = 0;
    active_ = true;
    result_ = Result::None;
    rng_ = 12345u + unsigned(level) * 7919u;
    serve();
}

void BBEngine::serve() {
    // dropped in over its place, bouncing, then off towards the bat: along the diagonal if the bat is
    // still in the middle, else straight (0x9C9F)
    const bool middle = std::fabs(bat_x_ - SERVE_X) < 1;
    ball_ = {};
    ball_.x = SERVE_X, ball_.z = SERVE_Z, ball_.h = DROP_H, ball_.drop = DROP_BOUNCES;
    ball_.vx = middle ? -std::sqrt(0.5f) : 0, ball_.vz = middle ? -std::sqrt(0.5f) : -1;
}

int BBEngine::solid(int layer, int col, int row) const {
    for (size_t i = 0; i < bricks_.size(); i++) {
        const Brick& b = bricks_[i];
        if (int(i) != ignore_ && b.state == State::Rest && b.layer == layer && b.col == col && b.row == row) return int(i);
    }
    return -1;
}

bool BBEngine::busy(int col, int row) const {
    for (size_t i = 0; i < bricks_.size(); i++) {
        const Brick& b = bricks_[i];
        if (int(i) != ignore_ && b.col == col && b.row == row && (b.state == State::Falling || b.state == State::Sliding)) return true;
    }
    return false;
}

int BBEngine::stack_height(int col, int row) const {
    int h = 0;
    for (const Brick& b : bricks_)
        if (b.state != State::Gone && b.state != State::Exploding && b.col == col && b.row == row) h = std::max(h, b.layer + 1);
    return h;
}

bool BBEngine::level_left() const {
    // a level is done when every brick the ball can count is gone: the armour (0x09, 0x20) and the
    // creatures are counted done from the start in the original
    for (const Brick& b : bricks_)
        if (b.state != State::Gone && b.state != State::Exploding && rules_.handler[size_t(b.kind)] != 1 && !creature_kind(b.kind))
            return true;
    return false;
}

bool BBEngine::fall_from(int col, int row, int layer, int layers, int steps) {
    // the bricks resting in this cell from this layer up (to the first gap) come down `layers`. (The
    // original lets a long brick hang on while its other end stands, holding up what is on it; drawn
    // as two cubes in HD that looks like bricks in the air, so here each half comes down on its own)
    if (busy(col, row)) return false;
    int n = 0;
    for (int l = layer;; l++) {
        const int i = solid(l, col, row);
        if (i < 0) break;
        Brick& b = bricks_[size_t(i)];
        b.state = State::Falling, b.t = 0, b.dy = 0, b.fall_start = 3 * n, b.fall_layers = layers;
        n++;
    }
    (void)steps;
    return true;
}

void BBEngine::explode(size_t i, bool score_it) {
    Brick& b = bricks_[i];
    if (b.state == State::Gone || b.state == State::Exploding) return;
    b.state = State::Exploding, b.t = 0;
    if (score_it) score += 100 * rules_.points[size_t(b.kind)];
    sound(1);
}

void BBEngine::deflect() {
    // the original's random turn (0xA680): one in four, the ball's sideways step one up or down
    sound(4);
    if (random() % 4) return;
    const float a = (random() & 1 ? 1.0f : -1.0f) * 0.3f;
    const float vx = ball_.vx * std::cos(a) - ball_.vz * std::sin(a), vz = ball_.vx * std::sin(a) + ball_.vz * std::cos(a);
    ball_.vx = vx, ball_.vz = vz;
}

bool BBEngine::hit(size_t i, bool missile, float vz) {
    Brick& b = bricks_[i];
    const int kind = b.kind, col = b.col, row = b.row;
    auto generic = [&](bool score_it) {
        // one hit: what is stacked above comes down a layer (not while that column is still falling:
        // then it only bounces); a long brick takes its other half with it (its points once). It no
        // longer holds anything up meanwhile (a long brick above it may rest on it alone)
        if (solid(1, col, row) >= 0 && busy(col, row)) return false;
        explode(i, score_it);
        if (solid(1, col, row) >= 0) fall_from(col, row, 1, 1, 8);
        if (long_kind(kind)) {
            int pc = col, pr = row;
            partner(kind, pc, pr);
            if (const int j = solid(0, pc, pr); j >= 0) {
                explode(size_t(j), false);
                if (solid(1, pc, pr) >= 0) fall_from(pc, pr, 1, 1, 8);
            }
        }
        return true;
    };
    if (long_kind(kind)) {
        int pc = col, pr = row;
        partner(kind, pc, pr);
        if (busy(pc, pr)) return false;
    }
    switch (rules_.handler[size_t(kind)]) {
    case 1:  // armour: 0x20 goes to a missile; the ball only bounces off (turned at random)
        if (missile && kind == 0x20) return generic(true);
        if (!missile) deflect();
        return false;
    case 2:  // the sliding brick: the first hit sends it two rows towards the bat, the second breaks it
    case 9:  // the bonus box: the same, +3000 at once and then 100 less a second (30 times); broken: +1000
        if (!b.slid) {
            if (busy(col, row)) return true;
            ignore_ = int(i);  // (it holds nothing up any more; its cell is not busy for this)
            if (solid(1, col, row) >= 0) fall_from(col, row, 1, 1, 8);
            ignore_ = -1;
            b.slid = true, b.state = State::Sliding, b.t = 0, b.dz = 0;
            if (kind == 0x08) score += 3000, drain_ = 30, drain_t_ = 0;
            sound(4);
            return true;
        }
        if (kind == 0x08) drain_ = 0;
        return generic(true);
    case 3: {  // the bomb: layers 0 and 1 of its column and those beside it (same row), bombs there
               // carrying it on; what stood higher comes down two layers
        for (int c = col - 1; c <= col + 1; c++)
            if (c >= 0 && c < 8 && busy(c, row)) return false;
        std::vector<int> cols{col}, done;
        while (!cols.empty()) {
            const int at = cols.back();
            cols.pop_back();
            if (std::find(done.begin(), done.end(), at) != done.end()) continue;
            done.push_back(at);
            for (int c = at - 1; c <= at + 1; c++) {
                if (c < 0 || c > 7) continue;
                for (int l = 0; l <= 1; l++) {
                    const int j = solid(l, c, row);
                    if (j < 0) continue;
                    if (bricks_[size_t(j)].kind == 0x06 && c != at) cols.push_back(c);
                    explode(size_t(j), true);
                }
                if (solid(2, c, row) >= 0) fall_from(c, row, 2, 2, 11);
            }
        }
        return true;
    }
    case 4: score += int(random() % 16) * 100; return generic(true);  // random bonus points
    case 5:  // the exit: the ball takes the whole floor down, then the next level (a missile only breaks it)
        if (!missile && eating_ == 0) exit_ = 1, ball_.flying = false;
        return generic(true);
    case 6: missiles = std::min(99, missiles + 1); return generic(true);
    case 7: missiles = std::min(99, missiles + 5); return generic(true);
    case 8: if (lives < 3) lives++; return generic(true);
    case 10:  // the bat's size: normal <-> small, for the rest of the level
        if (eating_ == 0) {
            bat_width_ = bat_width_ == WIDE ? NARROW : WIDE;
            if (bat_width_ == WIDE) bat_x_ = std::max(bat_x_, 7.0f);
        }
        return generic(true);
    case 11: slow_ = SLOW_FRAMES; return generic(true);
    case 12:  // struck from the rear only (the ball coming back towards the bat); a missile always
        if (missile || vz < 0) return generic(true);
        deflect();
        return false;
    default: return generic(true);
    }
}

void BBEngine::step(const BBEngineInput& in) {
    if (!active_ || result_ != Result::None) return;
    frame_++;
    contact = false;
    if (cooldown_ > 0) cooldown_--;
    if (slow_ > 0) slow_--;
    if (drain_ > 0) {
        if (drain_t_ == 0) score = std::max(0, score - 100), drain_--, drain_t_ = 60;
        else drain_t_--;
    }
    if (eating_ > 0) {
        // a creature eats the bat: it cannot move or fire; when it is gone the game is over (as in the
        // original, whatever the lives) - with unlimited lives it costs a ball instead
        if (--eating_ == 0) {
            for (const Creature& c : creatures_)
                if (c.on_bat) release(c);
            std::erase_if(creatures_, [](const Creature& c) { return c.on_bat; });
            if (!unlimited_lives) {
                result_ = Result::Over;
                return;
            }
            lose_ball();
        }
    } else step_bat(in);
    step_missile(in);
    if (exit_) step_exit();
    else step_ball();
    if (result_ != Result::None) return;
    step_creatures();
    step_bricks();
    for (Hold& h : holds_)
        if (--h.t <= 0 && fall_from(h.col, h.row, 1, 1, 8)) h.t = -1000;
    std::erase_if(holds_, [](const Hold& h) { return h.t <= -1000; });
    if (exit_) return;
    if (!level_left()) {
        if (cleared_wait_ == 0) cleared_wait_ = CLEARED_WAIT;
        else if (--cleared_wait_ == 0) result_ = Result::Cleared;
    }
}

void BBEngine::step_bat(const BBEngineInput& in) {
    const float half = float(bat_width_ / 2), lo = half + 1, hi = 0x5F - half;
    if (in.target >= 0) {
        bat_v_ = std::clamp(std::clamp(in.target, lo, hi) - bat_x_, -BAT_MOUSE, BAT_MOUSE);
    } else if (in.dir) {
        if (bat_v_ * float(in.dir) < 0) bat_v_ = 0;  // turning: from rest at once
        bat_v_ = std::clamp(bat_v_ + BAT_ACC * float(in.dir), -BAT_VMAX, BAT_VMAX);
    } else bat_v_ = 0;
    float to = std::clamp(bat_x_ + bat_v_, lo, hi);
    // the ball in its way, level with its end: it stops where it meets it (and knocks it away)
    if (ball_.flying && ball_.falling == 0 && to != bat_x_) {
        const BBPaddleBody now = bat_body();
        const float dz = ball_.z - std::clamp(ball_.z, now.z0, now.z1);
        if (const float reach = R * R - dz * dz; reach > 0) {
            const float r = std::sqrt(reach) - 0.05f;
            const bool right = to > bat_x_;
            auto gap = [&](float c) { const BBPaddleBody b = rules_.bat_body(c, bat_width_); return right ? ball_.x - b.x1 : b.x0 - ball_.x; };
            if (gap(bat_x_) >= r && gap(to) < r) {
                float a = bat_x_, z = to;
                for (int k = 0; k < 20; k++) {
                    const float m = (a + z) / 2;
                    (gap(m) >= r ? a : z) = m;
                }
                to = z;
            }
        }
    }
    bat_x_ = to;
}

void BBEngine::step_ball() {
    Ball& b = ball_;
    if (!b.flying) {  // the serve: the drop (held up there while a missile flies or the bat is eaten)
        if (b.drop == 0) return;
        b.vh -= DROP_G;
        b.h += b.vh;
        if (b.h <= 0) {
            b.h = 0, b.vh = -b.vh * DROP_KEEP;
            if (--b.drop == 0 || b.vh < 0.6f) {
                if (missile_.flying || eating_ > 0) {
                    b.drop = 1, b.vh = 0;
                    return;
                }
                b.drop = 0, b.h = 0, b.flying = true;
                b.vx *= SPEED, b.vz *= SPEED;
            }
        }
        return;
    }
    if (b.falling > 0) {  // past the bat: it falls off the floor's edge
        b.x += b.vx, b.z += b.vz, b.vh -= DROP_G, b.h += b.vh;
        if (++b.falling > FALL_FRAMES) lose_ball();
        return;
    }
    auto steepen = [&] {
        const float min_vz = SPEED * std::cos(MAX_ANGLE);
        if (std::fabs(b.vz) >= min_vz) return;
        b.vz = std::copysign(min_vz, b.vz == 0 ? -1.0f : b.vz);
        b.vx = std::copysign(std::sqrt(SPEED * SPEED - min_vz * min_vz), b.vx);
    };
    auto nudge = [&] {  // many bounces without the bat: each further one turns the path a little
        if (++b.bounces < 8) return;
        const float a = (b.bounces & 1 ? 1.0f : -1.0f) * 0.045f;
        const float vx = b.vx * std::cos(a) - b.vz * std::sin(a), vz = b.vx * std::sin(a) + b.vz * std::cos(a);
        if (std::fabs(vz) >= SPEED * std::cos(MAX_ANGLE)) b.vx = vx, b.vz = vz;
    };
    BBPaddleBody bat = bat_body();
    bat.x0 -= REACH, bat.x1 += REACH;
    const float bat_mid = (bat.x0 + bat.x1) / 2, bat_half = (bat.x1 - bat.x0) / 2;
    const float dist = SPEED * b.boost * (slow_ > 0 ? SLOW : 1.0f) * (fast_ ? 2.0f : 1.0f);
    const int steps = std::max(1, int(std::ceil(dist)));
    const float h = dist / SPEED / float(steps);
    for (int i = 0; i < steps; i++) {
        b.x += b.vx * h, b.z += b.vz * h;
        auto touch = [&](float x, float z) {
            if (!contact) contact = true, contact_at = float(i + 1) / float(steps), contact_x = x, contact_z = z;
        };
        auto wall = [&] { touch(b.x, b.z), events_.push_back({Event::Wall}), nudge(); };
        if (b.x < X_MIN) b.x = 2 * X_MIN - b.x, b.vx = std::fabs(b.vx), wall();
        if (b.x > X_MAX) b.x = 2 * X_MAX - b.x, b.vx = -std::fabs(b.vx), wall();
        if (b.z > Z_MAX) b.z = 2 * Z_MAX - b.z, b.vz = -std::fabs(b.vz), wall();
        // the floor bricks: the sphere against their boxes, off the nearest point of the one it touches
        {
            float best = R, nx = 0, nz = 0, qx = 0, qz = 0;
            size_t which = bricks_.size();
            for (size_t k = 0; k < bricks_.size(); k++) {
                const Brick& br = bricks_[k];
                if (br.state != State::Rest || br.layer != 0) continue;
                const float px = std::clamp(b.x, br.x(), br.x() + 12), pz = std::clamp(b.z, br.z() - 12, br.z());
                const float d = std::hypot(b.x - px, b.z - pz);
                if (d < best && d > 1e-3f && b.vx * (b.x - px) + b.vz * (b.z - pz) < 0)
                    best = d, which = k, nx = (b.x - px) / d, nz = (b.z - pz) / d, qx = px, qz = pz;
            }
            if (which < bricks_.size()) {
                const float vz = b.vz;
                const float dot = b.vx * nx + b.vz * nz;
                b.vx -= 2 * dot * nx, b.vz -= 2 * dot * nz;  // off it, whatever becomes of it
                hit(which, false, vz);
                if (!b.flying) return;  // the exit brick took it
                steepen();
                b.x = qx + nx * R, b.z = qz + nz * R;
                touch(b.x, b.z);
                b.bounces++;
            }
        }
        // the bat: its face sends the ball back by where it hits, its ends and corners save it at the
        // steepest angle; run into the ball's side, it knocks it away
        const float cx = std::clamp(b.x, bat.x0, bat.x1), cz = std::clamp(b.z, bat.z0, bat.z1);
        const float ex = b.x - cx, ez = b.z - cz, d = std::hypot(ex, ez);
        if (d < R) {
            const float nx = d > 1e-4f ? ex / d : 0.0f, nz = d > 1e-4f ? ez / d : 1.0f;
            const bool pushed = bat_v_ * nx > 0.1f;
            if (b.vx * nx + b.vz * nz < 0 || pushed) {
                float along = 0.5f;
                if (nz > 0.7f) {
                    const float a = std::clamp((b.x - bat_mid) / (bat_half + R * 0.5f), -1.0f, 1.0f);
                    b.vx = SPEED * std::sin(a * MAX_ANGLE), b.vz = SPEED * std::cos(a * MAX_ANGLE);
                    along = (a + 1) / 2;
                    if (speed_up) b.boost = std::min(1.5f, b.boost * 1.025f);
                } else {
                    b.vx = std::copysign(SPEED * std::sin(MAX_ANGLE), nx), b.vz = SPEED * std::cos(MAX_ANGLE);
                    along = nx < 0 ? 0.0f : 1.0f;
                }
                b.x = cx + nx * R, b.z = cz + nz * R;
                touch(b.x, b.z);
                b.bounces = 0;
                events_.push_back({Event::Bat, along});
            }
        }
        // creatures on the floor it runs into blow up (it goes on unturned, as in the original)
        if (b.z >= 9)
            for (Creature& c : creatures_) {
                const float lo = std::max(0.0f, b.x - 2);
                if (c.spawn == 0 && c.burst == 0 && !c.on_bat && c.y == 0 && c.z >= 0 && c.x < lo + 11 && c.x + 12 >= lo &&
                    c.z >= b.z - 12 && c.z - 12 < b.z)
                    c.burst = 7, sound(1);
            }
        if (b.z < Z_OUT) {  // past the bat: it falls
            b.falling = 1, b.vx *= 0.5f, b.vz *= 0.5f, b.vh = 0;
            return;
        }
    }
    b.x = std::clamp(b.x, X_MIN, X_MAX), b.z = std::clamp(b.z, 0.0f, Z_MAX);
}

void BBEngine::lose_ball() {
    // the lives are spare balls: with none left the game is over
    events_.push_back({Event::Lost});
    if (!unlimited_lives) {
        if (lives == 0) {
            result_ = Result::Over;
            return;
        }
        lives--;
    }
    serve();
}

void BBEngine::step_exit() {
    // the exit brick: every other frame a floor brick blows up (what stood on it coming down) until
    // the two lowest layers are empty; a while later, the next level
    if (exit_wait_ > 0) {
        if (--exit_wait_ == 0) result_ = Result::Cleared;
        return;
    }
    bool low = false, moving = false;
    for (const Brick& b : bricks_) {
        if (b.state == State::Gone) continue;
        if (b.state != State::Rest && b.state != State::Exploding) moving = true;
        if (b.state != State::Exploding && b.layer <= 1 && !creature_kind(b.kind)) low = true;
    }
    if (!low && !moving) {
        exit_wait_ = EXIT_WAIT;
        return;
    }
    if (frame_ % 2) return;
    std::vector<size_t> floor;
    for (size_t i = 0; i < bricks_.size(); i++)
        if (bricks_[i].state == State::Rest && bricks_[i].layer == 0 && !busy(bricks_[i].col, bricks_[i].row)) floor.push_back(i);
    if (floor.empty()) return;
    const size_t i = floor[random() % floor.size()];
    explode(i, true);
    if (solid(1, bricks_[i].col, bricks_[i].row) >= 0) fall_from(bricks_[i].col, bricks_[i].row, 1, 1, 8);
}

void BBEngine::step_bricks() {
    for (size_t i = 0; i < bricks_.size(); i++) {
        Brick& b = bricks_[i];
        switch (b.state) {
        case State::Exploding:  // 0x19-0x1F, a frame each, then gone
            if (++b.t >= 7) b.state = State::Gone;
            break;
        case State::Sliding:  // two rows towards the bat, slowing
            if (b.t < 7) b.dz += float(SLIDE[b.t]);
            if (++b.t >= 8) b.row -= 2, b.dz = 0, b.state = State::Rest;
            break;
        case State::Falling: {  // down its layers, then at rest a layer (or two) lower
            const int steps = b.fall_layers == 2 ? 11 : 8;
            const int d = b.t - b.fall_start;
            if (d >= 0 && d < steps) b.dy += float(FALL[d]);
            b.t++;
            // the column is done when its top brick has landed: all of it rests then
            bool done = true;
            for (const Brick& o : bricks_)
                if (o.state == State::Falling && o.col == b.col && o.row == b.row && o.t - o.fall_start < (o.fall_layers == 2 ? 11 : 8))
                    done = false;
            if (done)
                for (size_t k = 0; k < bricks_.size(); k++) {
                    Brick& o = bricks_[k];
                    if (o.state != State::Falling || o.col != b.col || o.row != b.row) continue;
                    o.layer -= o.fall_layers, o.dy = 0, o.state = State::Rest;
                    if (o.layer == 0 && creature_kind(o.kind)) become_creature(k);
                }
            break;
        }
        default: break;
        }
        if (b.spawn > 0) b.spawn--;
    }
}

void BBEngine::become_creature(size_t i) {
    // a creature come down to the floor with its column: it walks from there
    Brick& b = bricks_[i];
    Creature c;
    c.x = b.x(), c.z = b.z(), c.type = b.kind == 0x15 ? 2 : 1;
    c.home_col = b.col, c.home_row = b.row, c.id = next_id_++, c.obj = b.obj;
    creatures_.push_back(c);
    b.state = State::Gone, b.obj = -1;
}

void BBEngine::step_creatures() {
    const bool play = ball_.flying || ball_.drop == 0;
    for (size_t n = 0; n < creatures_.size(); n++) {
        Creature& c = creatures_[n];
        if (c.burst > 0) {  // blown up: its explosion, then gone; a stack it stood under comes down
            if (--c.burst == 0) {
                const int col = int(std::lround(c.x / 12)), row = int(std::floor(c.z / 12)) - 1;
                if (c.y == 0 && c.z >= 0 && solid(0, col, row) < 0 && solid(1, col, row) >= 0) holds_.push_back({col, row, 0});
                release(c), creatures_.erase(creatures_.begin() + long(n)), n--;
            }
            continue;
        }
        if (c.spawn > 0) {
            c.spawn--;
            continue;
        }
        if (c.on_bat || eating_ > 0 || !play) continue;  // the bat being eaten: they all freeze
        // it walks towards the bat while the floor cell ahead is free (the cell at row (Z - 12) / 12:
        // its own while it stands square in it, then the next); a falling column counts as taken
        bool go = c.z < 12 || c.y < 0;
        const int col = int(std::lround(c.x / 12));
        if (!go && c.y == 0) {
            const int ahead = int(std::floor((c.z - 12) / 12));
            go = solid(0, col, ahead) < 0 && !busy(col, ahead);
        }
        if (!go) continue;
        const int old_row = int(std::floor(c.z / 12)) - 1;
        c.z -= 2;
        if (c.y == 0 && c.z >= 0 && int(std::floor(c.z / 12)) - 1 != old_row) {
            if (busy(col, old_row)) {  // its old cell is still busy: it waits
                c.z += 2;
                continue;
            }
            if (solid(1, col, old_row) >= 0) holds_.push_back({col, old_row, HOLD_UNDER});  // out from under a stack
        }
        if (c.z >= 0) {
            if (--c.timer == 0) c.timer = 4, c.frame++;
        } else {  // past the front edge: it falls
            c.frame++;
            c.y -= float(c.frame / 4);
        }
        // a missile meets it
        if (missile_.flying && c.y == 0 && c.z >= 0 && c.x < missile_.x + 21 && c.x + 12 >= missile_.x + 10 &&
            c.z >= missile_.z - 12 && c.z - 12 < missile_.z) {
            missile_.flying = false, missile_.burst = 0, cooldown_ = COOLDOWN_CREATURE;
            c.burst = 7, sound(1);
            continue;
        }
        // at the bat's line over the bat: it lands on it and eats it
        if (c.z < 12 && c.z >= 0 && c.y == 0) {
            const float lo = std::max(0.0f, bat_x_ - 2 - float(bat_width_ / 2)), hi = lo + float(2 * (bat_width_ / 2)) - 1;
            if (c.x <= hi && c.x + 12 >= lo) {
                c.on_bat = true;
                eating_ = eat_total_ = bat_width_ == WIDE ? 56 : 42;
                sound(8);
                continue;
            }
        }
        if (c.y < -60) {  // gone below the playfield: one comes back at its home, later
            returning_.push_back({c, RESPAWN_WAIT});
            release(c);
            creatures_.erase(creatures_.begin() + long(n)), n--;
        }
    }
    if (!play || eating_ > 0) return;
    for (size_t n = 0; n < returning_.size(); n++) {
        if (--returning_[n].wait > 0) continue;
        int about = int(creatures_.size());
        for (const Brick& b : bricks_) about += b.state != State::Gone && creature_kind(b.kind);
        if (about >= MAX_ABOUT) {  // a crowd already: try again in a second
            returning_[n].wait = 50;
            continue;
        }
        respawn(returning_[n].who);
        returning_.erase(returning_.begin() + long(n)), n--;
    }
}

void BBEngine::respawn(const Creature& from) {
    int count = int(creatures_.size());
    for (const Brick& b : bricks_) count += b.state != State::Gone && creature_kind(b.kind);
    if (count >= MAX_CREATURES || busy(from.home_col, from.home_row)) return;
    int h = stack_height(from.home_col, from.home_row);
    if (h >= 4) return;
    if (h == 0)  // another one standing there: on top of it
        for (const Creature& o : creatures_)
            if (o.y == 0 && std::lround(o.x / 12) == from.home_col && int(std::floor(o.z / 12)) - 1 == from.home_row) h = 1;
    const int obj = free_object();
    if (obj < 0) return;  // (the original needs a free object too)
    if (h > 0) {  // on a stack: a brick of its kind there, until its column comes down
        Brick b;
        b.obj = obj;
        b.col = from.home_col, b.row = from.home_row, b.layer = h, b.kind = from.type == 2 ? 0x15 : 0x11;
        b.sprite = b.kind, b.spawn = 7;
        bricks_.push_back(b);
        return;
    }
    Creature c;
    c.x = float(from.home_col * 12), c.z = float((from.home_row + 1) * 12);
    c.type = from.type, c.home_col = from.home_col, c.home_row = from.home_row;
    c.spawn = 7, c.id = next_id_++, c.obj = obj;
    creatures_.push_back(c);
}

int BBEngine::free_object() {
    for (Brick& b : bricks_)
        if (b.state == State::Gone && b.obj >= 0) {
            const int obj = b.obj;
            b.obj = -1;
            return obj;
        }
    return -1;
}

void BBEngine::step_missile(const BBEngineInput& in) {
    Missile& m = missile_;
    if (m.burst > 0) m.burst--;
    // fire: held, one in flight at a time, after the cooldown; not before the serve, nor while the bat
    // is being eaten
    if (in.fire && !m.flying && m.burst == 0 && cooldown_ == 0 && eating_ == 0 && ball_.flying && !exit_ &&
        (missiles > 0 || unlimited_missiles)) {
        if (!unlimited_missiles) missiles--;
        m.flying = true, m.x = std::max(0.0f, bat_x_ - 9), m.z = 12, m.frame = 0;
        sound(7);
    }
    if (!m.flying) return;
    m.frame++;
    m.z += MISSILE_V;
    auto burst = [&] {
        m.flying = false, m.burst = 7, cooldown_ = COOLDOWN_BURST;
        sound(1), sound(6);
    };
    if (m.z >= MISSILE_WALL) {
        burst();
        return;
    }
    // the floor brick in its way: the cell at column (X + 6) / 12, row (Z - 4) / 12
    const int col = int((m.x + 6) / 12), row = int((std::min(m.z, 155.0f) - 4) / 12);
    if (const int i = solid(0, col, row); i >= 0) {
        if (hit(size_t(i), true, 0)) m.flying = false;  // through: gone at once, no cooldown
        else burst();
    }
}

void BBEngine::objects(std::vector<BBObject>& out) const {
    out.clear();
    if (!active_) return;
    // the ball, as the game's 0x886C publishes it (its object 5 off its centre), its height in sy
    if ((ball_.flying || ball_.drop > 0) && !exit_) {
        BBObject o{};
        o.x = std::max(0.0f, ball_.x - 5), o.y = 0, o.z = ball_.z + 5;
        o.sx = 148 + ball_.x - ball_.z, o.sy = 104 - ball_.x / 2 - ball_.h;
        o.sprite = 0x0C, o.slot = 0;
        out.push_back(o);
    }
    {  // the bat, eaten away while a creature is on it
        BBObject o{};
        o.sprite = bat_width_ == WIDE ? 0x0A : 0x0B, o.slot = 1;
        int width = bat_width_;
        if (eating_ > 0 && eat_total_ > 0) width = std::max(1, int(float(bat_width_) * float(eating_) / float(eat_total_) + 0.5f));
        rules_.bat_object(bat_x_, width, o);
        out.push_back(o);
    }
    if (missile_.flying || missile_.burst > 0) {
        BBObject o{};
        o.x = missile_.x, o.y = 0, o.z = missile_.z;
        o.sx = 160 - missile_.z + missile_.x + 6, o.sy = 100 - (missile_.x + 6) / 2;
        o.sprite = missile_.flying ? 0x16 + missile_.frame % 3 : 0x19 + (7 - missile_.burst);
        o.slot = 2;
        out.push_back(o);
    }
    for (size_t i = 0; i < bricks_.size(); i++) {
        const Brick& b = bricks_[i];
        if (b.state == State::Gone) continue;
        BBObject o{};
        o.x = b.x(), o.y = float(b.layer * 12) - b.dy, o.z = b.z() - b.dz;
        o.sx = 160 + o.x - o.z, o.sy = 100 - o.x / 2 - o.y;
        int sprite = b.sprite;
        const int table = rules_.sprite[size_t(b.kind)];
        if (table >= 0x81 && table <= 0x86) {  // a bobbing slab: ping-pong through its heights
            const int step = (frame_ / 5 + (table - 0x81)) % 10;
            sprite = SLAB[step < 6 ? step : 10 - step];
        } else if (table == 0x80) sprite = BREATH[(frame_ / 5) % 8];
        if (b.state == State::Exploding) sprite = 0x19 + std::min(b.t, 6);
        if (b.spawn > 0) sprite = 0x18 + b.spawn;
        o.sprite = sprite, o.slot = int(3 + i);
        out.push_back(o);
    }
    for (const Creature& c : creatures_) {
        BBObject o{};
        o.x = c.x, o.y = c.y, o.z = c.z;
        o.sx = 160 + c.x - c.z, o.sy = 100 - c.x / 2 - c.y;
        o.sprite = c.burst > 0 ? 0x19 + (7 - c.burst) : c.spawn > 0 ? 0x18 + c.spawn : c.type == 2 ? 0x15 : 0x11 + (c.frame & 3);
        o.slot = 400 + c.id % 400;
        out.push_back(o);
    }
}


void BBEngine::release(const Creature& c) {
    // its object is free again (held by a gone brick, as the game's list keeps it): drawn as none
    if (c.obj < 0) return;
    Brick b;
    b.state = State::Gone, b.obj = c.obj;
    bricks_.push_back(b);
}

void BBEngine::draws(std::vector<BBEngineDraw>& out) const {
    // as the game would publish them (its 0x886C, 0x88AD, 0x9BE6 and the bricks' 0x88CF): whole pixels
    out.clear();
    if (!active_) return;
    auto r = [](float v) { return int(std::lround(v)); };
    {  // the ball: its centre's X / Z, the sprite 5 off
        const bool shown = (ball_.flying || ball_.drop > 0) && !exit_;
        const int x = r(ball_.x), z = r(ball_.z);
        out.push_back({0, shown ? 148 + x - z : 0, r(104 - ball_.x / 2 - ball_.h), 0x0C, std::max(0, x - 5), 0, z + 5});
    }
    {  // the bat: from its left end, X 7 further left for the depth order
        const int left = r(bat_x_) - bat_width_ / 2;
        out.push_back({1, 153 + left, 100 - left / 2, bat_width_ == WIDE ? 0x0A : 0x0B, std::max(0, left - 7), 0, 7});
    }
    {
        const bool shown = missile_.flying || missile_.burst > 0;
        const int x = r(missile_.x), z = r(missile_.z);
        out.push_back({2, shown ? 166 - z + x : 0, 100 - (x + 6) / 2, missile_.flying ? 0x16 + missile_.frame % 3 : 0x19 + (7 - missile_.burst), x, 0, z});
    }
    std::vector<BBObject> objs;
    objects(objs);
    for (size_t i = 0; i < bricks_.size(); i++) {
        const Brick& b = bricks_[i];
        if (b.obj < 0) continue;
        if (b.state == State::Gone) {
            out.push_back({b.obj, 0, 0, 0x20, 0, 0, 0});
            continue;
        }
        for (const BBObject& o : objs)
            if (o.slot == int(3 + i)) out.push_back({b.obj, r(o.sx), r(o.sy), o.sprite, r(o.x), r(o.y), r(o.z)});
    }
    for (const Creature& c : creatures_)
        for (const BBObject& o : objs)
            if (c.obj >= 0 && o.slot == 400 + c.id % 400) out.push_back({c.obj, r(o.sx), r(o.sy), o.sprite, r(o.x), r(o.y), r(o.z)});
}


