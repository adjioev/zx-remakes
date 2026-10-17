// Integration regression: real snapshot, game adapter and presentation sampling.
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


#include <cmath>
#include <chrono>
#include <numeric>
#include <cstdlib>
int main(int argc,char**argv) {
 if(argc<2)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Original preview regression");
 auto a=Spectrum::from_z80(d),b=Spectrum::from_z80(d);if(!a||!b)return 2;
 BallBreakerView baseline,preview;baseline.start(*a,"remaster/assets/ballbreaker");preview.start(*b,"remaster/assets/ballbreaker");
 baseline.original_speed_=preview.original_speed_=true;baseline.preview_enabled_=false;preview.preview_enabled_=true;
 baseline.unlimited_lives_=preview.unlimited_lives_=true;
 int frame=0,failures=0,contacts=0,sounds=0,false_turns=0,active=0;
 uint64_t hit=0;double lastx=0,lastz=0,lastsx=0;bool have=false;
 std::vector<double> speeds,baseline_speeds,costs;double last_base=0;
 const bool juke=argc>2;
 for(int tick=1;tick<=1800;++tick) {
  double t=tick/60.0;
  while((frame+1)/50.0<=t+1e-8) {
   Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
   if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
   if(frame>300){int dx=int(a->peek(0x8064))-int(a->peek(0x8061));c.dirs=dx < -2?ZXC_JOY_LEFT:dx>2?ZXC_JOY_RIGHT:0;}
   if(juke&&frame>450&&a->peek(0x8065)<35)c.dirs=(frame/3)%2?ZXC_JOY_LEFT:ZXC_JOY_RIGHT;
   baseline.input(*a,c);a->run_frame();baseline.after_frame(*a);
   auto begin=std::chrono::steady_clock::now();
   preview.input(*b,c);b->run_frame();preview.after_frame(*b);
   costs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
   if(a->dump()!=b->dump() || a->cpu_ticks()!=b->cpu_ticks()) {std::fprintf(stderr,"FAIL authoritative divergence at frame %d\n",frame);return 1;}
   std::vector<SfxEvent> events;preview.take_sfx(events);for(const auto&e:events)if(e.kind==Sfx::BatHit)++sounds;
   if(preview.paddle_hit_.id!=hit){
    hit=preview.paddle_hit_.id;
    if(hit){++contacts;BBObject ball{};
     if(std::abs(preview.paddle_motion_.at(preview.game_t_)-(b->peek(0x8061)-b->peek(0x8063)/2))>.001 ||
        !preview.preview_.at(preview.game_t_,ball) || std::abs((ball.z-5)-b->peek(0x8062))>.001 || preview.delay_!=0) {
      ++failures;std::fprintf(stderr,"FAIL contact anchor at %.3f z %.3f\n",preview.game_t_,ball.z);
     }
    }
   }
   ++frame;
  }
  BBState base=baseline.latest_,state=preview.latest_;baseline.smooth_apply(base,t);preview.smooth_apply(state,t);
  auto get=[](const BBState&s)->const BBObject*{for(const auto&o:s.objects)if(o.slot==0)return &o;return nullptr;};
  auto ball=get(state);auto old=get(base);if(!ball||!old)continue;
  if(!std::isfinite(ball->sx)||!std::isfinite(ball->sy)){++failures;break;}
  if(have && t>10.4+1e-7 && t<=10.8+1e-7){speeds.push_back((ball->sx-lastsx)*60);baseline_speeds.push_back((old->sx-last_base)*60);}
  if(have && t>9.5 && !b->peek(0x809a) && !b->peek(0x8083) && ball->z<35 && int8_t(b->peek(0x8066))<0 && ball->z-lastz>0.2 && std::abs(ball->x-lastx)<12) ++false_turns;
  if(!preview.preview_.path_.empty())++active;
  lastx=ball->x;lastz=ball->z;lastsx=ball->sx;last_base=old->sx;have=true;
 }
 auto sd=[](const auto&v){double mean=std::accumulate(v.begin(),v.end(),0.0)/v.size(),sq=0;for(double x:v)sq+=(x-mean)*(x-mean);return std::sqrt(sq/v.size());};
 std::sort(costs.begin(),costs.end());
 std::printf("%s: straight speed std baseline %.3f preview %.3f, range %.3f..%.3f; contacts %d sounds %d false turns %d active frames %d; sample p95 %.3f ms max %.3f ms\n",juke?"juke":"follow",sd(baseline_speeds),sd(speeds),*std::min_element(speeds.begin(),speeds.end()),*std::max_element(speeds.begin(),speeds.end()),contacts,sounds,false_turns,active,costs[costs.size()*95/100],costs.back());
 if(contacts!=sounds||false_turns||(!juke&&(sd(speeds)>40 || *std::min_element(speeds.begin(),speeds.end())>-50)))++failures;
 CloseWindow();return failures?1:0;
}
