#pragma once
#include "peer_runtime.h"
#include <memory>

namespace mkdd::eos {
// Local networks only. No EOS login, Internet discovery or relay dependency.
// Reliable lobby/handshake traffic uses TCP; redundant input uses direct UDP.
class LanRuntime : public PeerRuntime {
public:
    explicit LanRuntime(std::string host="",unsigned port=28122);
    ~LanRuntime();
    void tick() override;
    void create(const std::string&,const std::string&) override;
    void join(const std::string&,const std::string&) override;
    void leave() override;
    void kick(const std::string&) override;
    void refresh_invitation(const std::string&) override;
    std::set<std::string> members()const override;
    bool send(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&) override;
    void game_peers(std::string,std::set<std::string>) override;
    const std::string& self()const override;
    const std::string& owner()const override;
    bool hosting()const override;
    const std::string& control_socket()const override;
    bool authorized(const std::string&,const std::string&)const override;
    bool lan()const override{return true;}
    std::string route()const override{return "lan";}
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
