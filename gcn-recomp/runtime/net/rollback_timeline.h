#pragma once
#include "peer_link.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

namespace net {
// Transport-independent rollback coordinator. The owner MUST provide complete
// deterministic snapshots and suppress presentation side effects during replay.
// This class does not make an incomplete guest-memory snapshot safe.
class RollbackTimeline {
public:
    using State=std::vector<uint8_t>;
    struct Engine {
        std::function<State()> save;
        std::function<void(const State&)> load;
        std::function<void(uint64_t,const Inputs&,bool)> advance;
    };
    struct Metrics {uint64_t rollbacks=0,replayed_frames=0;unsigned deepest=0;};
private:
    struct Frame {uint64_t tick;std::array<uint8_t,2> local;Inputs used;State before;};
    Engine engine_;
    unsigned players_,slot_,window_;
    uint64_t next_=1,confirmed_=0,dirty_=0;
    Inputs seed_{};
    std::deque<Frame> history_;
    std::map<uint64_t,Inputs> actual_;
    Metrics metrics_;
    bool failed_=false;
    void valid()const {if(failed_)throw std::runtime_error("Rollback timeline requires reset after a failure");}
    Inputs inputs(uint64_t tick,const std::array<uint8_t,2>& local,const Inputs& previous)const {
        auto i=actual_.find(tick);Inputs out=i==actual_.end()?previous:i->second;
        if(i!=actual_.end() && (out[2*slot_]!=local[0] || out[2*slot_+1]!=local[1]))
            throw std::runtime_error("Confirmed bundle changed local input");
        out[2*slot_]=local[0];out[2*slot_+1]=local[1];
        return out;
    }
    State snapshot() {
        State state=engine_.save();
        if(state.empty() || state.size()>64u*1024u*1024u)throw std::runtime_error("Invalid rollback snapshot size");
        return state;
    }
    void retire() {
        while(!history_.empty() && actual_.count(history_.front().tick)) {
            const auto& frame=history_.front();
            if(frame.used!=actual_.at(frame.tick))throw std::runtime_error("Uncorrected frame cannot be confirmed");
            confirmed_=frame.tick;seed_=frame.used;history_.pop_front();
        }
        // Retain a bounded duplicate-check history, plus future bundles.
        while(!actual_.empty() && actual_.begin()->first+window_<confirmed_)actual_.erase(actual_.begin());
    }
public:
    RollbackTimeline(unsigned players,unsigned local_slot,unsigned maximum_prediction,Engine engine)
        :engine_(std::move(engine)),players_(players),slot_(local_slot),window_(maximum_prediction) {
        if(players<2 || players>kMaxPeers || local_slot>=players || !maximum_prediction || maximum_prediction>30 ||
           !engine_.save || !engine_.load || !engine_.advance)throw std::invalid_argument("Invalid rollback configuration");
    }
    uint64_t next_tick()const{return next_;}
    uint64_t confirmed_tick()const{return confirmed_;}
    size_t pending_frames()const{return history_.size();}
    const Metrics& metrics()const{return metrics_;}
    void receive(uint64_t tick,const Inputs& bundle) {
        valid();
        try {
            if(!tick || tick>next_+window_)throw std::runtime_error("Confirmed tick outside receive window");
            for(unsigned i=players_*2;i<bundle.size();++i)if(bundle[i])throw std::runtime_error("Input for absent peer");
            auto found=actual_.find(tick);
            if(found!=actual_.end()) {
                if(found->second!=bundle)throw std::runtime_error("Conflicting confirmed bundle");
                return;
            }
            if(tick<=confirmed_)return; // outside retained history, already retired
            for(const auto& frame:history_)if(frame.tick==tick) {
                if(bundle[2*slot_]!=frame.local[0] || bundle[2*slot_+1]!=frame.local[1])
                    throw std::runtime_error("Confirmed bundle changed local input");
                if(bundle!=frame.used && (!dirty_ || tick<dirty_))dirty_=tick;
                break;
            }
            actual_.emplace(tick,bundle);
        }catch(...){failed_=true;throw;}
    }
    void reconcile() {
        valid();
        try {
            if(dirty_) {
                auto first=std::find_if(history_.begin(),history_.end(),[&](const Frame& f){return f.tick==dirty_;});
                if(first==history_.end())throw std::runtime_error("Required rollback snapshot expired");
                engine_.load(first->before);
                Inputs previous=first==history_.begin()?seed_:std::prev(first)->used;
                const unsigned depth=(unsigned)std::distance(first,history_.end());
                for(auto i=first;i!=history_.end();++i) {
                    i->before=snapshot();i->used=inputs(i->tick,i->local,previous);
                    engine_.advance(i->tick,i->used,true);previous=i->used;
                }
                ++metrics_.rollbacks;metrics_.replayed_frames+=depth;metrics_.deepest=std::max(metrics_.deepest,depth);
                dirty_=0;
            }
            retire();
        }catch(...){failed_=true;throw;}
    }
    // False means the prediction window is exhausted: wait for real input.
    // A successful step always uses this frame's local input, with no local delay.
    bool advance(const std::array<uint8_t,2>& local) {
        valid();reconcile();
        if(history_.size()>=window_)return false;
        try {
            Inputs previous=history_.empty()?seed_:history_.back().used;
            Frame frame{next_,local,inputs(next_,local,previous),snapshot()};
            engine_.advance(next_,frame.used,false);
            history_.push_back(std::move(frame));++next_;retire();return true;
        }catch(...){failed_=true;throw;}
    }
};
}
