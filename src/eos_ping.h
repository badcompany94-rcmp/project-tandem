#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mkdd::eos {
// Independent helper-to-helper probes, not simulation frame acknowledgements.
class PeerPings {
    struct Probe {uint64_t nonce=0,sent=0,replied=0;int rtt=-1;bool waiting=false;};
    std::map<std::string,Probe> peers_;
    uint64_t next_=1;
    static std::vector<uint8_t> packet(uint8_t kind,uint64_t nonce) {
        std::vector<uint8_t> bytes={'T','P',1,kind,0,0,0,0,0,0,0,0};
        for(int i=11;i>=4;--i){bytes[i]=(uint8_t)nonce;nonce>>=8;}return bytes;
    }
public:
    void peers(const std::set<std::string>& current) {
        for(auto i=peers_.begin();i!=peers_.end();)if(!current.count(i->first))i=peers_.erase(i);else ++i;
        for(const auto& peer:current)peers_.try_emplace(peer);
    }
    std::vector<uint8_t> probe(const std::string& peer,uint64_t now) {
        auto i=peers_.find(peer);if(i==peers_.end())return {};
        auto& p=i->second;
        if(p.nonce && now-p.sent<(p.waiting?4000u:500u))return {};
        p.nonce=next_++;if(!next_)next_=1;p.sent=now;p.waiting=true;
        return packet(1,p.nonce);
    }
    std::vector<uint8_t> received(const std::string& peer,const std::vector<uint8_t>& bytes,uint64_t now) {
        if(!peers_.count(peer) || bytes.size()!=12 || bytes[0]!='T' || bytes[1]!='P' || bytes[2]!=1 || (bytes[3]!=1 && bytes[3]!=2))return {};
        uint64_t nonce=0;for(unsigned i=4;i<12;++i)nonce=(nonce<<8)|bytes[i];
        if(!nonce)return {};
        if(bytes[3]==1)return packet(2,nonce);
        auto& p=peers_.at(peer);
        if(p.waiting && p.nonce==nonce && now>=p.sent && now-p.sent<=60000) {
            const int sample=(int)(now-p.sent);p.rtt=p.rtt<0?sample:(p.rtt*3+sample+2)/4;
            p.replied=now;p.waiting=false;
        }
        return {};
    }
    struct Summary {int rtt_ms=-1;uint64_t age_ms=0;unsigned peers=0;};
    Summary summary(uint64_t now)const {
        Summary out;out.peers=(unsigned)peers_.size();
        if(peers_.empty())return out;
        for(const auto& [peer,p]:peers_) {
            if(p.rtt<0 || now<p.replied || now-p.replied>3000)return {-1,3001,out.peers};
            out.rtt_ms=std::max(out.rtt_ms,p.rtt);out.age_ms=std::max(out.age_ms,now-p.replied);
        }
        return out;
    }
};
}
