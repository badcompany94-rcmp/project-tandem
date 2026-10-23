#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace mkdd::eos {
// Small reliable-ordered EOS packets carry bounded control messages. Session
// tokens stay in authenticated peer traffic, never lobby search attributes.
struct Message { uint8_t kind;uint32_t id;std::string body; };
inline std::vector<std::vector<uint8_t>> fragments(uint8_t kind,uint32_t id,const std::string& body) {
    if((kind!=1 && kind!=2) || !id || body.empty() || body.size()>65536)throw std::runtime_error("invalid-control-message");
    const unsigned count=(unsigned)((body.size()+999)/1000);
    std::vector<std::vector<uint8_t>> result;
    for(unsigned index=0;index<count;++index) {
        std::vector<uint8_t> packet={'D','D',1,kind,(uint8_t)(id>>24),(uint8_t)(id>>16),(uint8_t)(id>>8),(uint8_t)id,
                                    (uint8_t)(index>>8),(uint8_t)index,(uint8_t)(count>>8),(uint8_t)count};
        const size_t begin=index*1000,end=std::min(begin+1000,body.size());
        packet.insert(packet.end(),body.begin()+begin,body.begin()+end);result.push_back(std::move(packet));
    }
    return result;
}
class Reassembler {
    using Clock=std::chrono::steady_clock;
    struct Partial { unsigned next=0,count=0;std::string body;Clock::time_point touched=Clock::now(); };
    std::map<std::tuple<std::string,uint8_t,uint32_t>,Partial> pending_;
public:
    std::optional<Message> add(const std::string& peer,const std::vector<uint8_t>& packet) {
        const auto now=Clock::now();
        for(auto it=pending_.begin();it!=pending_.end();) {
            if(now-it->second.touched>std::chrono::seconds(10))it=pending_.erase(it);else ++it;
        }
        if(peer.empty() || packet.size()<13 || packet.size()>1012 || packet[0]!='D' || packet[1]!='D' || packet[2]!=1 || (packet[3]!=1 && packet[3]!=2))
            throw std::runtime_error("invalid-control-fragment");
        const uint32_t id=(uint32_t(packet[4])<<24)|(uint32_t(packet[5])<<16)|(uint32_t(packet[6])<<8)|packet[7];
        const unsigned index=(packet[8]<<8)|packet[9],count=(packet[10]<<8)|packet[11];
        if(!id || !count || count>66 || index>=count || (index+1<count && packet.size()!=1012))throw std::runtime_error("invalid-control-fragment");
        const auto key=std::make_tuple(peer,packet[3],id);
        if(!pending_.count(key)) {
            if(index || pending_.size()>=32)throw std::runtime_error("invalid-control-fragment-order");
            pending_[key].count=count;
        }
        auto& partial=pending_.at(key);
        if(partial.next!=index || partial.count!=count || partial.body.size()+packet.size()-12>65536) {
            pending_.erase(key);throw std::runtime_error("invalid-control-fragment-order");
        }
        partial.body.append((const char*)packet.data()+12,packet.size()-12);partial.touched=now;++partial.next;
        if(partial.next!=count)return std::nullopt;
        Message result{packet[3],id,std::move(partial.body)};pending_.erase(key);return result;
    }
};
}
