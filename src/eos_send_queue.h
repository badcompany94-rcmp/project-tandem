#pragma once
#include <deque>
#include <set>
#include <stdexcept>
#include <string>

namespace mkdd::eos {
// A transient send failure must not block unrelated destinations. Leave failed
// packets in their original positions and defer every later packet to that peer
// for this pump. Rotating failures to the back can reorder a peer's packets when
// the budget expires. Authorization and expiry still apply to deferred packets.
struct SendQueueResult {unsigned sent=0,blocked=0,discarded=0;};
template<class Packet,class Time,class Authorized,class Send>
SendQueueResult drain_send_queue(std::deque<Packet>& queue,size_t& bytes,
    Time now,typename Time::duration timeout,Authorized authorized,Send send,unsigned budget=256) {
    SendQueueResult result;
    std::set<std::string> blocked;
    unsigned attempts=0;
    for(auto it=queue.begin();it!=queue.end();) {
        auto& packet=*it;
        if(!authorized(packet)) {
            bytes-=packet.data.size();it=queue.erase(it);++result.discarded;continue;
        }
        if(now-packet.queued>timeout)throw std::runtime_error("EOS peer connection timed out");
        if(blocked.count(packet.peer) || attempts>=budget){++it;continue;}
        ++attempts;
        if(!send(packet)) {
            blocked.insert(packet.peer);++result.blocked;++it;continue;
        }
        bytes-=packet.data.size();it=queue.erase(it);++result.sent;
    }
    return result;
}
}
