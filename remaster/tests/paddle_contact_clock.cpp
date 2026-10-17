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

// A real native paddle-projector callback moves through a stationary sphere.
// The fixture controls the poses, not the collision response or publication.
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary);
 std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Moving contact clock");
 auto s=Spectrum::from_z80(d);if(!s)return 2;
 BallBreakerView v;int frame=0,fail=0,sounds=0;bool injected=false,observed=false,shown=false;
 uint64_t now=0,old=0,impact=0,id=0;double event_t=-1;
 auto check=[&](bool ok,const char*why){if(!ok){fprintf(stderr,"FAIL %s\n",why);++fail;}};
 s->on_exec(0x88ad,[&](const zxc_regs_t&){
  if(injected||frame<300||s->peek(0x807a)||s->peek(0x809a)||s->peek(0x8083)>1)return;
  injected=true;v.original_speed_=false;
  s->poke(0x8061,45);s->poke(0x8062,7);s->poke(0x8063,12);s->poke(0xf3f8,0x0a);
  s->poke(0x8064,55);s->poke(0x8065,4);s->poke(0x8067,0);s->poke(0x8066,1);
  s->poke(0x8076,0);s->poke(0x808d,0);
  v.modern_previous_body_=v.modern_body(*s);v.modern_episode_=false;v.modern_pending_hit_.reset();
  now=s->video_ticks();old=now-uint64_t((s->model()==128?3546894.:3500000.)*.02);
  v.even_paddle_clock_=std::pair{36.f,old};v.even_previous_paddle_clock_.reset();
  s->poke(0x8061,47);id=v.paddle_hit_id_;
 });
 v.start(*s,"remaster/assets/ballbreaker");v.set_remake(true);
 s->on_exec(0x88ad,[&](const zxc_regs_t&){
  if(!injected||observed)return;observed=true;
  check(v.paddle_hit_id_==id+1,"one published moving-paddle collision");id=v.paddle_hit_id_;
  check(bool(v.modern_event_clock_),"contact retains exact machine clock");
  if(!v.modern_event_clock_)return;
  impact=v.modern_event_clock_->second;
  check(impact>=old&&impact<=now,"TOI inside actual movement interval");
  double phase=double(impact-old)/double(now-old);
  check(std::abs(v.paddle_hit_.bat_left-(36+2*phase))<.001,"contact paddle pose agrees with movement clock");
  check(std::abs(v.paddle_hit_.bat_left-37)<.001,"halfway side impact");
  check(v.paddle_hit_.nx>.99&&std::abs(v.paddle_hit_.x-55)<.001,"actual sphere side contact");
 });
 for(frame=0;frame<700;++frame) {
  Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
  if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
  v.input(*s,c);s->run_frame();v.after_frame(*s);
  double t=v.game_t_;
  if(observed&&event_t<0) {
   event_t=v.paddle_hit_.t;
   double expected=t-double(s->video_ticks()-impact)/(s->model()==128?3546894.:3500000.);
   check(std::abs(event_t-expected)<1e-9,"published event uses exact TOI clock");
  }
  v.prepare(*s,false,t);std::vector<SfxEvent>events;v.take_sfx(events);
  if(observed)for(auto e:events)if(e.kind==Sfx::BatHit)++sounds;
  if(event_t>=0&&t<event_t+BB_RENDER_DELAY-1e-8) {
   check(sounds==0,"no early contact sound");check(v.hd_->bat_waves_.empty(),"no early contact flash");
  }
  if(event_t>=0&&t>=event_t+BB_RENDER_DELAY&&!shown) {
   shown=true;check(sounds==1,"one delayed contact sound");check(v.hd_->bat_waves_.size()==1,"one delayed contact flash");
   auto state=v.read_state(*s);v.smooth_apply(state,event_t+BB_RENDER_DELAY);
   auto ball=std::find_if(state.objects.begin(),state.objects.end(),[](auto&o){return o.slot==0;});
   auto bat=std::find_if(state.objects.begin(),state.objects.end(),[](auto&o){return o.slot==1;});
   check(ball!=state.objects.end()&&bat!=state.objects.end(),"paired contact objects exist");
   if(ball!=state.objects.end()&&bat!=state.objects.end()) {
    check(std::abs(ball->x+5-v.paddle_hit_.x)<.002&&std::abs(ball->z-5-v.paddle_hit_.z)<.002,"exact sphere contact anchor");
    check(std::abs(bat->x-v.paddle_hit_.bat_left)<.002,"exact paddle contact anchor");
   }
  }
  if(shown&&t>event_t+BB_RENDER_DELAY+.04)break;
 }
 check(injected&&observed&&shown,"native moving-paddle fixture completed");
 check(sounds==1,"no duplicate sound during contact episode");
 printf("moving contact clock failures %d\n",fail);CloseWindow();return fail?1:0;
}
