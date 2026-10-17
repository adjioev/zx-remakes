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
 if(argc<3)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Miss presentation regression");
 auto a=Spectrum::from_z80(d),baseline=Spectrum::from_z80(d);if(!a||!baseline)return 2;
 BallBreakerView view,control;
 const bool even=argc>3 && std::string(argv[3])=="even";
 struct Check { uint64_t tick; double t; float x,z,left,width; };
 std::vector<Check> pending,candidates,missed;
 int kind=std::atoi(argv[2]),failures=0,checked=0,misses=0,clearance_checks=0;bool falling=false;
 a->on_exec(0x8a0a,[&](const zxc_regs_t&) {
  int z=a->peek(0x8065),face=a->peek(0x8062);
  if(int8_t(a->peek(0x8066))>=0 || z>=face || z<face-3 || a->peek(0x807a))return;
  float x=a->peek(0x8064),width=a->peek(0x8063),left=a->peek(0x8061)-a->peek(0x8063)/2;
  if(x+3>=std::max(0.f,left) && std::max(0.f,x-6)<left+width)return;
  pending.push_back({even?a->video_ticks():a->cpu_ticks(),0,x,float(z),left,width});
 });
 a->on_exec(0x8a73,[&](const zxc_regs_t&) {pending.clear();candidates.clear();});
 // Record entry coordinates before the adapter prepares its compensated add.
 view.start(*a,"remaster/assets/ballbreaker");control.start(*baseline,"remaster/assets/ballbreaker");
 view.original_speed_=control.original_speed_=!even;view.preview_enabled_=true;control.preview_enabled_=false;
 view.unlimited_lives_=control.unlimited_lives_=true;
 for(int frame=0;frame<3000;++frame) {
  Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
  if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
  if(frame>300) {
   int dx=int(a->peek(0x8064))+(std::abs(kind)==100?(kind>0?4:-4):kind)-int(a->peek(0x8061));
   int dir=dx < -2?-1:dx>2?1:0;
   if(std::abs(kind)==100 && a->peek(0x8065)<12)dir=kind>0?1:-1;
   if(kind==0 && frame>450 && a->peek(0x8065)<35)dir=(frame/3)%2?1:-1;
   c.dirs=dir<0?ZXC_JOY_LEFT:dir>0?ZXC_JOY_RIGHT:0;
  }
  view.input(*a,c);a->run_frame();view.after_frame(*a);
  control.input(*baseline,c);baseline->run_frame();control.after_frame(*baseline);
  if(a->dump()!=baseline->dump() || a->cpu_ticks()!=baseline->cpu_ticks())return 1;
  double t=(frame+1)/50.;
  for(auto p:pending) {p.t=t-double((even?a->video_ticks():a->cpu_ticks())-p.tick)/3546894.;candidates.push_back(p);}pending.clear();
  bool now=a->peek(0x807a)!=0;
  if(now&&!falling) {++misses;missed.insert(missed.end(),candidates.begin(),candidates.end());candidates.clear();}
  if(a->peek(0x809a) || a->peek(0x8083)>1)candidates.clear();
  falling=now;
  // Even deliberately retimes the confirmed pair between exact impacts. Check
  // the visible geometry densely across render times instead of requiring each
  // intermediate native microstep to retain its old burst timestamp.
  if(even && frame>300 && !a->peek(0x809a) && a->peek(0x8083)<=1) {
   if(!view.hd_) {view.set_remake(true);view.prepare(*a,false,t);}
   for(int sub=0;sub<8;++sub) {
    auto shown=view.latest_;view.smooth_apply(shown,t+sub*.0025);
    const BBObject *ball=nullptr,*bat=nullptr;
    for(const auto& o:shown.objects){if(o.slot==0)ball=&o;if(o.slot==1)bat=&o;}
    if(!ball||!bat)continue;
    float x=ball->x+5,z=ball->z-5,y=108-x/2-ball->sy;
    if(z>17 || z<0 || std::abs(y-4)>.01f)continue;
    auto body=view.hd_->bat_body(*bat);
    float distance=std::hypot(x-std::clamp(x,body.x0,body.x1),z-std::clamp(z,body.z0,body.z1));
    if(distance<BB_BALL_RADIUS-.01f) {
     if(failures<12)fprintf(stderr,"FAIL visible penetration at %.5f distance %.4f\n",t+sub*.0025,distance);
     ++failures;
    }
    ++clearance_checks;
   }
  }
  for(auto it=missed.begin();it!=missed.end();) {
   if(t+1e-8<it->t+BB_RENDER_DELAY) {++it;continue;}
   auto state=view.latest_;view.smooth_apply(state,it->t+BB_RENDER_DELAY);
   const BBObject *ball=nullptr,*bat=nullptr;
   for(const auto& o:state.objects) {if(o.slot==0)ball=&o;if(o.slot==1)bat=&o;}
   if(!ball||!bat) {++failures;it=missed.erase(it);continue;}
   if(!view.hd_) { view.set_remake(true); view.prepare(*a,false,t); }
   const auto body=view.hd_->bat_body(*bat);
   bool overlap=ball->x+5+BB_BALL_RADIUS>=body.x0-.001f && ball->x+5-BB_BALL_RADIUS<body.x1-.001f;
   bool paired=std::abs(ball->x+5-it->x)<.01 && std::abs(bat->x-it->left)<.01;
   if(even) {
    float x=ball->x+5,z=ball->z-5;
    overlap=std::hypot(x-std::clamp(x,body.x0,body.x1),z-std::clamp(z,body.z0,body.z1))<BB_BALL_RADIUS-.01f;
    // Projection must continue to describe the same physical sphere centre.
    paired=std::abs(ball->sx-(148+x-z))<.01f;
   }
   if(overlap || !paired) {
    ++failures;fprintf(stderr,"FAIL t %.5f raw ball %.1f pad %.1f shown ball %.3f pad %.3f overlap %d\n",it->t,it->x,it->left,ball->x+5,bat->x,overlap);
   }
   ++checked;it=missed.erase(it);
  }
 }
 printf("%s mode %d: %d final misses, %d rejected contact samples, %d presentation mismatches; RAM/ticks identical\n",even?"Even":"Original",kind,misses,checked,failures);
 if(even)printf("dense visible-clearance samples %d\n",clearance_checks);
 CloseWindow();return failures || !checked || (even && !clearance_checks);
}
