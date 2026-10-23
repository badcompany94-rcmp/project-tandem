#include "adaptive_input_delay.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using mkdd::online::AdaptiveInputDelay;
void require(bool yes,const char* why){if(!yes){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
 AdaptiveInputDelay p;require(p.delay()==1,"start at one frame");
 for(int i=0;i<59;++i)p.observe(20,5,true);require(p.delay()==1,"collect full window before adapting");
 auto result=p.observe(20,5,true);require(result.changed && result.delay==2 && result.fps==50,"network slowdown raises delay");
 for(int i=0;i<60*30;++i)p.observe(20,5,true);require(p.delay()==16,"bounded poor-link fallback");
 for(int i=0;i<60*2;++i)p.observe(1000.0/60,0,true);require(p.delay()==16,"no early decrease");
 for(int i=0;i<60;++i)p.observe(1000.0/60,0,false);require(p.delay()==16,"changed controls defer decrease");
 for(int i=0;i<60;++i)p.observe(1000.0/60,0,true);require(p.delay()==15,"stable healthy play decreases one frame");
 for(int i=0;i<60*3*16;++i)p.observe(1000.0/60,0,true);require(p.delay()==0,"can recover immediate input");
 for(int i=0;i<60*5;++i)p.observe(25,0,true);require(p.delay()==0,"rendering-only slowdown does not add latency");
 for(int i=0;i<60;++i)p.observe(i%10==0?23:16,1,true);require(p.delay()==1,"small frequent spikes trigger at average sixty");
 p.observe(std::numeric_limits<double>::quiet_NaN(),0,true);p.observe(1000,0,true);
 require(p.delay()==1,"suspended host window does not change delay");
 AdaptiveInputDelay prepared;
 for(int i=0;i<15;++i)prepared.received(90);
 require(prepared.prepare()==1,"sparse delivery samples do not guess startup delay");
 prepared.received(90);require(prepared.prepare()==7,"delivery primes during countdown with one frame of headroom");
 for(int i=0;i<64;++i)prepared.received(25);
 require(prepared.prepare()==3,"close connection uses small delivery budget");
 // A minority of slower packets matters even with a healthy median.
 for(int i=0;i<64;++i)prepared.received(i%8==0?150:25);
 require(prepared.prepare()==10,"tail jitter is covered before racing");
 for(int i=0;i<64;++i)prepared.received(220);
 require(prepared.prepare()==15,"poor delivery can use the additional fallback headroom");
 for(int i=0;i<64;++i)prepared.received(300);
 prepared.received(1001);prepared.received(-1);prepared.received(std::numeric_limits<double>::infinity());
 require(prepared.prepare()==16,"delivery priming obeys ceiling and rejects bad samples");
 AdaptiveInputDelay spikes;
 for(int i=0;i<60;++i)result=spikes.observe(i%30==0?26:16.34,i%30==0?5:0,true);
 require(result.changed && result.delay==2 && result.network_ms<.75 && result.fps>59.5,"repeated costly corrections matter even at sixty FPS and low mean work");
 AdaptiveInputDelay graphics;
 for(int i=0;i<60*4;++i)graphics.observe(i%30==0?26:16.34,0,true);
 require(graphics.delay()<=1,"graphics spikes alone do not raise buffer");
 AdaptiveInputDelay isolated;
 for(int i=0;i<60;++i)isolated.observe(i==30?30:16.34,i==30?5:0,true);
 require(isolated.delay()==1,"one isolated correction does not permanently increase delay");
 AdaptiveInputDelay outage;
 for(int i=0;i<59;++i)outage.observe(1000.0/60,0,true);
 result=outage.observe(350,340,true);require(result.changed && result.delay==2,"real long network stall is not discarded as suspension");
 // Same real 50 ms delivery, but one peer has queued fifteen more frames.
 // Its unadjusted bundle age is 300 ms. Increasing delay must not manufacture
 // a worse connection estimate; the previous policy immediately chose sixteen.
 AdaptiveInputDelay asymmetric,equal;
 for(int i=0;i<64;++i){asymmetric.received(300,16,1);equal.received(50,4,4);}
 require(asymmetric.prepare()==4 && equal.prepare()==4,"queue imbalance is removed from measured delivery age");
 for(unsigned lead=1;lead<=16;++lead) {
   AdaptiveInputDelay shifted;
   for(int i=0;i<64;++i)shifted.received(50+(lead-1)*1000.0/60,lead,1);
   require(shifted.prepare()==4,"increasing local queue must not increase its own network estimate");
 }
 AdaptiveInputDelay countdown;
 require(countdown.prepare()==1,"countdown begins without fabricated samples");
 for(int i=0;i<64;++i)countdown.received(50);
 require(countdown.prepare()==4,"countdown estimate refreshes after real delivery arrives");
 // Render hitches must not trap an excessive network buffer once deliveries
 // recover. Ordinary checkpoint capture is excluded at the caller.
 for(int i=0;i<64;++i)prepared.received(25);
 for(int i=0;i<60*3;++i)prepared.observe(25,.1,true);
 require(prepared.delay()==15,"quiet network can recover even on a graphics-limited window");
 for(int i=0;i<60*3*20;++i)prepared.observe(1000.0/60,.1,true);
 require(prepared.delay()==3,"recovery respects observed tail delivery floor");
 const unsigned settled=prepared.delay();
 for(int i=0;i<64;++i){prepared.received(-1);prepared.received(std::numeric_limits<double>::quiet_NaN());prepared.received(50,17,0);}
 require(prepared.prepare()==settled,"invalid adjusted samples cannot erase measured latency");
 puts("PASS: queue-corrected delivery, countdown warmup, bounded pressure response, safe recovery and graphics isolation");
}
