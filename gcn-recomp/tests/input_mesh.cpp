#include "net/input_mesh.h"
#include <cstdio>
#include <memory>
#include <queue>
#include <random>
#include <tuple>
using namespace net::mesh;
static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static Packet hello(unsigned slot,unsigned count=0,unsigned delay=3) {
    auto p=packet(1,0,0x89112233);p[32]=uint8_t(slot);p[33]=uint8_t(count);p[34]=uint8_t(delay);p[35]=8;p[36]=1;return p;
}
static Packet input(uint64_t tick,unsigned slot) {
    auto p=packet(3,tick,tick*0x1234567);p[32]=uint8_t(tick+slot*7);p[33]=uint8_t((tick/5)^slot);
    p[24]=uint8_t(slot);p[25]=uint8_t((tick+slot)%17);return p;
}
static void group(unsigned count,bool impaired) {
    std::vector<std::unique_ptr<InputMesh>> peers;
    for(unsigned slot=0;slot<count;++slot)peers.push_back(std::make_unique<InputMesh>(count,slot,net::Admission{}));
    struct Event {uint64_t at,serial;unsigned from,to;std::vector<uint8_t> bytes;bool operator>(const Event& b)const{return std::tie(at,serial)>std::tie(b.at,b.serial);}};
    std::priority_queue<Event,std::vector<Event>,std::greater<Event>> events;
    std::vector<uint64_t> last(count*count),confirmed(count,3),started(count),sent(count,3);
    uint64_t serial=0,largest_age=0;std::mt19937 random(1234);unsigned dropped=0;
    for(uint64_t now=0;now<12000;++now) {
        // Peers may see an authenticated hello before their native process opens.
        for(unsigned slot=0;slot<count;++slot)if(now==slot*7)peers[slot]->native(hello(slot));
        if(now%100==0)for(auto& peer:peers)peer->retry();
        while(!events.empty() && events.top().at<=now) {
            auto event=events.top();events.pop();peers[event.to]->peer(event.from,event.bytes);
        }
        for(unsigned slot=0;slot<count;++slot) {
            for(const auto& p:peers[slot]->output()) {
                if(valid(p,2)){require(!started[slot],"duplicate welcome");started[slot]=now+1;require(p[32]==slot && p[33]==count && p[36]==1,"bad welcome");}
                else {
                    require(valid(p,4) && read(p,8)==confirmed[slot]+1,"unordered canonical bundle");
                    const uint64_t tick=++confirmed[slot];require(read(p,16)==read(input(tick,0),16),"bad checksum");
                    for(unsigned contributor=0;contributor<count;++contributor) {
                        auto expected=input(tick,contributor);require(p[32+2*contributor]==expected[32] && p[33+2*contributor]==expected[33],"bundle differs from independent inputs");
                    }
                    unsigned minimum=16;for(unsigned contributor=0;contributor<count;++contributor)minimum=std::min<unsigned>(minimum,input(tick,contributor)[25]);
                    require(p[24]==count-1 && p[25]==minimum,"shared pacing maximum/minimum differs across peers");
                    largest_age=std::max(largest_age,now-(500+(tick-4)*16));
                }
            }
            if(now>=500 && now<=500+511*16 && (now-500)%16==0) {
                require(started[slot],"mesh handshake stalled");peers[slot]->native(input(++sent[slot],slot));
            }
            for(auto& message:peers[slot]->outgoing()) {
                if(message.bytes[0]==10) {
                    require(std::all_of(message.bytes.begin()+41,message.bytes.begin()+57,[](auto b){return !b;}),"admission leaked over network");
                    if(impaired && dropped<3){++dropped;continue;}
                }
                uint64_t delay=25;
                if(impaired) {
                    delay+=random()%11;
                    if(message.bytes[0]==12) {
                        Packet p;std::copy(message.bytes.begin()+1,message.bytes.end(),p.begin());
                        if(read(p,8)%127==0)delay+=65; // reliable loss recovery holds subsequent traffic
                    }
                }
                auto& due=last[slot*count+message.peer];due=std::max(due,now+delay);
                events.push({due,serial++,slot,message.peer,std::move(message.bytes)});
            }
        }
    }
    for(auto value:confirmed)require(value==515,"mesh did not confirm every input");
    require(largest_age<=(impaired?101:26),"mesh adds a host round trip");
    printf("PASS: %u direct peers, 512 exact bundles each, %s, maximum input age %llu ms\n",count,impaired?"jitter/reliable recovery/hello retries":"25 ms one-way links",(unsigned long long)largest_age);
}
template<class Fn> static void rejected(Fn fn) {
    bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}require(failed,"invalid mesh state accepted");
}
int main() {
    try {
        for(unsigned count:{2u,4u,8u}){group(count,false);group(count,true);}
        net::Admission ticket{};ticket[0]=0x88;
        rejected([&]{InputMesh peer(2,0,ticket);peer.native(hello(0));});
        rejected([]{InputMesh peer(2,0,{});auto p=hello(1,2);p[40]=1;std::vector<uint8_t> data{10};data.insert(data.end(),p.begin(),p.end());peer.peer(1,data);});
        auto ready=[] {
            auto peer=std::make_unique<InputMesh>(2,0,net::Admission{});peer->native(hello(0));
            auto p=hello(1,2);std::vector<uint8_t> data{10};data.insert(data.end(),p.begin(),p.end());peer->peer(1,data);peer->peer(1,{11});peer->outgoing();peer->output();return peer;
        };
        rejected([&]{auto peer=ready();peer->native(input(5,0));});
        rejected([&]{auto peer=ready();peer->native(input(4,0));peer->native(input(4,0));});
        rejected([&]{auto peer=ready();for(unsigned tick=4;tick<=68;++tick){peer->native(input(tick,0));peer->outgoing();}});
        rejected([&]{auto peer=ready();peer->native(input(4,0));auto p=input(4,1);p[16]^=1;std::vector<uint8_t> data{12};data.insert(data.end(),p.begin(),p.end());peer->peer(1,data);});
        rejected([&]{auto peer=ready();peer->peer(0,{11});});
        rejected([&]{auto peer=ready();auto p=input(4,0);p[24]=17;peer->native(p);});
        rejected([&]{auto peer=ready();auto p=input(4,0);p[25]=17;peer->native(p);});
        rejected([&]{auto peer=ready();auto p=input(4,0);p[26]=1;peer->native(p);});
        puts("PASS: mesh admission/privacy, sequence, bounds, identity and checksum rejection");
    }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
