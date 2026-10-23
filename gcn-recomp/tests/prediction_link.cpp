#include "net/peer_link.h"
#include "net/rollback_timeline.h"
#include <barrier>
#include <chrono>
#include <cstring>
#include <future>
#include <cstdio>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::array<uint8_t,2> input(unsigned slot,uint64_t tick) {
    return {uint8_t((tick*7+slot*13)%97),uint8_t((tick/3+slot)%23)};
}
static net::Inputs bundle(unsigned count,uint64_t tick) {
    net::Inputs result{};for(unsigned slot=0;slot<count;++slot){auto v=input(slot,tick);result[2*slot]=v[0];result[2*slot+1]=v[1];}return result;
}
struct Model {
    std::array<uint64_t,16> state{};
    void advance(const net::Inputs& inputs) {
        const auto before=state;
        for(unsigned i=0;i<state.size();++i)state[i]=before[i]*17+before[(i+1)%16]*3+inputs[2*i]*13+inputs[2*i+1];
    }
    net::RollbackTimeline::Engine engine() {
        return {[this]{net::RollbackTimeline::State bytes(sizeof(state));memcpy(bytes.data(),&state,sizeof(state));return bytes;},
            [this](const auto& bytes){require(bytes.size()==sizeof(state),"bad model checkpoint");memcpy(&state,bytes.data(),sizeof(state));},
            [this](uint64_t,const auto& inputs,bool){advance(inputs);}};
    }
};
static void group(unsigned count,unsigned port,unsigned delay=0) {
    const std::string endpoint="127.0.0.1:"+std::to_string(port);
    std::vector<net::Admission> tickets(count);for(unsigned i=0;i<count;++i)tickets[i].fill(uint8_t(i+1));
    std::barrier queued{int(count)};
    std::vector<std::future<void>> peers;
    auto run=[&](unsigned slot) {
        try {
            net::PeerLink link;require(link.input_delay(delay),"startup delay rejected");require(link.prediction_window(8),"prediction configuration failed");
            if(!slot)require(link.host(endpoint,count,771,tickets.data()),link.error().c_str());
            else {
                std::this_thread::sleep_for(10ms*(count-slot));
                require(link.join(endpoint,771,slot,&tickets[slot]),link.error().c_str());
            }
            require(link.slot()==slot,"reserved slot changed");
            for(uint64_t tick=1;tick<=4;++tick) {
                auto local=input(slot,tick);net::Inputs actual;
                require(link.exchange(tick,tick,local.data(),actual),link.error().c_str());
                require(actual==(tick>delay?bundle(count,tick-delay):net::Inputs{}),"startup bundle changed");
            }
            for(uint64_t tick=5;tick<=4+delay;++tick) {
                net::Inputs actual;require(link.drain_startup(tick,actual),link.error().c_str());
                require(actual==bundle(count,tick-delay),"draining discarded or changed a queued input");
            }
            Model model,reference;net::RollbackTimeline timeline(count,slot,8,model.engine());
            for(uint64_t first=1;first<=256;first+=8) {
                // No receive operation is permitted before EVERY participant
                // has queued a whole window. A stop-and-wait sender deadlocks.
                for(uint64_t tick=first;tick<first+8;++tick) {
                    auto local=input(slot,tick);
                    require(link.submit_prediction(tick+4+delay,tick*31,local.data()),link.error().c_str());
                    require(timeline.advance(local),"unexpected prediction stall");
                    reference.advance(bundle(count,tick));
                }
                queued.arrive_and_wait();
                if((first+slot)%3==0)std::this_thread::sleep_for(3ms);
                const auto deadline=std::chrono::steady_clock::now()+5s;
                for(uint64_t expected=first;expected<first+8;) {
                    uint64_t tick=0;net::Inputs actual{};const auto result=link.poll_prediction(tick,actual);
                    require(result!=net::PeerLink::Poll::Failed,link.error().c_str());
                    if(result==net::PeerLink::Poll::Pending) {
                        require(std::chrono::steady_clock::now()<deadline,"prediction receive timed out");
                        std::this_thread::sleep_for(1ms);continue;
                    }
                    require(tick==expected+4+delay && actual==bundle(count,expected),"noncanonical asynchronous bundle");
                    timeline.receive(expected,actual);++expected;
                }
                timeline.reconcile();
                require(model.state==reference.state,"real-socket prediction correction diverged");
                require(timeline.pending_frames()==0,"confirmed frames were not retired");
                queued.arrive_and_wait();
            }
            require(timeline.metrics().rollbacks>0 && timeline.metrics().replayed_frames>=256,"prediction was not exercised");
        }catch(...) {queued.arrive_and_drop();throw;}
    };
    peers.push_back(std::async(std::launch::async,run,0));std::this_thread::sleep_for(100ms);
    for(unsigned slot=1;slot<count;++slot)peers.push_back(std::async(std::launch::async,run,slot));
    for(auto& peer:peers)peer.get();
    printf("PASS: %u peers, startup delay %u, 256 predicted model frames each over real sockets; pipelined windows and exact corrections\n",count,delay);
}
static void rejection(unsigned port,bool mismatch) {
    const std::string endpoint="127.0.0.1:"+std::to_string(port);
    auto run=[&](bool host) {
        net::PeerLink link;require(link.prediction_window(2),"configuration rejected");
        require(host?link.host(endpoint,2,881):link.join(endpoint,881),link.error().c_str());
        const uint8_t input[]={1,2};
        require(link.submit_prediction(1,host || !mismatch?100:101,input),"initial submit rejected");
        if(!mismatch && host) {
            require(link.submit_prediction(2,100,input),"second bounded submit rejected");
            require(!link.submit_prediction(3,100,input),"unbounded future input accepted");return;
        }
        const auto deadline=std::chrono::steady_clock::now()+5s;
        for(;;) {
            uint64_t tick=0;net::Inputs inputs;const auto result=link.poll_prediction(tick,inputs);
            if(result==net::PeerLink::Poll::Failed)return;
            require(!(mismatch && result==net::PeerLink::Poll::Ready),"checksum disagreement accepted");
            require(std::chrono::steady_clock::now()<deadline,"failed session did not terminate");
            std::this_thread::sleep_for(1ms);
        }
    };
    auto host=std::async(std::launch::async,run,true);std::this_thread::sleep_for(100ms);
    auto peer=std::async(std::launch::async,run,false);host.get();peer.get();
}
int main() {
    try {
        unsigned port=42000+unsigned(std::chrono::steady_clock::now().time_since_epoch().count()%10000);
        for(unsigned n:{2u,4u,8u})group(n,port++);
        for(unsigned n:{2u,4u,8u})group(n,port++,3);
        rejection(port++,true);rejection(port++,false);
        net::PeerLink bad;require(!bad.prediction_window(31),"oversized window accepted");
        net::PeerLink excessive;require(!excessive.input_delay(13),"oversized startup delay accepted");
        puts("PASS: asynchronous checksum rejection, bounded queues, invalid configuration");return 0;
    }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
