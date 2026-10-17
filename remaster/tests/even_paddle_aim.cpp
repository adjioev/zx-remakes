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

// Native integration: inject BEFORE the adapter's hook, inspect the actual add
// result and the event source. Original fixtures retain native acceptance/aim.
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Geometric Even paddle");
 auto s=Spectrum::from_z80(d);if(!s)return 2;BallBreakerView v;
 struct Case {bool original;int width,x,z,dx,dz;bool hit;int aim;};
 std::vector<Case> cases;
 for(int width:{1,2,3,4,6,12,16}) {
  int left=42-width/2,inset=std::min(3,width/2),lo=left+inset,hi=left+width;
  for(int x=lo;x<=hi;++x) {
   int aim=0;
   float off=x-(lo+hi)/2.f,ad=std::abs(off),w=hi-lo;
   if(ad>std::max(.5f,w/8))aim=(off<0?-1:1)*(ad<=w*3/8?1:2);
   cases.push_back({false,width,x,14,0,-1,true,aim});
  }
 }
 cases.push_back({false,12,32,3,1,1,true,-1}); // recover only this step's drain mark
 cases.push_back({false,12,32,4,1,1,true,-1}); // left side, outgoing in Z
 cases.push_back({false,12,55,4,-1,1,true,1}); // right side
 cases.push_back({false,12,34,13,2,-1,true,0}); // rounded corner
 cases.push_back({false,12,32,12,0,-1,false,0}); // expanded-AABB false corner
 cases.push_back({false,12,33,7,0,-1,false,0}); // separating/tangent side
 for(int width:{4,6,12,16})for(int x=27;x<=58;++x)for(int dx=-2;dx<=2;++dx) {
  int offset=x-42+6,native=offset<0?-2:offset>=12?2:int8_t(s->peek(uint16_t(0xb45c+offset)));
  bool hit=x+3>=42-width/2 && std::max(0,x-6)<42+width/2;
  cases.push_back({true,width,x,6,dx,-1,hit,std::clamp(dx+native,-2,2)});
 }
 size_t index=0;bool pending=false;int fail=0,hits=0,misses=0;uint64_t before_id=0;
 s->on_exec(0x9140,[&](const zxc_regs_t&){
  if(index>=cases.size())return;const auto& c=cases[index];
  v.original_speed_=c.original;v.modern_previous_body_.reset();v.modern_pending_hit_.reset();v.modern_episode_=false;
  s->poke(0x8061,uint8_t(42+(c.original?0:std::min(3,c.width/2))));s->poke(0x8062,7);s->poke(0x8063,uint8_t(c.width));
  s->poke(0x8064,uint8_t(c.x));s->poke(0x8065,uint8_t(c.z));
  s->poke(0x8066,uint8_t(c.dz));s->poke(0x8067,uint8_t(c.dx));
  s->poke(0x807a,0);s->poke(0x809a,0);s->poke(0x8083,0);
  before_id=v.paddle_hit_id_;pending=true;
 });
 v.start(*s,"remaster/assets/ballbreaker");v.preview_enabled_=false;
 s->on_exec(0x886b,[&](const zxc_regs_t&){
  if(!pending)return;const auto& c=cases[index];
  bool hit=v.paddle_hit_id_!=before_id;
  int dx=int8_t(s->peek(0x8067)),dz=int8_t(s->peek(0x8066));
  bool bad=hit!=c.hit || dx!=(hit?c.aim:c.dx);
  if(hit && !c.original) {
   auto body=v.modern_body(*s);float x=s->peek(0x8064),z=s->peek(0x8065);
   float distance=std::hypot(x-std::clamp(x,body.x0,body.x1),z-std::clamp(z,body.z0,body.z1));
   const auto& e=v.paddle_hit_;
   float contact=std::hypot(e.x-std::clamp(e.x,body.x0,body.x1),e.z-std::clamp(e.z,body.z0,body.z1));
   bad|=!e.geometric || distance<BB_BALL_RADIUS-.001 || std::abs(contact-BB_BALL_RADIUS)>.002 || dx*e.nx+dz*e.nz<=0;
  }
  if(c.original)bad|=dz!=(hit?1:c.dz) || (hit&&v.paddle_hit_.geometric);
  if(bad){if(fail<12)fprintf(stderr,"FAIL case%zu %s width%d x%d z%d dx%d expected%d hit%d expected%d\n",index,c.original?"Original":"Even",c.width,c.x,c.z,dx,hit?c.aim:c.dx,hit,c.hit);++fail;}
  if(hit)++hits;else++misses;pending=false;++index;
 });
 for(int frame=0;frame<4000 && index<cases.size();++frame) {
  Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
  if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
  v.input(*s,c);s->run_frame();v.after_frame(*s);
 }
 printf("geometric/native contact cases %zu/%zu hits%d misses%d failures%d\n",index,cases.size(),hits,misses,fail);

 int pushes=0;
 auto check=[&](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL moving/drain: %s\n",why);++fail;}};
 for(int width:{1,2,3,4,6,12,16})for(int direction:{-1,1}) {
  v.original_speed_=false;v.modern_previous_body_.reset();v.modern_pending_hit_.reset();v.modern_episode_=false;
  s->poke(0x8061,42);s->poke(0x8062,7);s->poke(0x8063,uint8_t(width));s->poke(0xf3f8,0x0a);
  s->poke(0x807a,0);s->poke(0x809a,0);s->poke(0x8083,0);s->poke(0x8076,0);
  auto old=v.modern_body(*s);
  s->poke(0x8064,uint8_t(direction>0?old.x1+7:old.x0-7));s->poke(0x8065,4);
  s->poke(0x8067,0);s->poke(0x8066,1);
  v.modern_paddle_move(*s);uint64_t id=v.paddle_hit_id_;
  s->poke(0x8061,uint8_t(42+direction*2));s->poke(0x808d,0);v.modern_paddle_move(*s);
  check(v.paddle_hit_id_==id+1,"moving side crossing emits exactly one contact");
  check(v.paddle_hit_.geometric && v.paddle_hit_.nx==direction,"moving side normal");
  check(s->peek(0x808d)==4,"native sound04 on first push");
  auto body=v.modern_body(*s);float ball=s->peek(0x8064);
  check(direction>0?ball>=body.x1+6:ball<=body.x0-6,"moving paddle cannot penetrate stationary ball");
  // Ball clock can be idle on the next pass: continued paddle pressure is the
  // same episode, not a second impact/audio event.
  s->poke(0x8061,uint8_t(42+direction*4));s->poke(0x808d,0);v.modern_paddle_move(*s);
  check(v.paddle_hit_id_==id+1 && s->peek(0x808d)==0,"sustained push deduplicated");
  s->poke(0x8064,uint8_t(direction>0?s->peek(0x8064)+10:s->peek(0x8064)-10));v.modern_paddle_move(*s);
  check(!v.modern_episode_,"separation ends episode");
  body=v.modern_body(*s);s->poke(0x8064,uint8_t(direction>0?body.x1+7:body.x0-7));
  s->poke(0x8061,uint8_t(42+direction*6));v.modern_paddle_move(*s);
  check(v.paddle_hit_id_==id+2,"later separated impact emits a new event");
  // A genuinely falling ball cannot be resurrected by subsequent paddle input.
  s->poke(0x807a,1);s->poke(0x8061,uint8_t(42+direction*8));v.modern_paddle_move(*s);
  check(v.paddle_hit_id_==id+2 && s->peek(0x807a)==1,"falling ball not resurrected");
  ++pushes;
 }
 printf("moving-paddle episodes %d failures%d\n",pushes,fail);
 CloseWindow();return fail || index!=cases.size();
}
