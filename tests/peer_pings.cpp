#include "eos_ping.h"
#include <cstdio>
#include <cstdlib>
void require(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
    mkdd::eos::PeerPings a,b;a.peers({"b"});b.peers({"a"});
    require(a.summary(0).rtt_ms<0,"unknown RTT before reply");
    auto probe=a.probe("b",100),reply=b.received("a",probe,110);
    a.received("b",reply,120);
    require(a.summary(120).rtt_ms==20 && a.summary(120).jitter_ms==0,"first sample has no invented jitter");
    probe=a.probe("b",700);reply=b.received("a",probe,710);a.received("b",reply,760);
    require(a.summary(760).rtt_ms==30 && a.summary(760).jitter_ms==10,"jitter measures RTT variation separately from smoothing");
    a.received("b",reply,800);require(a.summary(800).jitter_ms==10,"duplicate replies cannot invent jitter");
    require(a.summary(4000).rtt_ms<0 && a.summary(4000).jitter_ms<0,"stale readings expire together");
    a.peers({});require(a.summary(5000).peers==0,"leaving clears old peer readings");
    puts("PASS: RTT, jitter, stale samples and peer cleanup");
}
