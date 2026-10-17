// Integration regression: runs the real snapshot, game adapter and renderer.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>
// Inspect presentation state without adding test-only accessors to the game API.
#define private public
#include "ballbreaker_game.h"
#undef private

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: paddle_contact <original.z80>\n"); return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    auto s = Spectrum::from_z80(data);
    if (!s) return 2;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 180, "Paddle contact regression");
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
    };
    {
        BallBreakerView view;
        view.start(*s, "remaster/assets/ballbreaker");
        view.set_remake(true);
        int hits = 0, hit_sounds = 0;
        double contact_t = -1;
        uint64_t last_hit = 0;
        bool checked = false, checked_early = false;
        for (int frame = 0; frame < 450; ++frame) {
            Controls c;
            if (frame >= 10 && frame < 20) c.ok = c.fire = true, c.keys.insert(' ');
            if (frame >= 75 && frame < 85) c.ok = true, c.keys.insert(13);
            if (frame >= 400 && frame < 407) c.dirs = c.arrows = ZXC_JOY_LEFT;
            view.input(*s, c);
            s->run_frame();
            view.after_frame(*s);
            if (view.paddle_hit_.id && view.paddle_hit_.id != last_hit) {
                last_hit = view.paddle_hit_.id; ++hits;
                contact_t = view.paddle_hit_.t;
            }
            double t = double(s->frames()) / 50;
            view.prepare(*s, false, t);
            std::vector<SfxEvent> sounds;
            view.take_sfx(sounds);
            for (auto e : sounds) if (e.kind == Sfx::BatHit) ++hit_sounds;
            // The real shallow hit used to flip the ball over the paddle's end
            // at 8.68 s, while the sphere still overlapped its physical bounds.
            if (hits && t >= contact_t + BB_RENDER_DELAY && t <= contact_t + BB_RENDER_DELAY + .06) {
                for (auto theme : {BBTheme::ClassicHD, BBTheme::Neon}) {
                    view.hd_->set_theme(theme);
                    auto order = view.hd_->scene_order(view.state_);
                    auto ball = std::find_if(order.begin(), order.end(), [](auto o) { return o->slot == 0; });
                    auto bat = std::find_if(order.begin(), order.end(), [](auto o) { return o->slot == 1; });
                    check(ball < bat, "contact sphere stays behind the paddle across adjacent frames");
                }
            }
            if (!hits) check(view.hd_->bat_waves_.empty(), "no paddle flashes without a confirmed collision");
            if (hits && !checked_early) {
                check(view.hd_->bat_waves_.empty(), "no flash before the delayed ball reaches contact");
                check(hit_sounds == 0, "no early paddle sound");
                checked_early = true;
            }
            if (hits && t + 1e-8 >= contact_t + 0.04 && !checked) {
                const auto& state = view.state_;
                auto bat = std::find_if(state.objects.begin(), state.objects.end(), [](auto o) { return o.slot == 1; });
                check(bat != state.objects.end(), "paddle exists at contact");
                if (bat != state.objects.end()) {
                    check(std::abs(bat->x - (view.original_speed_ ? bb_original_paddle_left(float(s->peek(0x8061)-s->peek(0x8063)/2),s->peek(0x8063)) : bb_even_paddle_left(s->peek(0x8061),s->peek(0x8063)))) < .001f, "paddle uses collision X, not sprite-sort X");
                    check(bat->z == s->peek(0x8062), "paddle uses collision Z");
                    auto body = view.hd_->bat_body(*bat);
                    check(body.z1 == s->peek(0x8062), "paddle face stays at the original sprite Z");
                    check(body.z0 >= 0, "whole paddle base stays inside the room");
                    check(160 + body.x0 - bat->bat_inset - body.z1 == bat->sx,
                          "paddle play-side corner matches original sprite X");
                    check(std::abs(body.x1 - body.x0 - (s->peek(0x8063) - 3)) < .001f, "drawn width accounts for native asymmetric tolerance");
                }
                check(view.hd_->bat_waves_.size() == 1, "one flash for the real shallow-angle paddle hit");
                check(hit_sounds == 1, "one sound at the delayed contact");
                // Extra renders, including theme changes, must not retrigger the same hit.
                view.prepare(*s, false, t);
                view.hd_->set_theme(BBTheme::ClassicHD);
                view.prepare(*s, false, t);
                view.hd_->set_theme(BBTheme::Neon);
                view.prepare(*s, false, t);
                check(view.hd_->bat_waves_.size() == 1, "rerenders and theme switches do not duplicate a hit");
                checked = true;
            }
        }
        check(hits == 1 && checked, "script reproduces exactly one confirmed paddle collision");
        check(hit_sounds == 1, "direction heuristic does not add a second paddle sound");
        // The original clips sprite-sort X near the left end of the rail. Physical
        // bounds must still come from the collision variables for every bat size.
        for (int width : {4, 6, 8, 12, 16}) for (int centre : {width/2, 28, 80}) {
            s->poke(0x8061, uint8_t(centre));
            s->poke(0x8063, uint8_t(width));
            s->poke(0xF3FC, uint8_t(std::max(0, centre - width/2 - 7)));
            const auto state = view.read_state(*s);
            auto bat = std::find_if(state.objects.begin(), state.objects.end(), [](auto o) { return o.slot == 1; });
            check(bat != state.objects.end(), "paddle exists for width/end-of-rail check");
            if (bat == state.objects.end()) continue;
            const auto body = view.hd_->bat_body(*bat);
            float inset=std::min(3,width/2);
            float left=view.original_speed_ ? bb_original_paddle_left(float(centre-width/2),width)+inset : float(centre-width/2);
            check(std::abs(body.x0-left)<.001f && std::abs(body.x1-(left+width-inset))<.001f,
                  "all paddle widths and rail positions match the game's collision interval");
        }
        // Missing objects and genuinely foreground/airborne balls must still work.
        {
            BBObject paddle{}; paddle.slot = 1; paddle.x = 22; paddle.z = 12; paddle.bat_width = 12;
            auto body = view.hd_->bat_body(paddle);
            for (auto centre : {Vector3{10, 5, 15}, Vector3{28, body.height + 8, 12},
                                Vector3{28, 5, body.z0 - 8}}) {
                BBObject sphere{}; sphere.slot = 0; sphere.x = centre.x - 5;
                sphere.sx = 160 + centre.x - centre.z - 12;
                sphere.sy = 118 - centre.x/2 - centre.y - 10;
                BBState sample; sample.objects = {sphere, paddle};
                auto order = view.hd_->scene_order(sample);
                check(order.back()->slot == 0, "sphere clearing a visible face draws in front");
                sample.objects = {sphere};
                check(view.hd_->scene_order(sample).size() == 1, "no paddle is safe");
            }
        }
        // Replay the actual event through fresh renderers at common display rates.
        BBState state = view.state_;
        for (int hz : {50, 60, 120, 144}) {
            BBRenderer renderer("remaster/assets/fonts", "", "");
            renderer.set_theme(BBTheme::ClassicHD);
            bool emitted = false;
            for (int tick = 0; tick < hz/2; ++tick) {
                double t = contact_t - 0.02 + double(tick)/hz;
                renderer.prepare(state, t);
                if (t + 1e-8 < contact_t + BB_RENDER_DELAY)
                    check(renderer.bat_waves_.empty(), "no early flash at any display rate");
                else if (!emitted) {
                    check(renderer.bat_waves_.size() == 1, "contact is not lost at any display rate");
                    emitted = true;
                }
                check(renderer.bat_waves_.size() <= 1, "at most one wave at any display rate");
            }
            check(emitted, "display-rate replay reached contact");
        }
        {
            BBRenderer renderer("remaster/assets/fonts", "", "");
            renderer.set_theme(BBTheme::ClassicHD);
            renderer.prepare(state, contact_t + 0.04);
            check(renderer.bat_waves_.size() == 1, "wave exists before original-view interval");
            renderer.prepare(state, contact_t + 2.0);
            check(renderer.bat_waves_.empty(), "old wave expires while HD rendering is suspended");
        }
    }
    CloseWindow();
    if (!failures) std::puts("PASS: paddle geometry, delayed contact, sound and theme-switch deduplication");
    return failures ? 1 : 0;
}
