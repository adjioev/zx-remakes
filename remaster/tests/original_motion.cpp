#include <algorithm>
#include <cstdint>
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
#define private public
#include "ballbreaker_game.h"
#undef private
#include <cstdio>

int main(int argc, char** argv) {
    BallBreakerView::Glide g{};
    g.dur = 0.04; g.coast = 0.5f;
    for (int i=0;i<5;++i) g.to[i]=4;
    float a[5], b[5];
    g.at(0.04, a); g.at(0.04+1.0/60, b);
    if (!(b[0]>a[0] && b[0]<6)) return 1; // moves even without a new simulation sample
    g.at(100, a);
    if (std::abs(a[0]-6)>0.001) return 1; // never runs away during a long pause
    g.coast=0; g.at(0.1,a);
    if (a[0]!=4) return 1; // collision guard disables extrapolation
    if (argc!=2) return 2;
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
    SetTraceLogLevel(LOG_WARNING); SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320,180,"Original motion regression");
    auto s=Spectrum::from_z80(data);
    if (!s) return 2;
    BallBreakerView v;
    v.start(*s,"remaster/assets/ballbreaker");
    v.on_key(*s,KEY_O); v.on_key(*s,KEY_L);
    int predicted=0, guarded=0;
    for (int frame=0;frame<1500;++frame) {
        Controls c;
        if (frame>=10 && frame<20) c.ok=c.fire=true,c.keys.insert(' ');
        if (frame>=75 && frame<85) c.ok=true,c.keys.insert(13);
        v.input(*s,c); s->run_frame(); v.after_frame(*s);
        auto it=v.glides_.find(0);
        if(it!=v.glides_.end()) {
            predicted+=it->second.coast>0; guarded+=it->second.coast==0;
            for (int n=0;n<3;++n) {
                BBState st=v.latest_;
                v.smooth_apply(st,v.game_t_+n/150.0);
                for(const auto& o:st.objects)
                    if(!std::isfinite(o.sx)||!std::isfinite(o.sy)) return 1;
            }
        }
    }
    CloseWindow();
    std::printf("Original motion: %d prediction frames, %d guarded frames\n",predicted,guarded);
    return predicted>0 && guarded>0 ? 0 : 1;
}
