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
// Test-only paddle fixtures are applied before the original selects a serve.
// Exercise each position in both modes, across actual life-loss/new-ball cycles.
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Predictable Even serve");
 auto s=Spectrum::from_z80(d);if(!s)return 2;
 BallBreakerView view;view.start(*s,"remaster/assets/ballbreaker");view.preview_enabled_=false;view.unlimited_lives_=true;
 int serves=0,flights=0,failures=0,expected_dx=0;bool awaiting_flight=false;
 std::set<std::pair<bool,int>> coverage;
 s->on_exec(0x9c9f,[&](const zxc_regs_t&) {
  const bool original=(serves%2)!=0;
  if(view.original_speed_!=original) {
   auto before=s->dump();view.on_key(*s,KEY_O);
   if(before!=s->dump()){++failures;fprintf(stderr,"FAIL speed toggle mutates current ball\n");}
  }
  const int positions[]={30,42,60};int pad=positions[(serves/2)%3];
  s->poke(0x8061,uint8_t(pad));
  expected_dx=original?(pad==42?-1:0):-1;
  coverage.insert({original,pad});
 });
 s->on_exec(0x9cbb,[&](const zxc_regs_t&) {
  ++serves;awaiting_flight=true;
  int dx=int8_t(s->peek(0x8067)),dz=int8_t(s->peek(0x8066));
  if(dx!=expected_dx || dz!=-1 || s->peek(0x8064)!=42 || s->peek(0x8065)!=20) {
   ++failures;fprintf(stderr,"FAIL serve %d mode=%s pad=%d dx=%d expected=%d dz=%d\n",serves,view.original_speed_?"Original":"Even",s->peek(0x8061),dx,expected_dx,dz);
  }
 });
 s->on_exec(0x886b,[&](const zxc_regs_t&) {
  if(!awaiting_flight || s->peek(0x809a) || s->peek(0x8083))return;
  awaiting_flight=false;++flights;
  if(s->peek(0x8064)!=42+expected_dx || s->peek(0x8065)!=19) {
   ++failures;fprintf(stderr,"FAIL first physical step after drop\n");
  }
 });
 for(int frame=0;frame<3000 && flights<6;++frame) {
  Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
  if(frame>=75&&frame<85)c.ok=true,c.keys.insert(13);
  view.input(*s,c);s->run_frame();view.after_frame(*s);
 }
 if(coverage.size()!=6 || flights<6)++failures;
 printf("serves=%d flights=%d mode/position cases=%zu failures=%d\n",serves,flights,coverage.size(),failures);
 CloseWindow();return failures?1:0;
}
