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
 if(argc!=3)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 const bool sequel=std::string(argv[2])=="bb2";
 auto loaded=sequel?Spectrum::from_tape(d,true):Spectrum::from_z80(d);if(!loaded)return 2;
 const auto saved=loaded->save_state();
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Smoothing scope regression");
 int fail=0;
 for(const char* setting:{"unset","0","1"}) {
  if(std::string(setting)=="unset")unsetenv("BB_ORIGINAL_PREVIEW");else setenv("BB_ORIGINAL_PREVIEW",setting,1);
  auto a=Spectrum::from_state(saved),b=Spectrum::from_state(saved);
  BallBreakerView view(sequel?BALL_BREAKER_2:BALL_BREAKER),control(sequel?BALL_BREAKER_2:BALL_BREAKER);
  view.start(*a,"remaster/assets/ballbreaker");control.start(*b,"remaster/assets/ballbreaker");
  bool expected=!sequel && std::string(setting)!="0";
  if(view.preview_enabled_!=expected || view.original_speed_!=(expected&&std::string(setting)=="1"))++fail;
  control.preview_enabled_=false;
  view.unlimited_lives_=control.unlimited_lives_=true;
  int moving=0,hits=0,sounds=0;std::set<int> levels;
  a->on_exec(sequel?0x8a95:0x8a73,[&](const zxc_regs_t&){++hits;});
  for(int frame=0;frame<1500;++frame) {
   if(frame==650) {view.on_key(*a,KEY_O);control.on_key(*b,KEY_O);}
   if(frame==950) {view.on_key(*a,KEY_O);control.on_key(*b,KEY_O);}
   Controls c;
   if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
   if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
   if(frame>300){int dx=int(a->peek(0x8064))-int(a->peek(0x8061));c.dirs=dx < -2?ZXC_JOY_LEFT:dx>2?ZXC_JOY_RIGHT:0;}
   view.input(*a,c);a->run_frame();view.after_frame(*a);
   control.input(*b,c);b->run_frame();control.after_frame(*b);
   if(a->dump()!=b->dump() || a->cpu_ticks()!=b->cpu_ticks()){++fail;fprintf(stderr,"RAM/tick divergence frame %d\n",frame);break;}
   auto shown=view.latest_;view.smooth_apply(shown,(frame+1)/50.);
   for(auto&o:shown.objects)if(o.slot==0){++moving;if(!std::isfinite(o.sx)||!std::isfinite(o.sy))++fail;}
   std::vector<SfxEvent> events;view.take_sfx(events);for(auto&e:events)if(e.kind==Sfx::BatHit)++sounds;
   levels.insert(a->peek(0x8070)+1);
  }
  printf("%s env=%s preview=%d ball frames=%d hits=%d sounds=%d levels=%zu RAM/ticks identical\n",sequel?"BB2":"BB1",setting,view.preview_enabled_,moving,hits,sounds,levels.size());
  if(!moving||!hits||hits!=sounds)++fail;
 }
 unsetenv("BB_ORIGINAL_PREVIEW");CloseWindow();return fail?1:0;
}
