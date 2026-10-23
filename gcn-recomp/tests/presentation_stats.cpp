#include "presentation_stats.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
 PresentationStats stats;uint64_t now=0;
 require(!stats.frame(now,{}),"First frame only anchors the clock");
 for(unsigned i=0;i<119;++i){now+=16666667;require(!stats.frame(now,{}),"Summary emitted too early");}
 now+=16666667;auto smooth=stats.frame(now,{});
 require(smooth && std::abs(smooth->fps-60)<.001 && smooth->over25==0,"Smooth sixty-frame timing is wrong");
 for(unsigned i=0;i<120;++i){now+=(i<118?16000000:40000000);auto out=stats.frame(now,{0,0,i==118?12.:0});if(i==119){
  require(out && out->over25==2 && out->p95==16 && out->p99==40 && out->maximum==40,"Tail hitches hidden by the percentile summary");
  require(out->maximum_work.render==12 && out->maximum_work.wait==0,"Stage maxima changed");
 }}
 require(!stats.frame(now-1,{}),"A reset clock must discard the partial window");now-=1;
 for(unsigned i=0;i<120;++i){now+=20000000;auto out=stats.frame(now,{});if(i==119)require(out && out->fps==50 && out->maximum_work.render==0,"Window reset retained earlier samples");}
 puts("PASS: bounded presentation windows, sixty FPS, percentile hitches, stage maxima and reset");
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
