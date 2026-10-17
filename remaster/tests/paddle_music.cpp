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


// Real Z80 replay: paddle movement must execute its native LD HL,8061,
// preserve the tune data and retain the native AY notes. The reference disables
// only the movement hook; both views retain the same Even collision policy.
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>d((std::istreambuf_iterator<char>(f)),{});
 SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(320,180,"Paddle music regression");
 auto a=Spectrum::from_z80(d),b=Spectrum::from_z80(d);if(!a||!b)return 2;
 BBGame native=BALL_BREAKER;native.bat_move=0;
 BallBreakerView v,reference(native);v.start(*a,"remaster/assets/ballbreaker");reference.start(*b,"remaster/assets/ballbreaker");
 v.set_remake(true);reference.set_remake(true);v.unlimited_lives_=reference.unlimited_lives_=true;
 int moves=0,loaded=0,bad_writes=0,mismatches=0,period_mismatches=0,changed=0;
 a->on_exec(0x89d4,[&](const zxc_regs_t&){++moves;});
 a->on_exec(0x89d7,[&](const zxc_regs_t&r){++loaded;if(r.hl!=0x8061)++bad_writes;});
 for(int pc:{0x89e4,0x89e5,0x89f2,0x89f3})a->on_exec(pc,[&](const zxc_regs_t&r){if(r.hl!=0x8061)++bad_writes;});
 std::vector<uint8_t> tune;
 for(int frame=0;frame<1500;++frame){
  Controls c;if(frame>=10&&frame<20)c.ok=c.fire=true,c.keys.insert(' ');
  if(frame>=50&&frame<60)c.ok=true,c.keys.insert(13);
  if((frame>=400&&frame<450)||(frame>=900&&frame<950))c.dirs=ZXC_JOY_RIGHT;
  if(frame>=600&&frame<650)c.dirs=ZXC_JOY_LEFT;
  v.input(*a,c);reference.input(*b,c);a->run_frame();b->run_frame();v.after_frame(*a);reference.after_frame(*b);
  if(frame==350)for(int addr=0x5e96;addr<0x6300;++addr)tune.push_back(a->peek(addr));
  uint8_t ay[16],expected[16];a->ay_regs(ay);b->ay_regs(expected);
  bool different=false,period=false;for(int i=0;i<14;++i)if(ay[i]!=expected[i]){different=true;if(i<6)period=true;}
  mismatches+=different;period_mismatches+=period;
 }
 for(int addr=0x5e96;addr<0x6300;++addr)changed+=a->peek(addr)!=tune[addr-0x5e96];
 printf("moves %d native address loads %d wrong writes %d tune bytes changed %d AY frames differing %d pitch frames differing %d /1500\n",moves,loaded,bad_writes,changed,mismatches,period_mismatches);
 // A different CPU workload may place a sample one frame across an AY update;
 // persistent detuning or repeated changes must fail, not merely produce audio.
 bool fail=moves<100 || loaded!=moves || bad_writes || changed || mismatches>4 || period_mismatches>2;
 CloseWindow();return fail;
}
