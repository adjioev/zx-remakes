// Independent distance sampling checks the continuous collision solver.
#include "ballbreaker_collision.h"
#include <cstdio>
#include <random>

int main() {
    int failures = 0, checked = 0;
    auto check = [&](bool ok, const char* why) {
        if (!ok && failures++ < 8) std::fprintf(stderr, "FAIL: %s\n", why);
    };
    const BBPaddleBody body{39,48,0,7,12};
    check(bb_sweep_paddle(43,25,43,-10,body).hit, "fast front crossing");
    check(bb_sweep_paddle(20,5,60,5,body).hit, "fast side crossing");
    check(!bb_sweep_paddle(33,13,33,12,body).hit, "expanded-box corner is not sphere contact");
    check(!bb_sweep_paddle(43,13,43,20,body).hit, "separating front tangent");
    check(bb_sweep_paddle(54,5,54,5,body,2).hit, "moving paddle catches stationary sphere");
    std::mt19937 rng(4321);
    std::uniform_real_distribution<float> x(15,72), z(-12,32), velocity(-25,25), motion(-8,8);
    for (int trial=0; trial<12000; ++trial) {
        float ax=x(rng), az=z(rng), bx=ax+velocity(rng), bz=az+velocity(rng), move=motion(rng);
        auto distance = [&](float t) {
            float px=ax+(bx-ax-move)*t, pz=az+(bz-az)*t;
            return std::hypot(px-std::clamp(px,body.x0,body.x1), pz-std::clamp(pz,body.z0,body.z1));
        };
        // Existing overlaps are recovery policy, not first-entry sweep cases.
        if (distance(0) <= BB_BALL_RADIUS + .01f) continue;
        auto hit=bb_sweep_paddle(ax,az,bx,bz,body,move);
        int first=-1;
        for (int step=1; step<=2000; ++step)
            if (distance(step/2000.f)<BB_BALL_RADIUS) {first=step; break;}
        if (first>=0) {
            check(hit.hit, "sampled penetration must have an earlier collision");
            if (hit.hit) check(hit.fraction <= first/2000.f+.0001f &&
                              hit.fraction >= (first-1)/2000.f-.0001f, "earliest impact time");
        }
        if (hit.hit) {
            check(std::abs(distance(hit.fraction)-BB_BALL_RADIUS)<.002f, "impact lies on exact rounded surface");
            check(std::abs(std::hypot(hit.nx,hit.nz)-1)<.001f, "unit contact normal");
            check((bx-ax-move)*hit.nx+(bz-az)*hit.nz<0, "only approaching contacts");
        }
        ++checked;
    }
    std::printf("Swept geometry: %d independent sampled paths, %d failures\n",checked,failures);
    return failures ? 1 : 0;
}
