#include "net/rollback_timeline.h"
#include <cstring>
#include <cstdio>
#include <random>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct World {
    std::array<uint64_t,8> positions{};
    uint64_t random=9,frame=0;
    unsigned live_presentations=0,replay_presentations=0;
    std::vector<net::Inputs> visible_inputs;
    net::RollbackTimeline::State save()const {
        net::RollbackTimeline::State bytes(sizeof(positions)+sizeof(random)+sizeof(frame));
        std::memcpy(bytes.data(),positions.data(),sizeof(positions));
        std::memcpy(bytes.data()+sizeof(positions),&random,sizeof(random));
        std::memcpy(bytes.data()+sizeof(positions)+sizeof(random),&frame,sizeof(frame));return bytes;
    }
    void load(const net::RollbackTimeline::State& bytes) {
        require(bytes.size()==sizeof(positions)+sizeof(random)+sizeof(frame),"snapshot format");
        std::memcpy(positions.data(),bytes.data(),sizeof(positions));
        std::memcpy(&random,bytes.data()+sizeof(positions),sizeof(random));
        std::memcpy(&frame,bytes.data()+sizeof(positions)+sizeof(random),sizeof(frame));
    }
    void step(uint64_t tick,const net::Inputs& inputs,bool replay) {
        require(tick==frame+1,"nonsequential engine step");frame=tick;
        random=random*6364136223846793005ULL+1;
        // Coupled state: correcting one peer changes the other peers' outcomes.
        for(unsigned i=0;i<8;++i)positions[i]=(positions[i]*17+inputs[i*2]+(inputs[i*2+1]*43)+positions[(i+7)%8]+random)%1000003;
        if(!replay){++live_presentations;visible_inputs.push_back(inputs);}
    }
    net::RollbackTimeline::Engine engine(){return {[this]{return save();},[this](const auto& b){load(b);},[this](auto t,const auto& i,bool r){step(t,i,r);}};}
};
template<class F>void rejects(F f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}require(threw,"invalid input accepted");}
int main(){try {
    constexpr unsigned frames=600,window=12;
    for(unsigned players:{2u,4u,8u})for(unsigned slot=0;slot<players;++slot) {
        World predicted,reference;net::RollbackTimeline timeline(players,slot,window,predicted.engine());
        std::mt19937 random(100+slot);
        std::vector<net::Inputs> actual(frames+1);
        std::map<unsigned,std::vector<unsigned>> arrivals;
        for(unsigned t=1;t<=frames;++t){
            for(unsigned p=0;p<players;++p){actual[t][2*p]=(uint8_t)(random()%9);actual[t][2*p+1]=(uint8_t)(random()%4);}
            arrivals[t+1+random()%8].push_back(t);reference.step(t,actual[t],false);
        }
        for(unsigned clock=1;clock<=frames+9;++clock) {
            for(auto t:arrivals[clock]){timeline.receive(t,actual[t]);timeline.receive(t,actual[t]);}
            timeline.reconcile();
            if(clock<=frames){
                require(timeline.advance({actual[clock][2*slot],actual[clock][2*slot+1]}),"unexpected stall");
                require(predicted.visible_inputs.back()[2*slot]==actual[clock][2*slot],"local steering delayed");
                require(predicted.visible_inputs.back()[2*slot+1]==actual[clock][2*slot+1],"local buttons delayed");
            }
            require(timeline.pending_frames()<=window,"unbounded state history");
        }
        require(predicted.save()==reference.save(),"rollback diverged from authoritative simulation");
        require(timeline.confirmed_tick()==frames,"confirmation not drained");
        require(timeline.metrics().rollbacks && timeline.metrics().replayed_frames,"no actual corrections exercised");
        require(predicted.live_presentations==frames && !predicted.replay_presentations,"duplicate presentation");
    }
    World stalled;net::RollbackTimeline bounded(2,0,3,stalled.engine());
    require(bounded.advance({1,0}) && bounded.advance({2,0}) && bounded.advance({3,0}),"window rejected input");
    auto before=stalled.save();require(!bounded.advance({4,0}) && stalled.save()==before,"window overrun modified engine");
    net::Inputs bundle{};bundle[0]=1;bounded.receive(1,bundle);bounded.reconcile();require(bounded.advance({4,0}),"stalled simulation did not recover");
    bundle[0]=9;rejects([&]{bounded.receive(1,bundle);});rejects([&]{bounded.advance({5,0});});
    World malformed;net::RollbackTimeline bad(2,0,3,malformed.engine());
    require(bad.advance({1,2}),"initial step");bundle={};bundle[0]=2;rejects([&]{bad.receive(1,bundle);});
    World future;net::RollbackTimeline ahead(2,0,3,future.engine());bundle={};rejects([&]{ahead.receive(99,bundle);});
    uint64_t recovered_stalls=0,loss_replays=0;
    // A lossy-delivery fixture with eventual retransmission. This exercises the
    // coordinator, not a claim about socket/EOS packet-loss handling.
    for(unsigned players:{2u,4u,8u})for(unsigned seed=0;seed<12;++seed) {
        World predicted,reference;const unsigned slot=seed%players;
        net::RollbackTimeline timeline(players,slot,12,predicted.engine());
        std::mt19937 rng(9000+seed);constexpr unsigned count=2400;
        std::vector<net::Inputs> real(count+1);
        for(unsigned t=1;t<=count;++t)for(unsigned p=0;p<players;++p) {
            real[t][2*p]=(uint8_t)(rng()%32);real[t][2*p+1]=(uint8_t)rng();
        }
        std::map<unsigned,std::vector<unsigned>> pending;
        for(unsigned clock=1;clock<=count*100 && timeline.confirmed_tick()<count;++clock) {
            auto found=pending.find(clock);
            if(found!=pending.end()) {
                std::reverse(found->second.begin(),found->second.end());
                for(auto tick:found->second)timeline.receive(tick,real[tick]);
                pending.erase(found);
            }
            timeline.reconcile();
            const auto tick=(unsigned)timeline.next_tick();
            if(tick<=count) {
                if(timeline.advance({real[tick][2*slot],real[tick][2*slot+1]})) {
                    reference.step(tick,real[tick],false);
                    unsigned delay=1+rng()%9;
                    if(rng()%5==0)delay+=18+rng()%36; // lost original, delayed retry
                    pending[clock+delay].push_back(tick);
                    if(rng()%3==0)pending[clock+delay+1+rng()%30].push_back(tick);
                }else ++recovered_stalls;
            }
            require(timeline.pending_frames()<=12,"loss fixture exceeded prediction limit");
        }
        require(timeline.confirmed_tick()==count,"loss recovery failed to drain");
        require(predicted.save()==reference.save(),"loss recovery changed authoritative world");
        require(predicted.live_presentations==count,"recovery duplicated a live frame");
        loss_replays+=timeline.metrics().replayed_frames;
    }
    require(recovered_stalls && loss_replays,"loss fixture failed to exercise stall/replay");
    World failure;auto callbacks=failure.engine();callbacks.save=[] {return net::RollbackTimeline::State{};};
    net::RollbackTimeline empty(2,0,3,callbacks);rejects([&]{empty.advance({1,0});});rejects([&]{empty.reconcile();});
    callbacks=failure.engine();callbacks.advance=[](auto,const auto&,bool){throw std::runtime_error("injected engine failure");};
    net::RollbackTimeline crashed(2,0,3,callbacks);rejects([&]{crashed.advance({1,0});});rejects([&]{crashed.receive(1,{});});
    printf("PASS: 36 lossy-delivery/retry scenarios, 86400 frames, %llu bounded stalls recovered, %llu replayed updates; snapshot/engine failures poison the session\n",
        (unsigned long long)recovered_stalls,(unsigned long long)loss_replays);
    puts("PASS: 14 peer/slot scenarios, 8400 immediate local frames, jitter/out-of-order/duplicate corrections, authoritative state equality, bounded stalls/recovery, and protocol rejection.");
    return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
