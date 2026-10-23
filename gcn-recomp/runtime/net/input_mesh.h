#pragma once
#include "peer_link.h"
#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace net::mesh {
using Packet=std::array<uint8_t,64>;
inline uint64_t read(const Packet& p,unsigned at,unsigned size=8) {
    uint64_t result=0;for(unsigned i=0;i<size;++i)result=(result<<8)|p[at+i];return result;
}
inline void write(Packet& p,unsigned at,uint64_t value,unsigned size=8) {
    for(unsigned i=0;i<size;++i){p[at+size-1-i]=uint8_t(value);value>>=8;}
}
inline Packet packet(unsigned kind,uint64_t tick=0,uint64_t hash=0) {
    Packet p{};p[0]='G';p[1]='C';p[2]='I';p[3]='1';write(p,4,2,2);write(p,6,kind,2);write(p,8,tick);write(p,16,hash);return p;
}
inline bool valid(const Packet& p,unsigned kind) {
    const auto header=packet(kind);return std::equal(p.begin(),p.begin()+8,header.begin());
}

// Each authenticated participant sends its own input directly to all others.
// Every local bridge assembles the same ordered bundle without a host round trip.
// The caller authenticates sender identity/session and provides reliable ordered
// delivery. Admission tickets are checked only on loopback and never broadcast.
class InputMesh {
public:
    struct Outgoing {unsigned peer;std::vector<uint8_t> bytes;};
    InputMesh(unsigned count,unsigned slot,Admission admission):count_(count),slot_(slot),admission_(admission),hello_(count),acked_(count),next_(count) {
        if(count<2 || count>kMaxPeers || slot>=count)fail("Invalid mesh roster");
    }
    void native(const Packet& p) {
        check();
        if(!local_) {
            if(!valid(p,1) || read(p,8) || p[32]!=slot_ || p[33] || p[34]>12 || p[35]>30 || p[36]!=1 ||
               !std::equal(admission_.begin(),admission_.end(),p.begin()+40))fail("Invalid mesh loopback handshake");
            Packet clean=packet(1,0,read(p,16));clean[32]=uint8_t(slot_);clean[33]=uint8_t(count_);
            clean[34]=p[34];clean[35]=p[35];clean[36]=1;
            local_=clean;hello_[slot_]=clean;acked_[slot_]=true;
            next_bundle_=uint64_t(p[34])+1;
            for(unsigned i=0;i<count_;++i)if(hello_[i])validate_hello(i,*hello_[i]);
            retry();ready();return;
        }
        if(!welcomed_)fail("Mesh input preceded handshake completion");
        contribution(slot_,p);
        for(unsigned peer=0;peer<count_;++peer)if(peer!=slot_)send(peer,12,p);
    }
    void peer(unsigned sender,const std::vector<uint8_t>& bytes) {
        check();if(sender>=count_ || sender==slot_ || bytes.empty())fail("Invalid authenticated mesh sender");
        if(bytes[0]==10 && bytes.size()==65) {
            Packet p;std::copy(bytes.begin()+1,bytes.end(),p.begin());
            if(!valid(p,1) || read(p,8) || p[32]!=sender || p[33]!=count_ || p[34]>12 || p[35]>30 || p[36]!=1)
                fail("Invalid mesh peer handshake");
            Packet expected=packet(1,0,read(p,16));expected[32]=uint8_t(sender);expected[33]=uint8_t(count_);
            expected[34]=p[34];expected[35]=p[35];expected[36]=1;
            if(p!=expected)fail("Mesh handshake contains unexpected private or reserved fields");
            if(hello_[sender] && *hello_[sender]!=p)fail("Mesh peer changed its handshake");
            hello_[sender]=p;if(local_)validate_hello(sender,p);
            outgoing_.push_back({sender,{11}});ready();
        }else if(bytes[0]==11 && bytes.size()==1) {
            if(!local_)fail("Mesh acknowledgement preceded local handshake");
            acked_[sender]=true;ready();
        }else if(bytes[0]==12 && bytes.size()==65) {
            if(!local_ || !hello_[sender])fail("Mesh input preceded authenticated handshake");
            Packet p;std::copy(bytes.begin()+1,bytes.end(),p.begin());contribution(sender,p);
        }else fail("Invalid mesh packet");
        if(outgoing_.size()>128)fail("Mesh outgoing queue overflow");
    }
    void retry() {
        check();if(!local_)return;
        for(unsigned peer=0;peer<count_;++peer)if(peer!=slot_ && !acked_[peer])send(peer,10,*local_);
        if(outgoing_.size()>128)fail("Mesh outgoing queue overflow");
    }
    std::vector<Outgoing> outgoing() {check();auto result=std::move(outgoing_);outgoing_.clear();return result;}
    std::vector<Packet> output() {check();auto result=std::move(output_);output_.clear();return result;}
    bool welcomed()const{return welcomed_;}
private:
    unsigned count_,slot_;Admission admission_;
    bool failed_=false,welcomed_=false;
    std::optional<Packet> local_;
    std::vector<std::optional<Packet>> hello_;
    std::vector<bool> acked_;
    std::vector<uint64_t> next_;
    uint64_t next_bundle_=0;
    std::map<uint64_t,std::vector<std::optional<Packet>>> pending_;
    std::vector<Outgoing> outgoing_;
    std::vector<Packet> output_;
    [[noreturn]] void fail(const char* reason){failed_=true;throw std::runtime_error(reason);}
    void check(){if(failed_)fail("Mesh session is already failed");}
    void send(unsigned peer,uint8_t kind,const Packet& p) {
        if(outgoing_.size()>=128)fail("Mesh outgoing queue overflow");
        std::vector<uint8_t> bytes{kind};bytes.insert(bytes.end(),p.begin(),p.end());outgoing_.push_back({peer,std::move(bytes)});
    }
    void validate_hello(unsigned peer,const Packet& p) {
        if(read(p,16)!=read(*local_,16) || p[34]!=(*local_)[34] || p[35]!=(*local_)[35])fail("Incompatible mesh peer");
        if(!next_[peer])next_[peer]=uint64_t(p[34])+1;
    }
    void ready() {
        if(!local_ || welcomed_)return;
        for(unsigned i=0;i<count_;++i)if(!hello_[i] || !acked_[i])return;
        Packet welcome=packet(2);welcome[32]=uint8_t(slot_);welcome[33]=uint8_t(count_);
        welcome[34]=(*local_)[34];welcome[35]=(*local_)[35];welcome[36]=1;
        output_.push_back(welcome);welcomed_=true;flush();
    }
    void contribution(unsigned sender,const Packet& p) {
        if(!valid(p,3) || read(p,8)!=next_[sender] || read(p,8)<next_bundle_ || read(p,8)-next_bundle_>=64)
            fail("Invalid or out-of-window mesh input sequence");
        Packet expected=packet(3,read(p,8),read(p,16));expected[32]=p[32];expected[33]=p[33];expected[24]=p[24];expected[25]=p[25];
        if(p!=expected || p[24]>16 || p[25]>16)fail("Mesh input contains unexpected fields");
        auto& row=pending_[read(p,8)];if(row.empty())row.resize(count_);
        if(row[sender])fail("Duplicate mesh contribution");
        row[sender]=p;++next_[sender];flush();
    }
    void flush() {
        if(!welcomed_)return;
        for(;;) {
            auto found=pending_.find(next_bundle_);if(found==pending_.end())break;
            const auto& row=found->second;bool complete=true;
            for(const auto& value:row)if(!value){complete=false;break;}
            if(!complete)break;
            const auto hash=read(*row[0],16);Packet bundle=packet(4,next_bundle_,hash);bundle[25]=16;
            for(unsigned peer=0;peer<count_;++peer) {
                if(read(*row[peer],16)!=hash)fail("Mesh confirmed-state checksum disagreement");
                bundle[24]=std::max(bundle[24],(*row[peer])[24]);
                bundle[25]=std::min(bundle[25],(*row[peer])[25]);
                bundle[32+2*peer]=(*row[peer])[32];bundle[33+2*peer]=(*row[peer])[33];
            }
            output_.push_back(bundle);pending_.erase(found);++next_bundle_;
            if(output_.size()>128)fail("Mesh loopback output overflow");
        }
    }
};
}
