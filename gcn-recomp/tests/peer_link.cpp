#include "net/peer_link.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void group(unsigned count, unsigned port, bool mismatch=false, bool admitted=false,unsigned delay=0) {
    printf("Testing %u peers, delay %u, mismatch %u, admissions %u\n",count,delay,mismatch,admitted);fflush(stdout);
    const std::string endpoint="127.0.0.1:"+std::to_string(port);
    std::vector<net::Admission> tickets(count);
    for (unsigned i=0;i<count;++i) tickets[i].fill((uint8_t)(i+10));
    auto host=std::async(std::launch::async,[&] {
        net::PeerLink link;
        require(link.input_delay(delay),link.error().c_str());
        require(link.host(endpoint,count,1234,admitted?tickets.data():nullptr),link.error().c_str());
        for (unsigned tick=1;tick<=120;++tick) {
            uint8_t input[2]={(uint8_t)tick,0};
            net::Inputs result;
            const bool ok=link.exchange(tick,tick,input,result);
            // The asynchronous worker can reject an already submitted future
            // checksum before its buffered presentation tick is consumed.
            if(mismatch && !ok){require(tick>=20 && tick<=20+delay,"desync failed outside its submitted window");return;}
            if(mismatch && tick==20+delay)throw std::runtime_error("desync was accepted");
            if(!ok)fprintf(stderr,"Host failed at tick %u: %s\n",tick,link.error().c_str());
            require(ok,link.error().c_str());
            for (unsigned slot=0;slot<count;++slot)
                require(result[slot*2]==(tick>delay?(uint8_t)(tick-delay):0) && result[slot*2+1]==(tick>delay?slot:0),"wrong canonical input");
            for (unsigned i=count*2;i<32;++i) require(result[i]==0,"unused slot is not neutral");
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<std::future<void>> peers;
    for (unsigned i=1;i<count;++i) peers.push_back(std::async(std::launch::async,[&,i] {
        net::PeerLink link;
        require(link.input_delay(delay),link.error().c_str());
        if (admitted) std::this_thread::sleep_for(std::chrono::milliseconds((count-i)*10));
        require(link.join(endpoint,1234,admitted?i:net::kMaxPeers,admitted?&tickets[i]:nullptr),link.error().c_str());
        if (admitted) require(link.slot()==i,"reserved slot changed with arrival order");
        for (unsigned tick=1;tick<=120;++tick) {
            uint8_t input[2]={(uint8_t)tick,(uint8_t)link.slot()};
            net::Inputs result;
            const bool ok=link.exchange(tick,mismatch && tick==20 ? tick+1 : tick,input,result);
            if(mismatch && !ok){require(tick>=20 && tick<=20+delay,"peer desync failed outside its submitted window");return;}
            if(mismatch && tick==20+delay)throw std::runtime_error("desync was accepted by peer");
            if(!ok)fprintf(stderr,"Peer %u failed at tick %u: %s\n",i,tick,link.error().c_str());
            require(ok,link.error().c_str());
            for (unsigned slot=0;slot<count;++slot)
                require(result[slot*2]==(tick>delay?(uint8_t)(tick-delay):0) && result[slot*2+1]==(tick>delay?slot:0),"peer got different inputs");
            // A staggered peer exercises arrival ordering without changing
            // which slot contributes each input.
            if ((tick+i)%23==0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }));
    host.get();
    for (auto& peer:peers) peer.get();
}
void reject_admission(unsigned port, bool duplicate) {
    const std::string endpoint="127.0.0.1:"+std::to_string(port);
    net::Admission tickets[3];
    for (unsigned i=0;i<3;++i) tickets[i].fill((uint8_t)(i+10));
    auto host=std::async(std::launch::async,[&] {
        net::PeerLink link;
        require(!link.host(endpoint,duplicate?3:2,1234,tickets),"invalid admission accepted");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto peer=[&] {
        net::PeerLink link;
        net::Admission ticket=tickets[1];
        if (!duplicate) ticket[0]^=1;
        require(!link.join(endpoint,1234,1,&ticket),"invalid admission got a welcome");
    };
    auto first=std::async(std::launch::async,peer);
    if (duplicate) { auto second=std::async(std::launch::async,peer); second.get(); }
    first.get();host.get();
}
int main() {
    try {
        const unsigned port=40000+(unsigned)(std::chrono::steady_clock::now().time_since_epoch().count()%15000);
        group(2,port);
        group(8,port+1);
        group(2,port+2,true);
        group(8,port+4,false,true);
        group(2,port+7,false,false,6);
        group(8,port+8,false,true,6);
        group(2,port+9,true,false,6);
        reject_admission(port+5,false);
        reject_admission(port+6,true);
        auto incompatible_host=std::async(std::launch::async,[&] {
            net::PeerLink link;
            require(!link.host("127.0.0.1:"+std::to_string(port+3),2,1234),"incompatible peer accepted");
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        net::PeerLink incompatible;
        require(!incompatible.join("127.0.0.1:"+std::to_string(port+3),1235),"incompatible host accepted");
        incompatible_host.get();
        net::PeerLink invalid;
        require(!invalid.join("127.0.0.1:0",1234),"zero port accepted");
        net::PeerLink invalid_count;
        require(!invalid_count.host("127.0.0.1:40000",net::kMaxPeers+1,1234),"oversized session accepted");
        net::Admission ticket{};
        net::PeerLink invalid_slot;
        require(!invalid_slot.join("127.0.0.1:40000",1234,0,&ticket),"peer claimed host slot");
        puts("PASS: 2/8 peers, numbered ticks, canonical slots, reverse reserved arrivals, invalid/duplicate admissions, desync/build rejection, invalid config");
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr,"FAIL: %s\n",e.what()); return 1;
    }
}
