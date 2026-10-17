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

// Exercise the real movement routine, including its prechecks and projector.
int main(int argc,char** argv) {
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Even paddle rail");
 auto s=Spectrum::from_z80(d);if(!s)return 2;BallBreakerView v;
 struct Case {int width,centre;bool original,fast;};std::vector<Case> cases;
 for(bool original:{false,true})for(bool fast:{false,true})for(int width:{1,2,3,4,8,12,16})
  for(int centre:{width/2,width/2+1,46,92-width+width/2+std::min(3,width/2)-1,96-width+width/2})
   cases.push_back({width,centre,original,fast});
 size_t index=0;int expected=0,fail=0,left=0,right=0;bool pending=false;
 s->on_exec(0x89d4,[&](const zxc_regs_t& r){
  if(index>=cases.size())return;
  const auto c=cases[index];v.original_speed_=c.original;v.fast_mouse_=c.fast;
  v.aim_=(r.bc&4)?0:100;v.modern_previous_body_.reset();
  s->poke(0x8061,uint8_t(c.centre));s->poke(0x8063,uint8_t(c.width));s->poke(0x8062,7);
  // Independent arithmetic oracle: rendered body [centre-half,centre-half+width-inset].
  expected=c.centre;int half=c.width/2,inset=std::min(3,c.width/2);
  int steps=c.fast?std::clamp(int(std::abs(v.aim_-expected))/2,1,4):1;
  for(int i=0;i<steps;++i) {
   if(r.bc&4) {if(c.original){if(expected>half+1)expected-=2;}else expected=std::max(half,std::min(92-c.width+half+inset,expected-2));}
   if(r.bc&2) {if(c.original){if(expected+half<95)expected+=2;}else expected=std::max(half,std::min(92-c.width+half+inset,expected+2));}
  }
  pending=true;
 });
 v.start(*s,"remaster/assets/ballbreaker");v.unlimited_lives_=true;v.preview_enabled_=false;
 s->on_exec(0x88ad,[&](const zxc_regs_t&){
  if(!pending)return;const auto c=cases[index];
  auto b=v.modern_body(*s);
  bool bad=s->peek(0x8061)!=expected;
  if(!c.original){bad|=b.x0<-.001 || b.x1>92.001;if(b.x0==0)++left;if(b.x1==92)++right;}
  if(bad){fprintf(stderr,"FAIL case%zu original%d fast%d width%d centre%d got%d expected%d body%.1f..%.1f\n",index,c.original,c.fast,c.width,c.centre,s->peek(0x8061),expected,b.x0,b.x1);++fail;}
  pending=false;++index;
 });
 for(int frame=0;frame<2400 && index<cases.size();++frame){
  Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
  if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
  if(frame>400)c.dirs=index%2?ZXC_JOY_RIGHT:ZXC_JOY_LEFT;
  v.input(*s,c);s->run_frame();v.after_frame(*s);
 }
 printf("rail cases %zu/%zu; exact left %d right %d; failures %d\n",index,cases.size(),left,right,fail);
 CloseWindow();return fail || index!=cases.size() || !left || !right;
}
