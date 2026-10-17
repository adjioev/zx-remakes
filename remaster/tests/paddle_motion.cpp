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
 // Calibrate visual delay on a known 100 units/s movement, then release.
 BBPaddleMotion calibration;
 for(int i=0;i<=20;++i)calibration.sample(float(i*2),12,i*.02,false);
 double lag_ms=(40-calibration.at(.4))/100*1000;
 if(std::abs(lag_ms-40)>.001)return 1;
 float last=calibration.at(.4);
 for(int i=21;i<=25;++i) {
  calibration.sample(40,12,i*.02,false);
  float shown=calibration.at(i*.02);
  if(shown<last-.001 || shown>40.001)return 1;
  if(i>=23 && std::abs(shown-40)>.001)return 1;
  last=shown;
 }
 std::printf("steady motion visual lag %.3f ms; release settled within 60 ms\n",lag_ms);
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Paddle motion regression");
 auto a=Spectrum::from_z80(d),b=Spectrum::from_z80(d);if(!a||!b)return 2;
 BallBreakerView baseline,preview;baseline.start(*a,"remaster/assets/ballbreaker");preview.start(*b,"remaster/assets/ballbreaker");
 bool even=argc>2;
 baseline.original_speed_=preview.original_speed_=!even;
 baseline.preview_enabled_=false;preview.preview_enabled_=true;
 baseline.unlimited_lives_=preview.unlimited_lives_=true;
 int frame=0,dir=0,failures=0,checks=0,last_direction=0,backsteps=0;
 double transition_step=0,baseline_transition_step=0,transition_delta=0,baseline_transition_delta=0,last_step=0,last_base_step=0;
 double all_step=0,all_step_t=0;
 float maximum_lag=0;double previous=0,previous_base=0;bool have=false;
 std::vector<double> speeds,old_speeds;
 int far_checks=0;
 for(int tick=1;tick<=1200;++tick) {
  double t=tick/60.;
  while((frame+1)/50.<=t+1e-8) {
   Controls c;
   if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
   if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
   int phase=(frame-500)%100;
   dir=frame<500?0:phase<35?1:phase<50?0:phase<85?-1:0;
   // Fast reversals as well as long holds, stops, and both rail endpoints.
   if(frame>=750&&frame<850)dir=(frame/8)%2?1:-1;
   c.dirs=dir>0?ZXC_JOY_RIGHT:dir<0?ZXC_JOY_LEFT:0;
   baseline.input(*a,c);a->run_frame();baseline.after_frame(*a);
   preview.input(*b,c);b->run_frame();preview.after_frame(*b);++frame;
   if(a->dump()!=b->dump() || a->cpu_ticks()!=b->cpu_ticks()) {
    std::fprintf(stderr,"FAIL original state changed at %d\n",frame);return 1;
   }
   if(!even && t>10 && preview.paddle_motion_.valid()) {
    float next=preview.paddle_motion_.at(preview.game_t_+.06);
    float raw=b->peek(0x8061)-b->peek(0x8063)/2;

    if(!dir && std::abs(next-raw)>.001)++failures;
    ++checks;
   }
  }
  BBState old=baseline.latest_,st=preview.latest_;
  baseline.smooth_apply(old,t);preview.smooth_apply(st,t);
  auto get=[](const BBState&s)->const BBObject*{for(const auto&o:s.objects)if(o.slot==1)return &o;return nullptr;};
  const auto* p=get(st);const auto* q=get(old);if(!p||!q)continue;
  if(have && !dir && last_direction && (p->x-previous)*last_direction < -.001) ++backsteps;
  if(dir)last_direction=dir;
  BBObject even_ball{},even_bat{};
  if(even && (!preview.contact_motion_.at(t-BB_RENDER_DELAY,even_ball,even_bat) || even_ball.z-5>=40)) {
   if(p->x!=q->x || p->sx!=q->sx || p->sy!=q->sy)++failures;
   for(const auto& ball:st.objects)if(ball.slot==0)
    for(const auto& before:old.objects)if(before.slot==0 &&
      (ball.x!=before.x || ball.z!=before.z || ball.sx!=before.sx || ball.sy!=before.sy))++failures;
   ++far_checks;
  }
  if(even && !preview.preview_.path_.empty())++failures;
  if(!even && preview.paddle_motion_.valid() &&
     (p->x<-.001 || p->x>96-p->bat_width+.001 ||
      !std::isfinite(p->x)))++failures;
  maximum_lag=std::max(maximum_lag,std::abs(p->x-(even ? bb_even_paddle_left(b->peek(0x8061),b->peek(0x8063)) : float(b->peek(0x8061)-b->peek(0x8063)/2))));
  BBObject confirmed_ball{},confirmed_bat{};
  if(!even && preview.paddle_motion_.valid() &&
     (!preview.contact_motion_.at(t-BB_RENDER_DELAY,confirmed_ball,confirmed_bat) || confirmed_ball.z-5>=40)) {
   if(std::abs(p->x-preview.paddle_motion_.at(t))>.001)++failures;
   ++far_checks;
  }
  if(have && dir && t>10 && t<15 && p->x>10 && p->x<75 && q->x>10 && q->x<75) {
   speeds.push_back((p->x-previous)*60*dir);old_speeds.push_back((q->x-previous_base)*60*dir);
  }
  double step=p->x-previous,base_step=q->x-previous_base;
  if(even && have && t>10 && std::abs(step)<12 && std::abs(step)>all_step) {all_step=std::abs(step);all_step_t=t;}
  if(even && have && preview.contact_motion_.at(t-BB_RENDER_DELAY,even_ball,even_bat) && even_ball.z-5>=8 && even_ball.z-5<=16) {
   transition_step=std::max(transition_step,std::abs(step));
   baseline_transition_step=std::max(baseline_transition_step,std::abs(base_step));
   transition_delta=std::max(transition_delta,std::abs(step-last_step));
   baseline_transition_delta=std::max(baseline_transition_delta,std::abs(base_step-last_base_step));
  }
  last_step=step;last_base_step=base_step;
  previous=p->x;previous_base=q->x;have=true;
 }
 auto sd=[](const auto&v){double mean=std::accumulate(v.begin(),v.end(),0.0)/v.size(),sq=0;for(double x:v)sq+=(x-mean)*(x-mean);return std::sqrt(sq/v.size());};
 if(backsteps) {std::fprintf(stderr,"FAIL %d backwards samples after release\n",backsteps);++failures;}
 // Near contact the paired confirmed trajectory deliberately replaces averaging.
 // Require the original averaging filter to remain exact away from contact.
 if(speeds.empty() || (!even && (!checks || sd(speeds)>=sd(old_speeds))))++failures;
 if(!far_checks)++failures;
 if(even && (transition_step>2.001 || all_step>2.001 || sd(speeds)>sd(old_speeds)*1.2)) {
  fprintf(stderr,"FAIL Even contact smoothing: max step %.3f, speed SD %.3f vs %.3f\n",transition_step,sd(speeds),sd(old_speeds));
  ++failures;
 }
 printf("unchanged filter away from contact: %d checks\n",far_checks);
 std::printf("%s: speed std %.3f -> %.3f; input checks %d; maximum lag %.3f units; failures %d\n",even?"even":"original",sd(old_speeds),sd(speeds),checks,maximum_lag,failures);
 if(even)printf("contact-window max step baseline %.3f paired %.3f; max step change baseline %.3f paired %.3f\n",baseline_transition_step,transition_step,baseline_transition_delta,transition_delta);
 if(even)printf("largest paddle step throughout play %.3f at %.5f\n",all_step,all_step_t);
 CloseWindow();return failures?1:0;
}
