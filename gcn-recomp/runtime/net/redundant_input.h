#pragma once
#include "input_mesh.h"
#include <functional>
#include <limits>

namespace net::mesh {
// Convert an authenticated, lossy datagram channel into exact ordered input
// contributions. Each small packet repeats unacknowledged input; acknowledgments
// are repeated too, including when the simulation is waiting or has stopped
// producing new input. Handshake/control traffic stays on its reliable channel.
class RedundantInput {
public:
    using Send=std::function<void(unsigned,const std::vector<uint8_t>&)>;
    using Deliver=std::function<void(unsigned,const std::vector<uint8_t>&)>;
    RedundantInput(unsigned count,unsigned slot,uint64_t first,Send send,Deliver deliver)
        :count_(count),slot_(slot),first_(first),sent_(first-1),send_(std::move(send)),deliver_(std::move(deliver)),
         acked_(count,first-1),next_(count,first),received_(count),history_(count) {
        if(count<2 || count>kMaxPeers || slot>=count || !first)fail("Invalid redundant input roster");
    }
    void native(const Packet& p) {
        if(!valid(p,3) || read(p,8)!=sent_+1)fail("Nonsequential redundant local input");
        const auto tick=read(p,8);validate(p);
        local_.emplace(tick,p);sent_=tick;
        trim();if(local_.size()>64)fail("Redundant local history overflow");
        for(unsigned peer=0;peer<count_;++peer)if(peer!=slot_)emit(peer);
    }
    void peer(unsigned sender,const std::vector<uint8_t>& bytes) {
        if(sender>=count_ || sender==slot_ || bytes.size()<18 || bytes[0]!=14 || bytes[1]>24 || bytes.size()!=18+12*size_t(bytes[1]))fail("Invalid redundant input envelope");
        const auto ack=get(bytes,2),first=get(bytes,10);const unsigned count=bytes[1];
        if(ack<first_-1 || ack>sent_ || (!count && first) || (count && (first<first_ || first>std::numeric_limits<uint64_t>::max()-count)))fail("Invalid redundant input range");
        // Validate the whole envelope before acknowledging or delivering any of
        // it. Repeated immutable input must compare equal, never silently win.
        std::vector<Packet> packets;
        for(unsigned i=0;i<count;++i) {
            const uint64_t tick=first+i;
            if(tick>=next_[sender] && tick-next_[sender]>=64)fail("Redundant receive window exceeded");
            Packet p=packet(3,tick,get(bytes,18+12*i));p[32]=bytes[26+12*i];p[33]=bytes[27+12*i];p[24]=bytes[28+12*i];p[25]=bytes[29+12*i];validate(p);
            auto known=history_[sender].find(tick);
            if(known!=history_[sender].end() && known->second!=p)fail("Changed redundant historical input");
            auto pending=received_[sender].find(tick);
            if(pending!=received_[sender].end() && pending->second!=p)fail("Changed redundant pending input");
            packets.push_back(p);
        }
        acked_[sender]=std::max(acked_[sender],ack);trim();
        for(const auto& p:packets)if(read(p,8)>=next_[sender])received_[sender].emplace(read(p,8),p);
        for(;;) {
            auto found=received_[sender].find(next_[sender]);if(found==received_[sender].end())break;
            const auto p=found->second;
            std::vector<uint8_t> contribution{12};contribution.insert(contribution.end(),p.begin(),p.end());
            deliver_(sender,contribution);history_[sender].emplace(next_[sender],p);
            received_[sender].erase(found);++next_[sender];
        }
        while(history_[sender].size()>64)history_[sender].erase(history_[sender].begin());
    }
    void pump(uint64_t now) {
        if(pumped_ && now-last_pump_<16)return;
        pumped_=true;last_pump_=now;
        for(unsigned peer=0;peer<count_;++peer)if(peer!=slot_)emit(peer);
    }
    size_t retained()const{return local_.size();}
private:
    unsigned count_,slot_;uint64_t first_,sent_,last_pump_=0;bool pumped_=false;
    Send send_;Deliver deliver_;
    std::vector<uint64_t> acked_,next_;
    std::map<uint64_t,Packet> local_;
    std::vector<std::map<uint64_t,Packet>> received_,history_;
    [[noreturn]] static void fail(const char* why){throw std::runtime_error(why);}
    static uint64_t get(const std::vector<uint8_t>& b,unsigned at) {uint64_t n=0;for(unsigned i=0;i<8;++i)n=(n<<8)|b[at+i];return n;}
    static void put(std::vector<uint8_t>& b,unsigned at,uint64_t n) {for(unsigned i=0;i<8;++i){b[at+7-i]=uint8_t(n);n>>=8;}}
    static void validate(const Packet& p) {auto expected=packet(3,read(p,8),read(p,16));expected[32]=p[32];expected[33]=p[33];expected[24]=p[24];expected[25]=p[25];if(p!=expected || p[24]>16 || p[25]>16)fail("Invalid redundant input fields");}
    void trim() {
        uint64_t minimum=sent_;for(unsigned peer=0;peer<count_;++peer)if(peer!=slot_)minimum=std::min(minimum,acked_[peer]);
        while(!local_.empty() && local_.begin()->first<=minimum)local_.erase(local_.begin());
    }
    void emit(unsigned peer) {
        auto it=local_.upper_bound(acked_[peer]);const auto begin=it;
        unsigned count=0;for(;it!=local_.end() && count<24;++it)++count;
        std::vector<uint8_t> bytes(18+12*count);bytes[0]=14;bytes[1]=uint8_t(count);
        put(bytes,2,next_[peer]-1);put(bytes,10,count?begin->first:0);
        it=begin;for(unsigned i=0;i<count;++i,++it){put(bytes,18+12*i,read(it->second,16));bytes[26+12*i]=it->second[32];bytes[27+12*i]=it->second[33];bytes[28+12*i]=it->second[24];bytes[29+12*i]=it->second[25];}
        send_(peer,bytes);
    }
};
}
