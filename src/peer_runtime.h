#pragma once
#include "online_json.h"
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
    virtual bool native_prediction()const{return lan();}
    virtual bool local_peer(const std::string&)const{return lan();}
    virtual std::string route()const{return "unknown";}
    // Discovery runs only in the helper, never on the simulation thread.
    virtual void publish(const online::Json&,const std::string&){}
    virtual online::Json browse(const std::string&){online::Json rows;rows.kind=online::Json::Array;return rows;}
    virtual void join_target(const std::string& code,const std::string& build,const online::Json&){join(code,build);}
    virtual std::string browser_notice()const{return {};}
    virtual std::string reserve_name(const std::string& requested){return requested;}
    // A replacement keeps the previous claim until the room accepts the edit.
    virtual void commit_name(){}
    virtual void cancel_name(){}
    std::function<void(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&)> packet;
    std::function<void()> idle;
};
}
