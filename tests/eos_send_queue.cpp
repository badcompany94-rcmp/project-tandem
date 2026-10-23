#include "eos_send_queue.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <array>
#include <random>
using Clock=std::chrono::steady_clock;
struct Packet {std::string peer;std::vector<unsigned char> data;Clock::time_point queued;};
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main() {
    using namespace std::chrono_literals;
    const auto now=Clock::now();std::deque<Packet> queue;size_t bytes=0;
    auto push=[&](const char* peer,unsigned char value,Clock::time_point at){queue.push_back({peer,{value},at});++bytes;};
    auto authorized=[](const Packet& p){return p.peer!="left";};
    std::vector<std::string> attempts,delivered;
    auto blocked=[&](const Packet& p){attempts.push_back(p.peer+std::to_string(p.data[0]));
        if(p.peer=="A")return false;delivered.push_back(attempts.back());return true;};
    push("A",1,now);push("B",1,now);push("A",2,now);push("C",1,now);push("B",2,now);
    auto r=mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,blocked);
    require(r.blocked==1 && r.sent==3 && bytes==2,"blocked peer stalled other destinations");
    require(attempts==std::vector<std::string>{"A1","B1","C1","B2"},"attempt order changed");
    require(queue.size()==2 && queue[0].data[0]==1 && queue[1].data[0]==2,"failed packets reordered");
    auto send=[&](const Packet& p){delivered.push_back(p.peer+std::to_string(p.data[0]));return true;};
    mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,send,1);
    require(bytes==1 && queue.front().data[0]==2,"budget reordered remainder");
    mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,send,1);
    require(bytes==0 && delivered==std::vector<std::string>{"B1","C1","B2","A1","A2"},"resume order changed");
    // More deferred packets than the send-attempt budget cannot starve B.
    for(unsigned i=0;i<255;++i)push("A",static_cast<unsigned char>(i),now);
    push("B",3,now);
    r=mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,blocked,2);
    require(r.sent==1 && r.blocked==1 && bytes==255,"deferred backlog consumed attempt budget");
    queue.clear();bytes=0;
    push("A",1,now);push("left",1,now-60s);push("B",1,now);
    r=mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,blocked,1);
    require(r.discarded==1 && bytes==2 && queue.back().peer=="B","unauthorized packet retained");
    push("A",2,now-31s);
    bool expired=false;try{mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,blocked,0);}
    catch(const std::runtime_error&){expired=true;}
    require(expired,"budget hid expired packet");
    // Interleave eight destinations under varying backpressure and budgets.
    queue.clear();bytes=0;
    std::array<unsigned,8> sent{},next{};std::mt19937 random(0x71a2);
    unsigned tick=0;
    auto check_order=[&](const Packet& p) {
        unsigned peer=unsigned(p.peer[0]-'A');
        require(p.data.size()==1 && p.data[0]==(next[peer]&255),"randomized peer sequence reordered");
        ++next[peer];return true;
    };
    for(;tick<512;++tick) {
        if(tick%3==0)for(unsigned peer=0;peer<8;++peer) {
            char name[]={char('A'+peer),0};push(name,static_cast<unsigned char>(sent[peer]++),now);
        }
        auto send_sometimes=[&](const Packet& p){return random()%4==0?false:check_order(p);};
        mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,send_sometimes,1+random()%7);
        require(bytes==queue.size(),"randomized byte accounting differs");
    }
    while(!queue.empty())mkdd::eos::drain_send_queue(queue,bytes,now,30s,authorized,check_order,3);
    require(sent==next && bytes==0,"randomized delivery lost packets");
    puts("PASS: independent destinations, per-peer order, retry, budget, authorization, expiry and 1368 interleaved deliveries");
}
