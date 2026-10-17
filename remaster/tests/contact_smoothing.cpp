// Pure paired-path regression: no original snapshot or graphical window.
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

static BBContactMotion::Point point(double t,float x,float z,float left,bool anchor=false) {
 BBObject ball{};ball.slot=0;ball.x=x-5;ball.z=z+5;ball.sx=148+x-z;ball.sy=104-x/2;
 BBObject bat{};bat.slot=1;bat.x=left;bat.z=7;bat.bat_width=12;bat.bat_inset=3;bat.sx=160+left-7;bat.sy=100-left/2;
 return {t,ball,bat,anchor};
}
int main() {
 int failures=0;auto check=[&](bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL %s\n",message);++failures;}};
 for(int rate:{60,120,144}) {
  BBContactMotion m;m.paced_=true;m.points_.push_back(point(0,80,16,20));m.smooth(0);
  int frame=0;float previous=20,stop=70;double last=0;bool have=false;
  std::vector<double> smooth_speeds,raw_speeds;float raw_previous=20;
  for(int tick=1;tick<=rate*2;++tick) {
   double now=double(tick)/rate;
   while((frame+1)/50.<=now+1e-8) {
    ++frame;double t=frame/50.;
    BBObject before{},b{};bool ready=m.at(t-.04,before,b);float held=b.x;
    float left=20+std::min(50.f,2.f*std::floor(frame/2.f));
    m.points_.push_back(point(t,80+left-20,16,left));m.smooth(t);
    m.at(t-.04,before,b);if(ready)check(std::abs(b.x-held)<.0001,"frontier continuity");
   }
   BBObject ball{},bat{},rawball{},rawbat{};if(!m.at(now-.04,ball,bat))m.at_confirmed(now-.04,ball,bat);m.at_confirmed(now-.04,rawball,rawbat);
   check(std::abs((ball.x+5-bat.x)-60)<.001,"joint phase preserved");
   check(bat.x<=stop+.001 && (!have || bat.x>=previous-.001),"no overshoot/backstep at stop");
   if(now>1.06)check(std::abs(bat.x-stop)<.001,"settles within60ms");
   if(have && now>.2 && now<.7){smooth_speeds.push_back((bat.x-previous)*rate);raw_speeds.push_back((rawbat.x-raw_previous)*rate);}
   previous=bat.x;raw_previous=rawbat.x;last=now;have=true;
  }
  auto sd=[](auto& v){double mean=std::accumulate(v.begin(),v.end(),0.)/v.size(),sq=0;for(double x:v)sq+=(x-mean)*(x-mean);return std::sqrt(sq/v.size());};
  check(sd(smooth_speeds)<sd(raw_speeds)*.7,"smooth alternating25Hz/50Hz endpoints");
  printf("%dHz speed deviation %.3f -> %.3f\n",rate,sd(raw_speeds),sd(smooth_speeds));
 }
 {
  BBContactMotion m;m.paced_=true;m.points_={point(0,47,17,40),point(.03,47,13,40,true),point(.04,47,14,40)};m.smooth(.04);
  BBObject ball{},bat{};m.at(.03,ball,bat);check(std::abs(ball.z-18)<.001 && std::abs(ball.x-42)<.001 && bat.x==40,"exact contact anchor");
 }
 {
  BBContactMotion m;m.paced_=true;m.points_={point(0,36,7,40),point(.02,37,13,40),point(.04,44,14,40)};m.smooth(.04);
  check(!BBContactMotion::safe(m.points_.front(),m.points_.back()),"unsafe rounded-corner chord refused");
  check(m.presented_.size()>=3,"corner turn preserved");
  for(int i=0;i<=400;++i) {
   BBObject ball{},bat{};m.at(i*.0001,ball,bat);auto body=bb_visible_paddle_body(bat.x,bat.bat_width,bat.z,bat.bat_inset);
   float x=ball.x+5,z=ball.z-5;float distance=std::hypot(x-std::clamp(x,body.x0,body.x1),z-std::clamp(z,body.z0,body.z1));
   check(distance>=BB_BALL_RADIUS-.001,"no corner tunneling");
  }
 }

 {
  BBContactMotion m;m.paced_=true;
  m.points_={point(0,80,30,40),point(.01,79,30,44),point(.02,78,30,44),point(.03,77,30,44),point(.04,76,30,40)};
  m.smooth(.04);BBObject ball{},bat{};m.at(.01,ball,bat);
  check(std::abs(bat.x-44)<.001,"paddle reversal plateau keeps extremum while ball moves");
 }
 {
  BBContactMotion m;m.paced_=true;m.points_={point(.04,84,16,44)};
  auto seed=point(0,80,16,40);m.smooth(.04,&seed.ball,&seed.bat);
  BBObject ball{},bat{};check(m.at(0,ball,bat) && std::abs(bat.x-40)<.001,"bootstrap matches previous baseline frontier");
  m.at(.02,ball,bat);check(std::abs(bat.x-42)<.001,"bootstrap uses confirmed future without hard phase switch");
 }

 {
  auto a=point(0,47,2,40),b=point(.04,47,2,44);
  a.ball.sy+=30;b.ball.sy+=34;
  check(BBContactMotion::safe(a,b),"falling sphere below paddle is clear in3D despiteXZoverlap");
  a.ball.sy-=30;b.ball.sy-=34;
  check(!BBContactMotion::safe(a,b),"sameXZpath atfloorheight intersects solid paddle");
 }

 {
  std::mt19937 random(9140);std::uniform_real_distribution<float> x(25,75),z(-12,30),height(-30,30),move(-8,8);
  for(int trial=0;trial<500;++trial) {
   auto a=point(0,x(random),z(random),40),b=point(.04,x(random),z(random),40+move(random));
   a.ball.sy=108-(a.ball.x+5)/2-height(random);b.ball.sy=108-(b.ball.x+5)/2-height(random);
   double minimum=1000;
   for(int i=0;i<=2000;++i) {
    double k=i/2000.;double px=(a.ball.x+5)*(1-k)+(b.ball.x+5)*k;
    double py=(108-(a.ball.x+5)/2-a.ball.sy)*(1-k)+(108-(b.ball.x+5)/2-b.ball.sy)*k;
    double pz=(a.ball.z-5)*(1-k)+(b.ball.z-5)*k,left=a.bat.x*(1-k)+b.bat.x*k;
    double dx=px-std::clamp(px,left+3,left+12),dy=py-std::clamp(py,0.,12.),dz=pz-std::clamp(pz,0.,7.);
    minimum=std::min(minimum,std::sqrt(dx*dx+dy*dy+dz*dz));
   }
   bool safe=BBContactMotion::safe(a,b);
   if(minimum<5.99)check(!safe,"3Dchord guard rejects independentlysampled penetration");
   if(minimum>6.01)check(safe,"3Dchord guard accepts independentlysampled clear path");
  }
 }

 {
  BBContactMotion m;m.paced_=true;m.points_={point(0,80,16,40),point(.04,84,16,44)};
  auto curve=[](double t) {
   auto p=point(t,80,16,40).bat;p.x=40+std::min(4.,200*t);
   p.sx=160+p.x-p.z;p.sy=100-p.x/2;return p;
  };
  m.smooth(.04,nullptr,nullptr,curve,{.02});BBObject ball{},bat{};
  m.at(.02,ball,bat);check(std::abs(bat.x-44)<.001,"native paddle velocity knot preserved");
  m.at(.03,ball,bat);check(std::abs(bat.x-44)<.001,"paddle stop does not acquire phase lag");
 }
 printf("paired smoothing failures %d\n",failures);return failures?1:0;
}
