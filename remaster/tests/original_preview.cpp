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
 const bool even=argc>2 && std::string(argv[2])=="even";
 baseline.original_speed_=preview.original_speed_=!even;baseline.preview_enabled_=false;preview.preview_enabled_=true;
 baseline.unlimited_lives_=preview.unlimited_lives_=true;
 int frame=0,failures=0,contacts=0,sounds=0,false_turns=0,active=0,penetrations=0,misses=0;
 float contact_x=0,contact_left=0;
 b->on_exec(0x8a73,[&](const zxc_regs_t&) {
  contact_x=b->peek(0x8064);contact_left=b->peek(0x8061)-b->peek(0x8063)/2;
 });
 bool falling=false;
 struct Contact { double t; float x,z,left; uint64_t id; };
 std::deque<Contact> checks;
 uint64_t hit=0;double lastx=0,lastz=0,lastsx=0;bool have=false;
 std::vector<double> speeds,baseline_speeds,costs;double last_base=0;
 const bool juke=argc>2 && std::string(argv[2])=="juke";
 const int level=argc>3?std::atoi(argv[3]):1, width=argc>4?std::atoi(argv[4]):0;
 double largest_reverse=0,last_direction_change=0;int previous_dz=0;
 int narrow_frames=0,slow_frames=0,natural_slow_frames=0;std::set<int> levels;

 for(int tick=1;tick<=1800;++tick) {
  double t=tick/60.0;
  while((frame+1)/50.0<=t+1e-8) {
   if(frame==350 && level>1) {baseline.jump_to(*a,level-1);preview.jump_to(*b,level-1);}
   // Identical synthetic fixtures isolate width and native slowdown from random drops.
   if(frame>550 && width) {a->poke(0x8063,uint8_t(width));b->poke(0x8063,uint8_t(width));}
   if(argc>3 && frame==700) {a->poke16(0x808e,180);b->poke16(0x808e,180);}
   if(argc>3 && frame==950) {baseline.on_key(*a,KEY_O);preview.on_key(*b,KEY_O);}
   if(argc>3 && frame==1050) {baseline.on_key(*a,KEY_O);preview.on_key(*b,KEY_O);}
   Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
   if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
   if(frame>300){int dx=int(a->peek(0x8064))-int(a->peek(0x8061));c.dirs=dx < -2?ZXC_JOY_LEFT:dx>2?ZXC_JOY_RIGHT:0;}
   if(juke&&frame>450&&a->peek(0x8065)<35)c.dirs=(frame/3)%2?ZXC_JOY_LEFT:ZXC_JOY_RIGHT;
   BBObject approach{};bool had_approach=preview.preview_.at((frame+1)/50.0,approach);
   baseline.input(*a,c);a->run_frame();baseline.after_frame(*a);
   auto begin=std::chrono::steady_clock::now();
   preview.input(*b,c);b->run_frame();preview.after_frame(*b);
   costs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
   if(a->dump()!=b->dump() || a->cpu_ticks()!=b->cpu_ticks()) {std::fprintf(stderr,"FAIL authoritative divergence at frame %d\n",frame);return 1;}
   std::vector<SfxEvent> events;preview.take_sfx(events);for(const auto&e:events)if(e.kind==Sfx::BatHit)++sounds;
   bool now_falling=b->peek(0x807a)!=0;
   if(now_falling && !falling) {
    ++misses;
    BBObject held{};
    if(preview.preview_.at(preview.game_t_,held)) {
     ++failures;std::fprintf(stderr,"FAIL forecast retained after miss\n");
    }
   }
   falling=now_falling;
   if(preview.paddle_hit_.id!=hit){
    hit=preview.paddle_hit_.id;
    if(hit){++contacts;BBObject ball{};
     if(preview.original_speed_ || even)checks.push_back({even ? preview.paddle_hit_.t : preview.game_t_,even ? preview.paddle_hit_.x : contact_x,
       even ? preview.paddle_hit_.z : float(b->peek(0x8062)),even ? preview.paddle_hit_.bat_left : contact_left,hit});
     if(!even && !juke && level==1 && !width && had_approach && approach.z-5 < b->peek(0x8062)-.001)++penetrations;
     BBObject bat{};
     if((preview.original_speed_ || even) && (!preview.contact_motion_.at(even ? preview.paddle_hit_.t : preview.game_t_,ball,bat) ||
        std::abs(bat.x-(even ? preview.paddle_hit_.bat_left : b->peek(0x8061)-b->peek(0x8063)/2))>.001 ||
        std::abs((ball.z-5)-(even ? preview.paddle_hit_.z : b->peek(0x8062)))>.001 || preview.delay_!=BB_RENDER_DELAY)) {
      ++failures;std::fprintf(stderr,"FAIL contact anchor at %.3f z %.3f\n",preview.game_t_,ball.z);
     }
    }
   }
   if(!checks.empty()) {
    const auto c=checks.front();
    preview.set_remake(true);preview.prepare(*b,false,preview.game_t_);
    if(preview.game_t_+1e-8<c.t+BB_RENDER_DELAY) {
     if(preview.sounded_paddle_hit_==c.id || preview.hd_->shown_paddle_hit_==c.id) {
      ++failures;fprintf(stderr,"FAIL early contact effect/sound\n");
     }
    } else {
     BBState shown=preview.latest_;preview.smooth_apply(shown,c.t+BB_RENDER_DELAY);
     for(const auto& o:shown.objects) {
      if(o.slot==0 && (std::abs(o.x+5-c.x)>.001 || std::abs(o.z-5-c.z)>.001)) {++failures;fprintf(stderr,"FAIL displayed ball at %.3f: %.3f %.3f expected %.3f %.3f\n",c.t,o.x+5,o.z-5,c.x,c.z);}
      if(o.slot==1 && std::abs(o.x-c.left)>.001) {++failures;fprintf(stderr,"FAIL displayed bat at %.3f: %.3f expected %.3f\n",c.t,o.x,c.left);}
     }
     if(preview.sounded_paddle_hit_!=c.id || preview.hd_->shown_paddle_hit_!=c.id) {
      ++failures;fprintf(stderr,"FAIL contact effect/sound missing at delayed anchor\n");
     }
     checks.pop_front();
    }
   }
   if(frame>550 && width && b->peek(0x8063)==width)++narrow_frames;
   if(b->peek16(0x808e)){++slow_frames;if(frame<700)++natural_slow_frames;}
   levels.insert(b->peek(0x8070)+1);
   int dz=int8_t(b->peek(0x8066));if(dz!=previous_dz)last_direction_change=t,previous_dz=dz;
   ++frame;
  }
  BBState base=baseline.latest_,state=preview.latest_;baseline.smooth_apply(base,t);preview.smooth_apply(state,t);
  auto get=[](const BBState&s)->const BBObject*{for(const auto&o:s.objects)if(o.slot==0)return &o;return nullptr;};
  auto ball=get(state);auto old=get(base);if(!ball||!old)continue;
  if(!std::isfinite(ball->sx)||!std::isfinite(ball->sy)){++failures;break;}
  if(have && t>10.4+1e-7 && t<=10.8+1e-7){speeds.push_back((ball->sx-lastsx)*60);baseline_speeds.push_back((old->sx-last_base)*60);}
  if(have && (preview.original_speed_ || even) && t-last_direction_change>.2 && t>9.5 && !b->peek(0x807a) && !b->peek(0x809a) && !b->peek(0x8083) && ball->z<61 && int8_t(b->peek(0x8066))<0 && ball->z-lastz>0.2 && std::abs(ball->x-lastx)<12 && t-preview.paddle_hit_.t>0.12) {++false_turns;largest_reverse=std::max(largest_reverse,double(ball->z-lastz));fprintf(stderr,"outward motion t=%.3f z=%.2f previous=%.2f falling=%d\n",t,ball->z,lastz,b->peek(0x807a));}
  if(!preview.preview_.path_.empty())++active;
  lastx=ball->x;lastz=ball->z;lastsx=ball->sx;last_base=old->sx;have=true;
 }
 auto sd=[](const auto&v){if(v.empty())return std::nan("");double mean=std::accumulate(v.begin(),v.end(),0.0)/v.size(),sq=0;for(double x:v)sq+=(x-mean)*(x-mean);return std::sqrt(sq/v.size());};
 std::sort(costs.begin(),costs.end());
 std::printf("%s: straight speed std baseline %.3f preview %.3f, range %.3f..%.3f; contacts %d sounds %d false turns %d active frames %d; sample p95 %.3f ms max %.3f ms\n",even?"even":juke?"juke":"follow",sd(baseline_speeds),sd(speeds),(speeds.empty()?std::nan(""):*std::min_element(speeds.begin(),speeds.end())),(speeds.empty()?std::nan(""):*std::max_element(speeds.begin(),speeds.end())),contacts,sounds,false_turns,active,costs[costs.size()*95/100],costs.back());
 std::printf("incoming penetrations %d; misses %d\n",penetrations,misses);
 if(penetrations || (juke && !misses) || contacts!=sounds||false_turns||(!even&&!juke&&level==1&&!width&&(speeds.empty()||sd(speeds)>40 || (speeds.empty()?std::nan(""):*std::min_element(speeds.begin(),speeds.end()))>-50)))++failures;
 std::printf("level %d observed %d; width %d fixture frames %d; slowdown frames %d; largest reverse %.3f; slowdown before fixture %d frames\n",level,int(levels.count(level)),width,narrow_frames,slow_frames,largest_reverse,natural_slow_frames);
 if(even && !preview.preview_.path_.empty())++failures;
 if(!levels.count(level) || (width && !narrow_frames) || (argc>3 && !slow_frames))++failures;
 CloseWindow();return failures?1:0;
}
