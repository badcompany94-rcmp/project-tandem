#pragma once
#include <cstdint>
#include <deque>
#include <atomic>
// A running game can report its effective input delay after a transport-mode
// transition. -1 keeps the helper's reported value. Measured RTT is separate.
inline std::atomic<int> network_input_delay_override{-1};

// Wall-clock measurements; repeated UI refreshes never count as game frames.
class PerformanceStats {
    uint64_t started_,sequence_=0,network_at_=0;
    std::deque<uint64_t> frames_;
    double rtt_=-1;
public:
    explicit PerformanceStats(uint64_t now):started_(now){}
    void frame(uint64_t now){while(!frames_.empty() && now>=frames_.front()+1000)frames_.pop_front();frames_.push_back(now);}
    double fps(uint64_t now) {
        while(!frames_.empty() && now>=frames_.front()+1000)frames_.pop_front();
        const auto elapsed=now-started_;
        return elapsed<500?-1:frames_.size()*1000.0/(elapsed<1000?elapsed:1000);
    }
    void network(uint64_t sequence,double rtt,uint64_t age,uint64_t now) {
        if(sequence!=sequence_){sequence_=sequence;network_at_=now;}
        rtt_=age<=3000 && rtt>=0 && rtt<=60000?rtt:-1;
    }
    double rtt(uint64_t now)const{return now-network_at_<=3000?rtt_:-1;}
};
