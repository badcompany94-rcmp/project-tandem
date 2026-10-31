#pragma once
#include "lan_runtime.h"
#include <memory>
#include <deque>

namespace mkdd::eos {
// One logical room may contain local and Internet peers. LAN-only peers never
// need an EOS identity. The host authenticates and forwards cross-route input;
// peers on the same transport retain their existing direct mesh path.
class UnifiedRuntime final:public PeerRuntime {
    LanRuntime& local;PeerRuntime* internet;
    enum Mode{Idle,Host,Local,Internet}mode=Idle;
    bool ticking=false,pumping=false;
    std::string game;std::set<std::string> game_members;
    struct Forward{std::string to,socket;std::vector<uint8_t> bytes;};
    std::deque<Forward> forwarded;
    std::string discovery_error;
    void flush();
    void pulse(bool from_internet);
    void receive(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&);
    bool physical_send(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&);
public:
    UnifiedRuntime(LanRuntime&,PeerRuntime*);
    ~UnifiedRuntime() override;
    void tick() override;
    void create(const std::string&,const std::string&) override;
    void join(const std::string&,const std::string&) override;
    void join_target(const std::string&,const std::string&,const online::Json&) override;
    void leave() override;
    void kick(const std::string&) override;
    void refresh_invitation(const std::string&) override;
    std::set<std::string> members()const override;
    bool send(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&) override;
    void game_peers(std::string,std::set<std::string>) override;
    const std::string& self()const override;
    const std::string& owner()const override;
    const std::string& control_socket()const override;
    bool hosting()const override{return mode==Host && !owner().empty();}
    bool native_prediction()const override{return true;}
    bool local_peer(const std::string& peer)const override{return local.members().count(peer)!=0;}
    bool authorized(const std::string&,const std::string&)const override;
    std::string route()const override;
    void publish(const online::Json&,const std::string&) override;
    online::Json browse(const std::string&) override;
    std::string browser_notice()const override{return !discovery_error.empty()?discovery_error:internet?"":"INTERNET UNAVAILABLE / LAN ONLY. CHECK YOUR CONNECTION";}
    std::string reserve_name(const std::string& name)override{return internet && internet->hosting()?internet->reserve_name(name):local.reserve_name(name);}
    void commit_name()override{if(internet)internet->commit_name();}
    void cancel_name()override{if(internet)internet->cancel_name();}
};
}
