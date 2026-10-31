#pragma once
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace mkdd::eos {
// The room/game bridge owns synchronization; transports own membership and I/O.
class PeerRuntime {
public:
    virtual ~PeerRuntime()=default;
    virtual void tick()=0;
    virtual void create(const std::string&,const std::string&)=0;
    virtual void join(const std::string&,const std::string&)=0;
    virtual void leave()=0;
    virtual void kick(const std::string&)=0;
    virtual void refresh_invitation(const std::string&)=0;
    virtual std::set<std::string> members()const=0;
    virtual bool send(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&)=0;
    virtual void game_peers(std::string,std::set<std::string>)=0;
    virtual const std::string& self()const=0;
    virtual const std::string& owner()const=0;
    virtual bool hosting()const=0;
    virtual const std::string& control_socket()const=0;
    virtual bool authorized(const std::string&,const std::string&)const=0;
    virtual bool lan()const{return false;}
    virtual std::string route()const{return "unknown";}
    std::function<void(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&)> packet;
    std::function<void()> idle;
};
}
