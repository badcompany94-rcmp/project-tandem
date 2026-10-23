#include "net/redundant_input.h"
#include <cstdio>
#include <memory>
#include <queue>
#include <random>
#include <tuple>
using namespace net::mesh;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static Packet input(uint64_t tick,unsigned slot){auto p=packet(3,tick,tick*1234567);p[32]=uint8_t(tick+slot*7);p[33]=uint8_t(tick/5+slot);p[24]=uint8_t(slot);p[25]=uint8_t(tick%17);return p;}
template<class F>static void rejects(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}require(failed,"Malformed/conflicting datagram accepted");}
static void run(unsigned count,uint64_t first,unsigned seed) {
 struct Event{uint64_t due,serial;unsigned from,to;std::vector<uint8_t> bytes;bool operator>(const Event& b)const{return std::tie(due,serial)>std::tie(b.due,b.serial);}};
 std::priority_queue<Event,std::vector<Event>,std::greater<Event>> queue;
 std::vector<std::unique_ptr<RedundantInput>> nodes;
 std::vector<std::vector<uint64_t>> received(count,std::vector<uint64_t>(count,first-1));
 std::vector<uint64_t> sent(count,first-1);std::mt19937 random(seed);uint64_t now=0,serial=0,dropped=0,duplicates=0;
 for(unsigned slot=0;slot<count;++slot)nodes.push_back(std::make_unique<RedundantInput>(count,slot,first,
  [&,slot](unsigned target,const auto& bytes){
   require(bytes.size()<=306,"Datagram exceeds bounded MTU");
   if(random()%5==0 || (now%1700>=1000 && now%1700<1120)){++dropped;return;}
   uint64_t delay=35+random()%71;
   queue.push({now+delay,serial++,slot,target,bytes});
   if(random()%5==0){queue.push({now+delay+random()%60,serial++,slot,target,bytes});++duplicates;}
  },[&,slot](unsigned from,const auto& bytes){
   require(bytes.size()==65 && bytes[0]==12,"Bad ordered output framing");Packet p;std::copy(bytes.begin()+1,bytes.end(),p.begin());
   require(read(p,8)==++received[slot][from] && p==input(read(p,8),from),"Lost, changed or reordered delivered input");
  }));
 for(now=0;now<20000;++now) {
  while(!queue.empty() && queue.top().due<=now){auto e=queue.top();queue.pop();nodes[e.to]->peer(e.from,e.bytes);}
  for(unsigned slot=0;slot<count;++slot) {
   if(now%16==0 && sent[slot]<first+499 && nodes[slot]->retained()<20)nodes[slot]->native(input(++sent[slot],slot));
   nodes[slot]->pump(now);
  }
 }
 for(unsigned to=0;to<count;++to){require(sent[to]==first+499,"Producer failed to finish");require(nodes[to]->retained()==0,"Final input/ACK failed to drain without new samples");for(unsigned from=0;from<count;++from)if(to!=from)require(received[to][from]==first+499,"Missing final delivered input");}
 require(dropped>100 && duplicates>100,"Impairment was not exercised");
 printf("PASS: %u peers, 500 exact inputs each, 20%% loss + burst loss + reorder + duplicates (seed %u)\n",count,seed);
}
int main(){try {
 for(unsigned peers:{2u,5u,8u})for(unsigned seed:{17u,2049u})run(peers,seed==17?1:13,seed);
 std::vector<std::vector<uint8_t>> output;
 RedundantInput sender(2,0,1,[&](unsigned,const auto& b){output.push_back(b);},[](unsigned,const auto&){});
 sender.native(input(1,0));sender.pump(0);auto original=output.back();
 auto make_receiver=[](){return std::make_unique<RedundantInput>(2,1,1,[](unsigned,const auto&){},[](unsigned,const auto&){});};
 for(unsigned cut:{0u,1u,17u,27u})rejects([&]{auto r=make_receiver();r->peer(0,{original.begin(),original.begin()+cut});});
 rejects([&]{auto r=make_receiver();auto b=original;b[1]=25;r->peer(0,b);});
 rejects([&]{auto r=make_receiver();auto b=original;b[9]=1;r->peer(0,b);}); // acknowledges unsent input
 rejects([&]{auto r=make_receiver();r->peer(1,original);});
 rejects([&]{auto r=make_receiver();r->peer(0,original);auto b=original;b[27]^=1;r->peer(0,b);});
 rejects([&]{auto r=make_receiver();auto b=original;b[28]=17;r->peer(0,b);});
 rejects([&]{auto r=make_receiver();auto b=original;b[29]=17;r->peer(0,b);});
 rejects([&]{auto r=make_receiver();r->peer(0,original);auto b=original;b[28]^=1;r->peer(0,b);});
 auto r=make_receiver();r->peer(0,original);r->peer(0,original); // exact duplication is idempotent
 // A stalled producer must still recover its final sample and a lost final
 // acknowledgment. The source does not resume producing any new input.
 std::vector<std::vector<uint8_t>> a,b;
 unsigned delivered=0;
 RedundantInput left(2,0,1,[&](unsigned,const auto& p){a.push_back(p);},[](unsigned,const auto&){});
 RedundantInput right(2,1,1,[&](unsigned,const auto& p){b.push_back(p);},[&](unsigned,const auto&){++delivered;});
 left.native(input(1,0));left.pump(0);require(!a.empty(),"Fresh input must be sent in its first pump");a.clear(); // lose final data
 left.pump(15);require(a.empty(),"Retry timer must not run ahead of its deadline");
 left.pump(16);require(a.size()==1,"Final data must retry without new input");right.peer(0,a.back());a.clear();right.pump(16);
 require(delivered==1 && b.size()==1,"Recovered input must produce its ACK");b.clear(); // lose final ACK
 left.pump(32);require(a.size()==1,"Lost ACK must keep input retrying");right.peer(0,a.back());a.clear();right.pump(32);
 require(delivered==1 && b.size()==1,"Duplicate must repeat ACK without duplicate delivery");left.peer(1,b.back());b.clear();
 require(!left.retained(),"A repeated final ACK must retire the stopped producer's history");
 puts("PASS: bounds, identity, ACK validation, immutable duplicates and tail retransmission");
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
